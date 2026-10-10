#include "app.hpp"
#include "startup.hpp"
#include <shellapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <limits>

namespace rrs {
namespace {
constexpr UINT kTray = WM_APP+1, kResult = WM_APP+2, kReconcileLater = WM_APP+3, kBusyChanged = WM_APP+4;
constexpr UINT_PTR kDebounceTimer = 1, kTrayRetryTimer = 2, kPreviewCaptureTimer = 3;
constexpr UINT kStartup = 102, kLogs = 103, kExit = 104, kStatus = 105;
constexpr UINT kAuto = 110, kConfirm = 111, kManual = 112;
constexpr UINT k60 = kMenu60, k240 = kMenu240;
constexpr GUID kAcSource{0x5d3e9a59,0xe9d5,0x4b00,{0xa6,0xbd,0xff,0x34,0xff,0x51,0x65,0x48}};
// GUID_SESSION_DISPLAY_STATUS; do not use the console-wide setting for user sessions.
constexpr GUID kSessionDisplayState{0x2b84c20e,0xad23,0x4ddf,{0x93,0xdb,0x05,0xff,0xbd,0x7e,0xfc,0xa5}};
int Priority(Trigger trigger) {
    switch (trigger) {
    case Trigger::startup: return 5;
    case Trigger::enable: return 4;
    case Trigger::resume: return 3;
    case Trigger::power_change: return 2;
    case Trigger::display_change: return 1;
    }
    return 0;
}
std::wstring Summary(const Update* status, Mode mode, bool saved, bool include_availability_reason = true) {
    std::wstring mode_text = ModeText(mode);
    if (!saved) mode_text += L" · "+Tr(Text::settings_unsaved);
    if (!status) return Tr(Text::reading_status)+L" · "+mode_text;
    std::wstring text = status->snapshot.policy.screen ? CurrentRateText(*status->snapshot.policy.screen) : Tr(Text::screen_unavailable);
    text += L" · "+PowerText(status->snapshot.policy.power)+L" · "+mode_text;
    if (status->failed) text += L" · "+Tr(status->decision.reason == Reason::unsupported_mode || status->decision.reason == Reason::no_valid_modes ? Text::target_unavailable : Text::switch_failed);
    else if (include_availability_reason && status->snapshot.policy.availability != Availability::ready)
        text += L" · "+ReasonText(CheckTarget(status->snapshot.policy,60).reason);
    return text;
}
}
bool App::Usable() const { return !locked_ && !suspended_ && display_on_; }
bool App::Busy() const { return active_action_ != 0 || applying_epoch_ == epoch_; }
bool App::SettingsSaved() const { return mode_saved_ && ac_saved_ && battery_saved_ && language_saved_; }
PowerSource App::Power() const { return services_.power ? services_.power() : ReadPower(); }
DisplaySnapshot App::Inspect() const { return services_.inspect ? services_.inspect() : backend_.Inspect(); }
ApplyResult App::Apply(const DisplaySnapshot& snapshot, int hz, const std::function<bool()>& cancelled) const {
    return services_.apply ? services_.apply(snapshot,hz,cancelled) : backend_.Apply(snapshot,hz,cancelled);
}
int App::Run(HINSTANCE instance, const AppOptions& options) {
    instance_ = instance; options_ = options;
    if (!options_.preview) {
        directory_ = services_.directory.empty() ? DataDirectory() : services_.directory;
        logger_ = std::make_unique<Logger>(directory_);
        language_ = LoadLanguage(directory_); SetUiLanguage(language_);
        mode_ = LoadMode(directory_); confirmation_baseline_ = Power();
        bool invalid = false; targets_ = LoadRefreshTargets(directory_,&invalid);
        if (invalid) logger_->Write(Tr(Text::log_invalid_targets));
        logger_->Write(Tr(Text::log_preparing,{kVersion,Tr(options_.startup ? Text::log_sign_in_launch : Text::log_manual_launch)}));
    } else { language_ = options_.language.value_or(Language::system); SetUiLanguage(language_); }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls); cls.lpfnWndProc = Procedure; cls.hInstance = instance_;
    cls.lpszClassName = kWindowClass; cls.hCursor = LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassExW(&cls)) {
        if (logger_) logger_->Write(Tr(Text::log_register_failed)+L" · " + NativeError(GetLastError()));
        return 2;
    }
    const std::wstring caption = options_.preview ? std::wstring(kAppName) + L" · preview" : services_.caption.empty() ? kAppName : services_.caption;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,kWindowClass,caption.c_str(),WS_OVERLAPPED,
        0,0,0,0,nullptr,nullptr,instance_,this);
    if (!hwnd_ || !popup_.Initialize(instance_,hwnd_) ||
        !confirmation_popup_.Initialize(instance_,hwnd_,PopupKind::confirmation)) {
        if (logger_) logger_->Write(Tr(Text::log_window_failed)+L" · " + NativeError(GetLastError()));
        if (hwnd_) DestroyWindow(hwnd_);
        return 3;
    }
    RefreshAppearance();
    if (options_.preview) {
        previous_foreground_ = GetForegroundWindow(); preview_start_ = GetTickCount64();
        popup_.Show(Tr(Text::switched_to,{L"60Hz"}),Tr(Text::power_battery));
        preview_focus_preserved_ = previous_foreground_ == GetForegroundWindow();
        SetTimer(hwnd_,kPreviewCaptureTimer,150,nullptr);
    } else {
        logger_->Write(Tr(Text::log_started,{kVersion,ModeText(mode_),ExecutablePath().wstring()}));
        taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated"); InstallTray();
        power_notification_ = RegisterPowerSettingNotification(hwnd_,&kAcSource,DEVICE_NOTIFY_WINDOW_HANDLE);
        display_notification_ = RegisterPowerSettingNotification(hwnd_,&kSessionDisplayState,DEVICE_NOTIFY_WINDOW_HANDLE);
        resume_notification_ = RegisterSuspendResumeNotification(hwnd_,DEVICE_NOTIFY_WINDOW_HANDLE);
        events_ready_ = power_notification_ && display_notification_ && resume_notification_ &&
            WTSRegisterSessionNotification(hwnd_,NOTIFY_FOR_THIS_SESSION);
        if (!events_ready_) {
            logger_->Write(Tr(Text::events_failed)+L" · "+NativeError(GetLastError()));
            popup_.Show(Tr(Text::events_failed),Tr(Text::events_restart),true);
            mode_ = Mode::manual;
        }
        if (!options_.startup && !options_.skip_startup_initialization && !StartupInitialized(directory_)) {
            std::wstring error;
            if (SetStartup(true,error)) {
                logger_->Write(Tr(Text::log_first_startup));
                if (!MarkStartupInitialized(directory_)) popup_.Show(Tr(Text::settings_not_saved),Tr(Text::check_settings_folder),true);
            } else { logger_->Write(Tr(Text::log_enable_startup_failed)+L" · "+error); popup_.Show(Tr(Text::startup_failed),error,true); }
        } else if (!options_.startup && !options_.skip_startup_initialization) {
            bool changed = false; std::wstring error;
            if (!MigrateStartup(changed,error)) {
                logger_->Write(Tr(Text::log_migrate_startup_failed)+L" · " + error); popup_.Show(Tr(Text::startup_migration_incomplete),error,true);
            } else if (changed) logger_->Write(Tr(Text::log_startup_migrated));
        }
        worker_ = std::thread(&App::Work,this);
        WorkRequest request; request.trigger = Trigger::startup; Submit(request);
    }
    MSG message{}; int code = 0;
    while ((code = GetMessageW(&message,nullptr,0,0)) > 0) {
        if (confirmation_popup_.Translate(message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    StopWorker();
    if (power_notification_) UnregisterPowerSettingNotification(power_notification_);
    if (display_notification_) UnregisterPowerSettingNotification(display_notification_);
    if (resume_notification_) UnregisterSuspendResumeNotification(resume_notification_);
    if (!options_.preview) WTSUnRegisterSessionNotification(hwnd_);
    if (icon_) DestroyIcon(icon_);
    if (logger_) logger_->Write(Tr(Text::log_exited));
    return code < 0 ? 4 : static_cast<int>(message.wParam);
}
LRESULT CALLBACK App::Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->Message(message,wparam,lparam) : DefWindowProcW(hwnd,message,wparam,lparam);
}
void App::Request(Trigger trigger, UINT delay_ms) {
    if (options_.preview) return;
    if (!Usable()) { reconcile_after_unlock_ = true; return; }
    if (!debounce_ || Priority(trigger) >= Priority(*debounce_)) debounce_ = trigger;
    SetTimer(hwnd_,kDebounceTimer,delay_ms,nullptr);
}
std::uint64_t App::Submit(WorkRequest request) {
    request.id = ++next_request_; request.epoch = epoch_; request.policy_revision = policy_revision_; request.mode = mode_;
    request.targets = targets_;
    if (request.kind == WorkKind::manual && request.capability_key.empty() && status_) request.capability_key = CapabilityKey(status_->snapshot.policy);
    if (request.kind == WorkKind::manual && !request.capability_version && status_ && status_->snapshot.policy.screen)
        request.capability_version = status_->snapshot.policy.screen->capability_version;
    const auto id = request.id;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return 0;
        if (request.kind == WorkKind::reconcile && !jobs_.empty() && jobs_.back().kind == WorkKind::reconcile &&
            jobs_.back().epoch == request.epoch) {
            if (Priority(jobs_.back().trigger) > Priority(request.trigger)) request.trigger = jobs_.back().trigger;
            jobs_.back() = request;
        } else jobs_.push_back(request);
    }
    condition_.notify_all(); return id;
}
void App::StopWorker() {
    stopping_ = true; ++epoch_; condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void App::Work() {
    std::uint64_t seen_revision = ~std::uint64_t{0};
    std::uint64_t capability_version = 0;
    std::wstring seen_capability;
    const auto stamp = [&](DisplaySnapshot& snapshot) {
        const auto key = CapabilityKey(snapshot.policy);
        if (key != seen_capability) { seen_capability = key; ++capability_version; }
        if (snapshot.policy.screen) snapshot.policy.screen->capability_version = capability_version;
    };
    for (;;) {
        WorkRequest request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock,[&] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            request = jobs_.front(); jobs_.pop_front();
        }
        if (request.epoch != epoch_ || request.mode != mode_ ||
            (request.kind == WorkKind::manual && request.mode != Mode::manual)) continue;
        if (request.policy_revision != seen_revision) {
            policy_ = Policy{}; confirmation_policy_.Reset(confirmation_baseline_);
            seen_revision = request.policy_revision;
        }
        auto update = std::make_unique<Update>(); update->request = request;
        const auto interrupted = [&] { return stopping_ || request.epoch != epoch_ || request.mode != mode_ || !Usable(); };
        bool reconcile = false;
        try {
            if (request.kind == WorkKind::dismiss && request.confirmation) confirmation_policy_.Dismiss(request.confirmation->id);
            for (int attempt = 0; attempt < 3; ++attempt) {
                update->snapshot = Inspect();
                stamp(update->snapshot);
                const auto& input = update->snapshot.policy;
                const PowerSource job_power = input.power;
                const bool power_bound = request.mode != Mode::manual;
                const auto cancelled = [&] { return interrupted() || (power_bound && Power() != job_power); };
                if (interrupted()) { update->cancelled = true; break; }
                if (request.mode == Mode::confirmation) update->confirmation = confirmation_policy_.Update(input,request.targets);
                if (request.kind == WorkKind::manual) {
                    update->decision = CapabilityKey(input) == request.capability_key && input.screen &&
                        input.screen->capability_version == request.capability_version ? CheckTarget(input,request.target_hz) :
                        Decision{Action::reject,Reason::capabilities_changed,request.target_hz};
                } else if (request.kind == WorkKind::confirm) {
                    if (request.mode != Mode::confirmation || !request.confirmation ||
                        !confirmation_policy_.Matches(*request.confirmation,input,request.targets)) {
                        update->decision = {Action::observe,Reason::none,0};
                        update->detail = Localize(Text::confirmation_updated); break;
                    }
                    update->decision = CheckTarget(input,request.confirmation->target_hz);
                } else if (request.mode == Mode::automatic) {
                    update->decision = policy_.Evaluate(input,attempt ? Trigger::resume : request.trigger,events_ready_ && Usable(),request.targets);
                } else update->decision = {Action::observe,Reason::none,0};
                bool retryable = false;
                if (update->decision.action == Action::apply) {
                    if (cancelled()) { update->cancelled = true; reconcile = true; break; }
                    logger_->Write(Tr(Text::log_switch_requested,{std::to_wstring(update->decision.target_hz),ModeText(request.mode),PowerText(job_power)}));
                    applying_epoch_ = request.epoch; PostMessageW(hwnd_,kBusyChanged,0,0);
                    struct ApplyingGuard {
                        std::atomic<std::uint64_t>& value; HWND window;
                        ~ApplyingGuard() { value = ~std::uint64_t{0}; PostMessageW(window,kBusyChanged,0,0); }
                    } applying{applying_epoch_,hwnd_};
                    const auto outcome = Apply(update->snapshot,update->decision.target_hz,cancelled);
                    update->snapshot = outcome.after; update->changed = outcome.changed;
                    stamp(update->snapshot);
                    update->cancelled = cancelled(); update->failed = !outcome.success && !update->cancelled; update->detail = outcome.detail;
                    logger_->Write(Tr(Text::log_display_result,{std::to_wstring(outcome.code),outcome.detail.Get()}));
                    if (outcome.recovery != RecoveryState::not_needed)
                        logger_->Write(Tr(Text::log_recovery_result,{RecoveryStateText(outcome.recovery),
                            outcome.recovery_code ? std::to_wstring(*outcome.recovery_code) : Tr(Text::log_not_requested)}));
                    retryable = update->failed && outcome.retryable;
                    if (request.kind == WorkKind::confirm && outcome.success && !update->cancelled) {
                        confirmation_policy_.Dismiss(request.confirmation->id); update->completed_confirmation = true;
                    }
                    reconcile = update->cancelled && request.mode != Mode::manual;
                } else if (update->decision.action == Action::reject || update->decision.action == Action::wait) {
                    update->failed = request.kind == WorkKind::manual || request.kind == WorkKind::confirm || update->decision.action == Action::reject;
                    update->detail = ReasonMessage(update->decision.reason);
                    if (!update->snapshot.detail.empty()) update->detail += L" · "+update->snapshot.detail;
                    retryable = update->failed && update->decision.reason == Reason::query_failed;
                }
                if (request.mode == Mode::confirmation) update->confirmation = confirmation_policy_.Update(update->snapshot.policy,request.targets);
                if (!retryable || attempt == 2 || cancelled()) break;
                std::unique_lock lock(mutex_);
                if (condition_.wait_for(lock,std::chrono::seconds(1),cancelled)) break;
            }
            if (request.mode == Mode::confirmation && !update->completed_confirmation && update->cancelled && !interrupted()) {
                update->snapshot = Inspect();
                stamp(update->snapshot);
                update->confirmation = confirmation_policy_.Update(update->snapshot.policy,request.targets);
            }
        } catch (const std::exception& exception) {
            update->failed = true; update->decision.reason = Reason::query_failed;
            const std::string message(exception.what()); update->detail = Localize(Text::internal_error,{std::wstring(message.begin(),message.end())});
        }
        if (stopping_) return;
        if (reconcile) PostMessageW(hwnd_,kReconcileLater,0,0);
        auto* payload = update.release();
        if (!PostMessageW(hwnd_,kResult,0,reinterpret_cast<LPARAM>(payload))) delete payload;
    }
}
void App::Accept(std::unique_ptr<Update> update) {
    if (update->request.epoch != epoch_ || update->request.mode != mode_) return;
    if (active_action_ == update->request.id) active_action_ = 0;
    if (mode_ == Mode::confirmation) {
        if (!pending_confirmation_ || !update->confirmation || pending_confirmation_->id != update->confirmation->id) confirmation_error_.clear();
        pending_confirmation_ = update->confirmation;
        if (update->failed && update->request.kind == WorkKind::confirm) confirmation_error_ = update->detail;
        if (update->completed_confirmation) { pending_confirmation_.reset(); confirmation_error_.clear(); confirmation_popup_.Hide(); }
    }
    status_ = std::move(update); UpdateTray();
    if (Usable()) {
        const auto body = [&](const std::wstring& text) {
            return SettingsSaved() ? text : Tr(Text::settings_warning,{text});
        };
        if (status_->changed && !status_->cancelled) {
            last_failure_.clear();
            popup_.Show(Tr(Text::switched_to,{CurrentRateText(*status_->snapshot.policy.screen)}),
                body(PowerText(status_->snapshot.policy.power)+L" · "+ModeText(mode_)),!SettingsSaved(),status_->snapshot.device);
        } else if (status_->failed && status_->request.kind != WorkKind::confirm) {
            const auto signature = std::to_wstring(status_->decision.target_hz)+L"|"+status_->detail.Get();
            if (signature != last_failure_ || status_->request.kind == WorkKind::manual)
                popup_.Show(Tr(Text::switch_incomplete),body(status_->detail.Get()),true,status_->snapshot.device);
            last_failure_ = signature;
        }
        ShowConfirmation();
        ArrangePopups();
    }
}
bool App::SetMode(Mode mode, bool announce) {
    if (options_.preview || stopping_) return false;
    if (mode != Mode::manual && !events_ready_) {
        popup_.Show(Tr(Text::power_rules_unavailable),Tr(Text::power_rules_restart),true); return false;
    }
    if (mode == Mode::manual && mode_ == Mode::manual) {
        mode_saved_ = SaveMode(directory_,mode);
        if (!mode_saved_) popup_.Show(Tr(Text::mode_not_saved),Tr(Text::mode_active_unsaved,{ModeText(mode)}),true);
        UpdateTray(); return true;
    }
    mode_ = mode; confirmation_baseline_ = Power(); ++epoch_; ++policy_revision_;
    active_action_ = 0; pending_confirmation_.reset(); confirmation_error_.clear(); last_failure_.clear();
    confirmation_popup_.Hide(); KillTimer(hwnd_,kDebounceTimer); debounce_.reset();
    { std::lock_guard lock(mutex_); jobs_.clear(); }
    condition_.notify_all();
    mode_saved_ = SaveMode(directory_,mode);
    if (!mode_saved_) {
        popup_.Show(Tr(Text::mode_not_saved),Tr(Text::mode_active_unsaved,{ModeText(mode)}),true);
        logger_->Write(Tr(Text::log_mode_save_failed)+L" · "+ModeText(mode));
    } else if (announce) popup_.Show(Tr(Text::entered_mode,{ModeText(mode)}),
        Tr(mode == Mode::automatic ? Text::check_power_rate : mode == Mode::confirmation ? Text::ask_next_power : Text::manual_keeps_choice));
    logger_->Write(Tr(Text::log_mode_changed)+L" · "+ModeText(mode));
    WorkRequest request; request.trigger = Trigger::enable; Submit(request); UpdateTray();
    ArrangePopups();
    return true;
}
void App::SetLanguage(Language language) {
    if (options_.preview || stopping_) return;
    language_ = language;
    SetUiLanguage(language_);
    language_saved_ = SaveLanguage(directory_,language_);
    RefreshLanguage();
    popup_.Show(Tr(language_saved_ ? Text::language_changed : Text::language_not_saved),
        Tr(language_saved_ ? Text::language_saved : Text::language_active_unsaved),!language_saved_);
    ArrangePopups();
}
void App::RefreshLanguage() {
    popup_.Hide();
    popup_.RefreshLanguage(); confirmation_popup_.RefreshLanguage();
    // Rebuild the menu so its text measurements and hint titles use the new
    // language. Refreshing a confirmation retains its token and action state.
    if (active_menu_) { menu_refresh_ = true; EndMenu(); }
    UpdateTray(); ShowConfirmation(); ArrangePopups();
}
ControlResult App::ManualSwitch(int hz, const std::wstring& capability, std::uint64_t version) {
    if (hz <= 1) return ControlResult::invalid;
    if (mode_ != Mode::manual) return ControlResult::manual_required;
    if (Busy()) return ControlResult::busy;
    if (options_.preview || stopping_ || !Usable()) return ControlResult::unavailable;
    if (!status_ || (CheckTarget(status_->snapshot.policy,hz).action != Action::apply &&
        CheckTarget(status_->snapshot.policy,hz).action != Action::unchanged)) return ControlResult::unavailable;
    if (!capability.empty() && capability != CapabilityKey(status_->snapshot.policy)) return ControlResult::unavailable;
    if (version && (!status_->snapshot.policy.screen || version != status_->snapshot.policy.screen->capability_version)) return ControlResult::unavailable;
    WorkRequest request; request.kind = WorkKind::manual; request.target_hz = hz;
    request.capability_key = capability;
    request.capability_version = version;
    active_action_ = Submit(request);
    UpdateTray();
    return active_action_ ? ControlResult::accepted : ControlResult::unavailable;
}
ControlResult App::SetTarget(PowerSource source, int hz, const std::wstring& capability, std::uint64_t version) {
    if (source == PowerSource::unknown || (hz != 0 && hz <= 1)) return ControlResult::invalid;
    if (Busy()) return ControlResult::busy;
    if (options_.preview || stopping_ || !Usable()) return ControlResult::unavailable;
    if (hz && (!status_ || (!capability.empty() && capability != CapabilityKey(status_->snapshot.policy)) ||
        (version && (!status_->snapshot.policy.screen || status_->snapshot.policy.screen->capability_version != version)) ||
        (CheckTarget(status_->snapshot.policy,hz).action != Action::apply && CheckTarget(status_->snapshot.policy,hz).action != Action::unchanged)))
        return ControlResult::unavailable;
    if (source == PowerSource::ac) targets_.ac = hz; else targets_.battery = hz;
    confirmation_baseline_ = Power(); ++epoch_; ++policy_revision_;
    pending_confirmation_.reset(); confirmation_error_.clear(); last_failure_.clear(); confirmation_popup_.Hide();
    KillTimer(hwnd_,kDebounceTimer); debounce_.reset();
    { std::lock_guard lock(mutex_); jobs_.clear(); }
    condition_.notify_all();
    const bool saved = SaveRefreshTarget(directory_,source,hz);
    (source == PowerSource::ac ? ac_saved_ : battery_saved_) = saved;
    const auto label = Tr(source == PowerSource::ac ? Text::ac_target : Text::battery_target);
    const std::wstring value = hz ? std::to_wstring(hz)+L"Hz" : Tr(source == PowerSource::ac ? Text::highest_rate : Text::prefer_60);
    logger_->Write(Tr(Text::log_target_changed,{label,value})+(saved ? L"" : L" · "+Tr(Text::log_unsaved)));
    if (!saved) popup_.Show(Tr(Text::target_not_saved),Tr(Text::target_active_unsaved),true);
    else popup_.Show(Tr(Text::target_set,{label,value}),Tr(mode_ == Mode::automatic ? Text::check_power_rules :
        mode_ == Mode::confirmation ? Text::ask_next_power : Text::manual_keeps_rate));
    WorkRequest request; request.trigger = Trigger::enable; Submit(request); UpdateTray(); ArrangePopups();
    return saved ? ControlResult::accepted : ControlResult::not_saved;
}
void App::OnPopup(const PopupEvent& event) {
    if (event.kind != PopupKind::confirmation || mode_ != Mode::confirmation ||
        !pending_confirmation_ || event.token != pending_confirmation_->id) return;
    if (event.action == PopupAction::dismiss || event.action == PopupAction::second) {
        const auto previous = *pending_confirmation_; ++epoch_; active_action_ = 0;
        { std::lock_guard lock(mutex_); jobs_.clear(); }
        pending_confirmation_.reset(); confirmation_error_.clear(); confirmation_popup_.Hide();
        WorkRequest request; request.kind = WorkKind::dismiss; request.confirmation = previous; Submit(request);
        logger_->Write(Tr(Text::log_suggestion_dismissed)); return;
    }
    if (active_action_ || !Usable() || !pending_confirmation_->needed || pending_confirmation_->reason != Reason::none) return;
    WorkRequest request; request.kind = WorkKind::confirm; request.confirmation = pending_confirmation_;
    active_action_ = Submit(request); ShowConfirmation();
}
void App::ShowConfirmation() {
    if (mode_ != Mode::confirmation || !pending_confirmation_ || !Usable()) { confirmation_popup_.Hide(); return; }
    const auto& proposal = *pending_confirmation_;
    const std::wstring source = Tr(proposal.power == PowerSource::ac ? Text::power_connected : proposal.power == PowerSource::battery ? Text::power_disconnected : Text::power_unknown);
    std::wstring body = source+L"\n";
    if (status_ && status_->snapshot.policy.screen) body += Tr(Text::current_rate_prefix,{CurrentRateText(*status_->snapshot.policy.screen)});
    body += proposal.target_hz > 1 ? Tr(Text::suggested_rate,{std::to_wstring(proposal.target_hz)}) : Tr(Text::suggestion_unavailable);
    if (proposal.reason != Reason::none) body += L"\n"+ReasonText(proposal.reason);
    else if (!proposal.needed) body += L"\n"+Tr(Text::already_suggested);
    if (!confirmation_error_.empty()) body += L"\n"+confirmation_error_.Get();
    const std::wstring first = active_action_ ? Tr(Text::switching) : proposal.needed ? Tr(Text::switch_to_button,{std::to_wstring(proposal.target_hz)}) :
        proposal.reason == Reason::none ? Tr(Text::already_rate_button,{std::to_wstring(proposal.target_hz)}) : Tr(Text::cannot_switch_button);
    const std::array<PopupButton,2> buttons{{{first,proposal.needed && proposal.reason == Reason::none && !active_action_,true},
        {Tr(proposal.needed ? Text::keep_current : Text::close),!active_action_,false}}};
    confirmation_popup_.ShowActions(Tr(confirmation_error_.empty() ? Text::confirm_title : Text::switch_incomplete),
        body,buttons,proposal.id,status_ ? status_->snapshot.device : L"",!confirmation_error_.empty());
    ArrangePopups();
}
void App::ArrangePopups() {
    if (active_menu_) {
        if (const auto bounds = TrayMenuBounds(hwnd_,active_menu_)) {
            confirmation_popup_.Avoid(*bounds); popup_.Avoid(*bounds);
        }
    }
    if (confirmation_popup_.Visible()) popup_.Avoid(confirmation_popup_.Bounds());
}
void App::BlockSession() {
    if (menu_session_) menu_cancelled_ = true;
    ++epoch_; active_action_ = 0; reconcile_after_unlock_ = true; condition_.notify_all();
    if (active_menu_) EndMenu();
    popup_.Hide(); confirmation_popup_.Hide();
}
void App::RestoreSession() {
    if (!Usable()) return;
    const bool reconcile = reconcile_after_unlock_; reconcile_after_unlock_ = false;
    Request(reconcile ? Trigger::resume : Trigger::display_change,750);
}
void App::InstallTray() {
    if (!services_.show_tray) return;
    RefreshTrayIcon();
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP; data.uCallbackMessage = kTray; data.hIcon = icon_;
    auto tip = std::wstring(kAppName)+L"\n"+Summary(status_.get(),mode_,SettingsSaved());
    tip.resize(std::min(tip.size(),std::size(data.szTip)-1)); std::copy(tip.begin(),tip.end(),data.szTip);
    tray_installed_ = Shell_NotifyIconW(NIM_ADD,&data) != FALSE;
    if (tray_installed_) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); KillTimer(hwnd_,kTrayRetryTimer); }
    else SetTimer(hwnd_,kTrayRetryTimer,2000,nullptr);
}
void App::UpdateTray() {
    RefreshMenu();
    if (!tray_installed_) return;
    NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = 1; data.uFlags = NIF_TIP | NIF_SHOWTIP;
    auto tip = std::wstring(kAppName)+L"\n"+Summary(status_.get(),mode_,SettingsSaved());
    tip.resize(std::min(tip.size(),std::size(data.szTip)-1)); std::copy(tip.begin(),tip.end(),data.szTip);
    Shell_NotifyIconW(NIM_MODIFY,&data);
}
void App::StatusPopup() {
    popup_.Show(Tr(Text::status_title),Summary(status_.get(),mode_,SettingsSaved()),!SettingsSaved() || (status_ && status_->failed),status_ ? status_->snapshot.device : L"");
    ArrangePopups();
}
void App::Menu() {
    // A right click can deliver both WM_RBUTTONUP and WM_CONTEXTMENU.
    // The menu loop dispatches the second callback while this call is active.
    if (menu_session_ || stopping_ || !Usable()) return;
    menu_session_ = true; menu_cancelled_ = false;
    struct SessionGuard { bool& active; ~SessionGuard() { active = false; } } guard{menu_session_};
    RefreshAppearance();
    Request(Trigger::display_change,50);
    POINT position{}; GetCursorPos(&position);
    NOTIFYICONIDENTIFIER identifier{}; identifier.cbSize = sizeof(identifier); identifier.hWnd = hwnd_; identifier.uID = 1;
    TPMPARAMS parameters{}; parameters.cbSize = sizeof(parameters);
    const bool anchored = SUCCEEDED(Shell_NotifyIconGetRect(&identifier,&parameters.rcExclude));
    if (anchored) position = POINT{parameters.rcExclude.right,parameters.rcExclude.top};
    // Keep the hidden owner on the tray monitor so menu metrics use its DPI.
    SetWindowPos(hwnd_,nullptr,position.x,position.y,0,0,SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    UINT width = 0; bool continuation = false;
    while (IsWindow(hwnd_) && !stopping_ && !menu_cancelled_ && Usable()) {
        const auto state = MenuState(); menu_capability_ = state.input ? CapabilityKey(*state.input) : L""; menu_refresh_ = false;
        menu_version_ = state.input && state.input->screen ? state.input->screen->capability_version : 0;
        HMENU menu = CreateTrayMenu(state);
        if (!menu) { popup_.Show(Tr(Text::menu_unavailable),NativeError(GetLastError()),true); break; }
        active_menu_ = menu;
        TrayMenuRenderer renderer;
        if (!renderer.Attach(menu,hwnd_,appearance_,width)) {
            active_menu_ = nullptr; DestroyTrayMenu(menu); popup_.Show(Tr(Text::menu_unavailable),Tr(Text::menu_style_failed),true); break;
        }
        width = renderer.Width(); menu_renderer_ = &renderer;
        SetForegroundWindow(hwnd_);
        const UINT selected = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_WORKAREA |
            TPM_RIGHTALIGN | TPM_BOTTOMALIGN | (continuation ? TPM_NOANIMATION : 0),
            position.x,position.y,hwnd_,anchored ? &parameters : nullptr);
        const auto action = ResolveTrayAction(menu,selected);
        menu_renderer_ = nullptr; active_menu_ = nullptr; DestroyTrayMenu(menu);
        if (!IsWindow(hwnd_) || stopping_ || menu_cancelled_ || !Usable()) break;
        if (menu_refresh_) { continuation = true; continue; }
        if (selected == kManual) {
            popup_.Hide();
            if (SetMode(Mode::manual,false)) { continuation = true; continue; }
        } else if (selected) Command(selected,action);
        break;
    }
    if (IsWindow(hwnd_) && !stopping_) { PostMessageW(hwnd_,WM_NULL,0,0); ShowConfirmation(); ArrangePopups(); }
}
TrayMenuState App::MenuState() const {
    // The menu appends its current availability reason. Tooltips and status
    // popups keep the complete summary through the default argument.
    TrayMenuState state; state.summary = Summary(status_.get(),mode_,SettingsSaved(),false);
    if (status_) state.input = status_->snapshot.policy;
    state.mode = mode_; state.busy = Busy(); state.usable = Usable(); state.targets = targets_;
    state.events_ready = events_ready_; state.startup = StartupRegistered(); state.language = language_; return state;
}
void App::RefreshMenu(bool repaint) {
    if (!active_menu_) return;
    const auto capability = status_ ? CapabilityKey(status_->snapshot.policy) : L"";
    const auto version = status_ && status_->snapshot.policy.screen ? status_->snapshot.policy.screen->capability_version : 0;
    if (capability != menu_capability_ || version != menu_version_) { menu_refresh_ = true; EndMenu(); return; }
    UpdateTrayMenu(active_menu_,MenuState());
    if (repaint) if (HWND window = FindTrayMenuWindow(hwnd_,active_menu_)) { InvalidateRect(window,nullptr,FALSE); UpdateWindow(window); }
}
void App::RefreshAppearance() {
    appearance_ = services_.appearance ? services_.appearance() : ReadAppearance();
    popup_.RefreshAppearance(appearance_); confirmation_popup_.RefreshAppearance(appearance_);
    if (menu_renderer_) menu_renderer_->RefreshAppearance(appearance_);
    RefreshTrayIcon();
}
void App::RefreshTrayIcon() {
    if (options_.preview || !services_.show_tray || !hwnd_) return;
    bool dark = appearance_.dark;
    if (appearance_.high_contrast) {
        const COLORREF background = GetSysColor(COLOR_WINDOW);
        dark = 299 * GetRValue(background) + 587 * GetGValue(background) + 114 * GetBValue(background) < 128000;
    } else {
        DWORD light = dark ? 0 : 1, bytes = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes) == ERROR_SUCCESS) dark = light == 0;
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd",nullptr);
    UINT dpi = GetDpiForWindow(taskbar ? taskbar : hwnd_);
    if (!dpi) dpi = 96;
    const WORD resource = dark ? 202 : 201;
    if (icon_ && icon_resource_ == resource && icon_dpi_ == dpi) return;
    const int width = GetSystemMetricsForDpi(SM_CXSMICON,dpi), height = GetSystemMetricsForDpi(SM_CYSMICON,dpi);
    HICON replacement = static_cast<HICON>(LoadImageW(instance_,MAKEINTRESOURCEW(resource),IMAGE_ICON,width,height,0));
    if (!replacement) replacement = static_cast<HICON>(LoadImageW(instance_,MAKEINTRESOURCEW(101),IMAGE_ICON,width,height,0));
    if (!replacement) return;
    const HICON previous = icon_; icon_ = replacement; icon_resource_ = resource; icon_dpi_ = dpi;
    if (tray_installed_) {
        NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = 1;
        data.uFlags = NIF_ICON; data.hIcon = icon_;
        Shell_NotifyIconW(NIM_MODIFY,&data);
    }
    if (previous) DestroyIcon(previous);
    if (logger_) logger_->Write(Tr(Text::log_tray_icon,{std::to_wstring(width),std::to_wstring(height),
        Tr(dark ? Text::log_dark_taskbar : Text::log_light_taskbar)}));
}
void App::Command(UINT command, std::optional<TrayAction> action) {
    if (!action && active_menu_) action = ResolveTrayAction(active_menu_,command);
    if (!action && (command == k60 || command == k240)) action = TrayAction{TrayActionKind::manual,command == k60 ? 60 : 240,L""};
    if (!action && (command == kMenuAcAuto || command == kMenuBatteryAuto))
        action = TrayAction{command == kMenuAcAuto ? TrayActionKind::ac_target : TrayActionKind::battery_target,0,L""};
    if (action) {
        if (action->kind == TrayActionKind::manual) {
            if (mode_ == Mode::manual && status_ && CheckTarget(status_->snapshot.policy,action->hz).action == Action::apply)
                ManualSwitch(action->hz,action->capability_key,action->capability_version);
        } else SetTarget(action->kind == TrayActionKind::ac_target ? PowerSource::ac : PowerSource::battery,action->hz,action->capability_key,action->capability_version);
    }
    else if (command == kMenuLanguageSystem || command == kMenuLanguageChinese || command == kMenuLanguageEnglish)
        SetLanguage(command == kMenuLanguageChinese ? Language::chinese : command == kMenuLanguageEnglish ? Language::english : Language::system);
    else if (command == kAuto || command == kConfirm || command == kManual) SetMode(command == kAuto ? Mode::automatic : command == kConfirm ? Mode::confirmation : Mode::manual);
    else if (command == kStartup) {
        const bool enable = !StartupRegistered(); std::wstring error;
        if (SetStartup(enable,error) && MarkStartupInitialized(directory_)) {
            popup_.Show(Tr(enable ? Text::startup_enabled : Text::startup_disabled),Tr(Text::current_user_only));
            logger_->Write(Tr(enable ? Text::log_startup_enabled : Text::log_startup_disabled));
        } else { popup_.Show(Tr(Text::startup_incomplete),error.empty() ? Tr(Text::cannot_save_settings) : error,true); logger_->Write(Tr(Text::log_startup_failed)+L" · "+error); }
        ArrangePopups();
    } else if (command == kLogs) {
        logger_->Write(Tr(Text::logs_menu));
        std::wstring error;
        if (!OpenDiagnosticLog(hwnd_,logger_->File(),error)) {
            logger_->Write(Tr(Text::log_open_failed)+L" · " + error);
            popup_.Show(Tr(Text::cannot_open_log),error,true); ArrangePopups();
        }
    } else if (command == kExit) PostMessageW(hwnd_,WM_CLOSE,0,0);
    else if (command == kStatus) StatusPopup();
}
LRESULT App::Message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created_ && message == taskbar_created_) { tray_installed_ = false; InstallTray(); return 0; }
    switch (message) {
    case kBusyChanged: UpdateTray(); return 0;
    case kReconcileLater: if (mode_ != Mode::manual) Request(Trigger::resume); return 0;
    case kControlMessage:
        if (options_.preview || stopping_) return static_cast<LRESULT>(ControlResult::unavailable);
        if (wparam == 0 || wparam == 1 || wparam == 3) {
            if (!SetMode(wparam == 0 ? Mode::manual : wparam == 1 ? Mode::automatic : Mode::confirmation))
                return static_cast<LRESULT>(ControlResult::unavailable);
            return static_cast<LRESULT>(mode_saved_ ? ControlResult::accepted : ControlResult::not_saved);
        }
        if (wparam == 2) { StatusPopup(); return static_cast<LRESULT>(ControlResult::accepted); }
        if (wparam == 4 || wparam == 5) return static_cast<LRESULT>(ManualSwitch(wparam == 4 ? 60 : 240));
        return static_cast<LRESULT>(ControlResult::invalid);
    case kSwitchMessage:
        if (wparam > static_cast<WPARAM>(std::numeric_limits<int>::max())) return static_cast<LRESULT>(ControlResult::invalid);
        return static_cast<LRESULT>(ManualSwitch(static_cast<int>(wparam)));
    case kResult: Accept(std::unique_ptr<Update>(reinterpret_cast<Update*>(lparam))); return 0;
    case kPopupAction: {
        std::unique_ptr<PopupEvent> event(reinterpret_cast<PopupEvent*>(lparam));
        if (event) OnPopup(*event);
        return 0;
    }
    case kTray:
        switch (LOWORD(lparam)) {
        case WM_CONTEXTMENU: case WM_RBUTTONUP: case NIN_SELECT: case NIN_KEYSELECT: Menu(); break;
        case NIN_POPUPOPEN: Request(Trigger::display_change,50); break;
        }
        return 0;
    case WM_COMMAND: Command(LOWORD(wparam)); return 0;
    case WM_MEASUREITEM:
        if (menu_renderer_ && lparam && menu_renderer_->Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
        break;
    case WM_DRAWITEM:
        if (menu_renderer_ && lparam && menu_renderer_->Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
        break;
    case WM_MENUCHAR: if (active_menu_) return MAKELRESULT(0,MNC_IGNORE); break;
    case WM_MENUSELECT:
        if (menu_renderer_) menu_renderer_->MenuSelection(reinterpret_cast<HMENU>(lparam),LOWORD(wparam),HIWORD(wparam));
        return 0;
    case WM_SETTINGCHANGE: {
        const auto previous = CurrentLanguage();
        SetUiLanguage(language_);
        if (CurrentLanguage() != previous) RefreshLanguage();
        RefreshAppearance(); return 0;
    }
    case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: RefreshAppearance(); return 0;
    case WM_INITMENUPOPUP: if (active_menu_) RefreshMenu(false); return 0;
    case WM_ENTERIDLE:
        if (wparam == MSGF_MENU) { if (menu_renderer_) menu_renderer_->RefreshVisibleWindows(); ArrangePopups(); }
        return 0;
    case WM_DPICHANGED: RefreshTrayIcon(); return 0;
    case WM_DISPLAYCHANGE: RefreshTrayIcon(); Request(Trigger::display_change); return TRUE;
    case WM_DEVICECHANGE: Request(Trigger::display_change); return TRUE;
    case WM_POWERBROADCAST:
        if (wparam == PBT_POWERSETTINGCHANGE && lparam) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lparam);
            if (IsEqualGUID(setting->PowerSetting,kAcSource)) Request(Trigger::power_change);
            else if (IsEqualGUID(setting->PowerSetting,kSessionDisplayState) && setting->DataLength == sizeof(DWORD)) {
                DWORD state = 0; std::memcpy(&state,setting->Data,sizeof(state)); display_on_ = state != 0;
                if (!display_on_) BlockSession(); else RestoreSession();
            }
        } else if (wparam == PBT_APMPOWERSTATUSCHANGE) Request(Trigger::power_change);
        else if (wparam == PBT_APMSUSPEND) { suspended_ = true; BlockSession(); }
        else if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) { suspended_ = false; reconcile_after_unlock_ = true; RestoreSession(); }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wparam == WTS_SESSION_LOCK || wparam == WTS_CONSOLE_DISCONNECT || wparam == WTS_REMOTE_CONNECT) { locked_ = true; BlockSession(); }
        else if (wparam == WTS_SESSION_UNLOCK || wparam == WTS_CONSOLE_CONNECT || wparam == WTS_REMOTE_DISCONNECT) { locked_ = false; RestoreSession(); }
        return 0;
    case WM_TIMER:
        if (wparam == kDebounceTimer) {
            KillTimer(hwnd_,kDebounceTimer);
            if (debounce_) {
                if (Usable()) { WorkRequest request; request.trigger = *debounce_; Submit(request); }
                else reconcile_after_unlock_ = true;
                debounce_.reset();
            }
        } else if (wparam == kTrayRetryTimer) InstallTray();
        else if (wparam == kPreviewCaptureTimer) {
            KillTimer(hwnd_,kPreviewCaptureTimer);
            if (!options_.capture.empty()) preview_capture_saved_ = popup_.Capture(options_.capture);
        }
        return 0;
    case kPopupClosed:
        if (options_.preview && wparam == static_cast<WPARAM>(PopupKind::notification)) {
            std::ostringstream report;
            report << "{\n  \"elapsedMs\": " << GetTickCount64()-preview_start_
                << ",\n  \"focusPreserved\": " << (preview_focus_preserved_ && previous_foreground_ == GetForegroundWindow() ? "true" : "false")
                << ",\n  \"captureSaved\": " << (preview_capture_saved_ ? "true" : "false") << ",\n  \"closed\": true\n}\n";
            if (!options_.report.empty()) WriteText(options_.report,report.str());
            PostMessageW(hwnd_,WM_CLOSE,0,0);
        } else if (wparam != static_cast<WPARAM>(PopupKind::confirmation)) { ShowConfirmation(); ArrangePopups(); }
        return 0;
    case WM_CLOSE: {
        menu_cancelled_ = true;
        if (active_menu_) EndMenu();
        KillTimer(hwnd_,kDebounceTimer); KillTimer(hwnd_,kTrayRetryTimer); StopWorker();
        MSG queued{};
        while (PeekMessageW(&queued,hwnd_,kResult,kResult,PM_REMOVE)) delete reinterpret_cast<Update*>(queued.lParam);
        while (PeekMessageW(&queued,hwnd_,kPopupAction,kPopupAction,PM_REMOVE)) delete reinterpret_cast<PopupEvent*>(queued.lParam);
        if (tray_installed_) { NOTIFYICONDATAW data{}; data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = 1; Shell_NotifyIconW(NIM_DELETE,&data); }
        popup_.Hide(); confirmation_popup_.Hide(); DestroyWindow(hwnd_); return 0;
    }
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wparam) PostMessageW(hwnd_,WM_CLOSE,0,0); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_,message,wparam,lparam);
}
}
