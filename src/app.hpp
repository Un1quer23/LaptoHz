#pragma once
#include "display.hpp"
#include "popup.hpp"
#include "tray_menu.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace rrs {
inline constexpr UINT kControlMessage = WM_APP + 10;
inline constexpr UINT kSwitchMessage = WM_APP + 11;
enum class ControlResult : LRESULT { accepted = 1, manual_required, busy, unavailable, not_saved, invalid };
enum class WorkKind { reconcile, manual, confirm, dismiss };
struct AppOptions {
    bool startup = false, skip_startup_initialization = false, preview = false;
    std::filesystem::path report, capture;
    std::optional<Language> language;
};
// Defaults use the real Windows backend. Tests can exercise the same message loop
// with isolated settings and deterministic hardware snapshots.
struct AppServices {
    std::function<DisplaySnapshot()> inspect;
    std::function<ApplyResult(const DisplaySnapshot&, int, const std::function<bool()>&)> apply;
    std::function<PowerSource()> power;
    std::function<Appearance()> appearance;
    std::filesystem::path directory;
    std::wstring caption;
    bool show_tray = true;
};
struct WorkRequest {
    WorkKind kind = WorkKind::reconcile;
    Trigger trigger = Trigger::display_change;
    Mode mode = Mode::automatic;
    int target_hz = 0;
    std::uint64_t id = 0, epoch = 0, policy_revision = 0;
    std::optional<Confirmation> confirmation;
    RefreshTargets targets;
    std::wstring capability_key;
    std::uint64_t capability_version = 0;
};
struct Update {
    WorkRequest request;
    DisplaySnapshot snapshot;
    Decision decision;
    std::optional<Confirmation> confirmation;
    bool changed = false, failed = false, cancelled = false, completed_confirmation = false;
    LocalizedText detail;
};
class App {
public:
    explicit App(AppServices services = {}) : services_(std::move(services)) {}
    int Run(HINSTANCE instance, const AppOptions& options);
private:
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT Message(UINT message, WPARAM wparam, LPARAM lparam);
    bool Usable() const;
    bool Busy() const;
    bool SettingsSaved() const;
    PowerSource Power() const;
    DisplaySnapshot Inspect() const;
    ApplyResult Apply(const DisplaySnapshot& snapshot, int hz, const std::function<bool()>& cancelled) const;
    void Request(Trigger trigger, UINT delay_ms = 500);
    std::uint64_t Submit(WorkRequest request);
    void Work();
    void StopWorker();
    void Accept(std::unique_ptr<Update> update);
    bool SetMode(Mode mode, bool announce = true);
    void SetLanguage(Language language);
    void RefreshLanguage();
    ControlResult ManualSwitch(int target_hz, const std::wstring& capability = L"", std::uint64_t version = 0);
    ControlResult SetTarget(PowerSource source, int hz, const std::wstring& capability = L"", std::uint64_t version = 0);
    void OnPopup(const PopupEvent& event);
    void ShowConfirmation();
    void ArrangePopups();
    void BlockSession();
    void RestoreSession();
    void InstallTray();
    void UpdateTray();
    void Menu();
    TrayMenuState MenuState() const;
    void RefreshMenu(bool repaint = true);
    void RefreshAppearance();
    void RefreshTrayIcon();
    void Command(UINT command, std::optional<TrayAction> action = std::nullopt);
    void StatusPopup();
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HICON icon_ = nullptr;
    WORD icon_resource_ = 0;
    UINT icon_dpi_ = 0;
    HPOWERNOTIFY power_notification_ = nullptr, display_notification_ = nullptr, resume_notification_ = nullptr;
    UINT taskbar_created_ = 0;
    AppOptions options_;
    AppServices services_;
    std::filesystem::path directory_;
    std::unique_ptr<Logger> logger_;
    Popup popup_, confirmation_popup_;
    HMENU active_menu_ = nullptr;
    bool menu_session_ = false, menu_cancelled_ = false;
    bool menu_refresh_ = false;
    std::wstring menu_capability_;
    std::uint64_t menu_version_ = 0;
    TrayMenuRenderer* menu_renderer_ = nullptr;
    Appearance appearance_;
    DisplayBackend backend_;
    Policy policy_;
    ConfirmationPolicy confirmation_policy_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<WorkRequest> jobs_;
    std::optional<Trigger> debounce_;
    std::atomic<bool> stopping_{false}, locked_{false}, suspended_{false}, display_on_{true};
    std::atomic<Mode> mode_{Mode::automatic};
    std::atomic<PowerSource> confirmation_baseline_{PowerSource::unknown};
    std::atomic<std::uint64_t> epoch_{0}, policy_revision_{0};
    std::atomic<std::uint64_t> applying_epoch_{~std::uint64_t{0}};
    RefreshTargets targets_;
    bool ac_saved_ = true, battery_saved_ = true;
    Language language_ = Language::system;
    bool language_saved_ = true;
    std::uint64_t next_request_ = 0, active_action_ = 0;
    bool reconcile_after_unlock_ = false, tray_installed_ = false, events_ready_ = false, mode_saved_ = true;
    std::unique_ptr<Update> status_;
    std::optional<Confirmation> pending_confirmation_;
    std::wstring last_failure_;
    LocalizedText confirmation_error_;
    ULONGLONG preview_start_ = 0;
    HWND previous_foreground_ = nullptr;
    bool preview_focus_preserved_ = false, preview_capture_saved_ = false;
};
}
