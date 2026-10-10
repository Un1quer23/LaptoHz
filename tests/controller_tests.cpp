#include "app.hpp"
#include "menu_probe.hpp"
#include "render_probe.hpp"
#include "mode_hint.hpp"
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wtsapi32.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <fstream>
#include <iterator>
using namespace rrs;
namespace {
struct Hardware {
    std::mutex mutex;
    PowerSource source = PowerSource::ac;
    int hz = 60, applied = 0, attempts = 0, delay = 0;
    bool fail = false, drr = false;
    Availability availability = Availability::ready;
    RecoveryState recovery = RecoveryState::not_needed;
    std::wstring route = L"inner|gpu-a|solo|fixed";
    std::vector<int> rates{60,240};
    std::wstring capability;
    DisplaySnapshot Inspect() {
        std::lock_guard lock(mutex);
        DisplaySnapshot value;
        value.policy = {source,availability,Screen{route,hz,static_cast<double>(hz),rates,false,drr,capability}};
        if (availability != Availability::ready) value.policy.screen.reset();
        value.device = L"test-inner"; value.monitor = L"test-monitor";
        return value;
    }
    void Set(PowerSource power, int rate) { std::lock_guard lock(mutex); source = power; hz = rate; }
    int Rate() { return Inspect().policy.screen->nominal_hz; }
    int Applications() { std::lock_guard lock(mutex); return applied; }
    int Attempts() { std::lock_guard lock(mutex); return attempts; }
    ApplyResult Apply(const DisplaySnapshot& before, int target, const std::function<bool()>& cancelled) {
        int latency = 0;
        { std::lock_guard lock(mutex); ++attempts; latency = delay; }
        ApplyResult result;
        for (int elapsed = 0; elapsed < latency; elapsed += 10) {
            if (cancelled()) { result.after = Inspect(); return result; }
            Sleep(10);
        }
        if (cancelled()) { result.after = Inspect(); return result; }
        {
            std::lock_guard lock(mutex);
            if (fail || drr || route != before.policy.screen->route || rates != before.policy.screen->supported_hz ||
                capability != before.policy.screen->capability_key || std::find(rates.begin(),rates.end(),target) == rates.end()) {
                result.code = DISP_CHANGE_FAILED; result.detail = LocalizedText{L"测试显示驱动拒绝本次切换",L"The test display driver rejected this change"};
                if (recovery != RecoveryState::not_needed) {
                    result.code = DISP_CHANGE_SUCCESSFUL; result.recovery = recovery;
                    result.recovery_code = recovery == RecoveryState::request_failed ? DISP_CHANGE_FAILED : DISP_CHANGE_SUCCESSFUL;
                    result.detail = RecoveryMessage(recovery)+LocalizedText{L"\n目标刷新率未通过验证",L"\nThe target refresh rate did not pass verification"};
                }
            } else { hz = target; ++applied; result.success = true; result.changed = true; }
        }
        result.after = Inspect(); return result;
    }
};
HWND ChildPopup(HWND owner, const wchar_t* caption) {
    struct Search { HWND owner, result; const wchar_t* caption; } search{owner,nullptr,caption};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& item = *reinterpret_cast<Search*>(data);
        if (GetWindow(window,GW_OWNER) != item.owner || !IsWindowVisible(window)) return TRUE;
        wchar_t text[128]{}; GetWindowTextW(window,text,128);
        if (std::wstring(text) == item.caption) { item.result = window; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
bool CaptureWindow(HWND window, const std::filesystem::path& path) {
    RECT rect{}; GetClientRect(window,&rect);
    HDC dc = GetDC(window), buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc,rect.right,rect.bottom);
    if (!buffer || !bitmap) { if (buffer) DeleteDC(buffer); if (bitmap) DeleteObject(bitmap); ReleaseDC(window,dc); return false; }
    const auto previous = SelectObject(buffer,bitmap);
    const bool painted = PrintWindow(window,buffer,PW_CLIENTONLY) != FALSE;
    SelectObject(buffer,previous);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0; bool saved = false;
    if (painted && Gdiplus::GdiplusStartup(&token,&input,nullptr) == Gdiplus::Ok) {
        { Gdiplus::Bitmap image(bitmap,nullptr);
          const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
          saved = image.Save(path.c_str(),&png,nullptr) == Gdiplus::Ok; }
        Gdiplus::GdiplusShutdown(token);
    }
    DeleteObject(bitmap); DeleteDC(buffer); ReleaseDC(window,dc); return saved;
}
}
int main() {
    USEROBJECTFLAGS flags{}; DWORD size = 0;
    if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&flags,sizeof(flags),&size) || !(flags.dwFlags & WSF_VISIBLE)) return 77;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t temporary[MAX_PATH]{}; if (!GetTempPathW(MAX_PATH,temporary)) return 1;
    const auto directory = std::filesystem::path(temporary)/(L"rrs-controller-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(directory); MarkStartupInitialized(directory);
    SaveLanguage(directory,Language::chinese);
    const auto captures = std::filesystem::current_path()/L"controller-captures";
    std::filesystem::create_directories(captures);
    Hardware hardware;
    std::atomic<bool> dark{false};
    AppServices services;
    services.inspect = [&] { return hardware.Inspect(); };
    services.power = [&] { return hardware.Inspect().policy.power; };
    services.appearance = [&] { return Appearance{dark,false}; };
    services.apply = [&](const DisplaySnapshot& snapshot, int hz, const std::function<bool()>& cancelled) { return hardware.Apply(snapshot,hz,cancelled); };
    services.directory = directory; services.caption = L"LaptoHz · controller test"; services.show_tray = false;
    const std::wstring caption = services.caption;
    const HWND foreground = GetForegroundWindow();
    int assertions = 0; bool passed = true;
    std::thread controller([&] {
        HWND host = nullptr;
        const auto check = [&](bool condition, const char* message) { ++assertions; if (!condition) throw std::runtime_error(message); };
        const auto wait = [&](const std::function<bool()>& condition, const char* message) {
            const auto deadline = GetTickCount64()+5000;
            while (!condition() && GetTickCount64() < deadline) Sleep(20);
            check(condition(),message);
        };
        const auto confirmation = [&] { return ChildPopup(host,Tr(Text::confirmation_caption).c_str()); };
        const auto notification = [&] { return ChildPopup(host,Tr(Text::notification_caption).c_str()); };
        const auto menu = [&] { return ActiveMenu(host); };
        const auto control = [&](WPARAM value) { return static_cast<ControlResult>(SendMessageW(host,kControlMessage,value,0)); };
        const auto mode = [&](WPARAM value, Mode expected) {
            check(control(value) == ControlResult::accepted,"A mode command acknowledges that it has been applied.");
            wait([&] { return LoadMode(directory) == expected; },"Mode selection is persisted.");
        };
        const auto power_event = [&] { PostMessageW(host,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0); };
        const auto resume = [&] { PostMessageW(host,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC,0); };
        const auto click = [&](HWND window, int id) { SendMessageW(GetDlgItem(window,id),BM_CLICK,0,0); };
        const auto set_target = [&](PowerSource source, int hz, bool saved = true) {
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() != nullptr; },"Open the unified menu for a target selection.");
            const HMENU root = menu();
            const UINT parent = source == PowerSource::ac ? kMenuAcTarget : kMenuBatteryTarget;
            const HMENU child = GetSubMenu(root,source == PowerSource::ac ? 6 : 7);
            const UINT command = ActionCommand(root,source == PowerSource::ac ? TrayActionKind::ac_target : TrayActionKind::battery_target,hz);
            check(command && SelectMenuItem(host,parent),"Open the correct target submenu with the keyboard.");
            wait([&] { return FindTrayMenuWindow(host,child) != nullptr; },"The native target submenu is visible.");
            check(SelectMenuItem(host,command,child),"Select the requested target from its frozen menu mapping.");
            wait([&] { return !menu(); },"Target selection closes the menu.");
            if (saved) wait([&] { const auto values = LoadRefreshTargets(directory); return (source == PowerSource::ac ? values.ac : values.battery) == hz; },"The selected target is persisted.");
        };
        const auto set_language = [&](Language language) {
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() != nullptr; },"Open the unified menu for a language selection.");
            const HMENU root = menu();
            MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_SUBMENU;
            check(GetMenuItemInfoW(root,kMenuLanguage,FALSE,&info) && info.hSubMenu,"The language submenu is present.");
            check(SelectMenuItem(host,kMenuLanguage),"Keyboard navigation opens the language submenu.");
            wait([&] { return FindTrayMenuWindow(host,info.hSubMenu) != nullptr; },"The language submenu is visible.");
            const UINT command = language == Language::english ? kMenuLanguageEnglish : language == Language::chinese ? kMenuLanguageChinese : kMenuLanguageSystem;
            check(SelectMenuItem(host,command,info.hSubMenu),"Choose the requested language with Enter.");
            wait([&] { return !menu() && LoadLanguage(directory) == language; },"Language selection closes the menu and persists.");
        };
        try {
            wait([&] { host = FindWindowW(kWindowClass,caption.c_str()); return host != nullptr; },"Controller host exists.");
            wait([&] { return hardware.Rate() == 240; },"Automatic startup applies the AC rule.");
            const auto menu_text = [&](UINT command) {
                wchar_t text[512]{};
                if (const HMENU root = menu()) GetMenuStringW(root,command,text,512,MF_BYCOMMAND);
                return std::wstring(text);
            };
            const int before_unavailable = hardware.Applications();
            for (const auto availability : {Availability::absent,Availability::remote}) {
                { std::lock_guard lock(hardware.mutex); hardware.availability = availability; }
                PostMessageW(host,WM_DISPLAYCHANGE,0,0);
                PostMessageW(host,WM_APP+1,0,NIN_SELECT);
                const auto reason = ReasonText(availability == Availability::absent ? Reason::no_screen : Reason::remote_session);
                wait([&] { return menu_text(kMenuSummary).find(reason) != std::wstring::npos; },"An unavailable display is explained in the real controller menu.");
                const auto summary = menu_text(kMenuSummary);
                check(summary.find(reason) == summary.rfind(reason),"The menu status shows the unavailable-display reason exactly once.");
                check(summary.find(L"内屏暂不可用 · 外部供电 · 自动模式") == 0,"The unavailable-display status retains display, power and mode information.");
                SendMessageW(host,WM_DISPLAYCHANGE,0,0); Sleep(150);
                check(menu_text(kMenuSummary) == summary,"Refreshing an open menu does not duplicate its unavailable-display reason.");
                MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"The unavailable-display menu closes normally.");
            }
            { std::lock_guard lock(hardware.mutex); hardware.availability = Availability::ready; }
            PostMessageW(host,WM_DISPLAYCHANGE,0,0);
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu_text(kMenuSummary) == L"240Hz · 外部供电 · 自动模式"; },"The old unavailable-display reason clears when the internal screen returns.");
            check(hardware.Applications() == before_unavailable,"Reading unavailable and restored menu status does not submit a display change.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"The restored-display menu closes normally.");
            const int before_rejected = hardware.Applications();
            check(control(4) == ControlResult::manual_required,"A direct rate command is rejected in automatic mode.");
            SendMessageW(host,WM_COMMAND,kMenu60,0); Sleep(120);
            check(hardware.Applications() == before_rejected && LoadMode(directory) == Mode::automatic,
                "A stale menu command cannot implicitly enter manual mode or change the rate.");
            mode(3,Mode::confirmation); hardware.Set(PowerSource::ac,60);
            PostMessageW(host,WM_DISPLAYCHANGE,0,0); resume(); Sleep(1200);
            check(!confirmation() && hardware.Rate() == 60,"Entering confirmation and waking on the same source do not prompt or switch.");
            hardware.Set(PowerSource::battery,240); power_event();
            wait([&] { return confirmation() != nullptr; },"An actual source transition creates the persistent prompt.");
            const HWND before_theme = confirmation();
            check(ClientPixel(before_theme,12,12) == ColorsFor({false,false}).background,"The controller uses the current light application theme.");
            dark = true; SendMessageW(host,WM_SETTINGCHANGE,0,0);
            check(confirmation() == before_theme && ClientPixel(before_theme,12,12) == ColorsFor({true,false}).background,
                "A settings message updates the persistent prompt to dark without recreating it.");
            Sleep(5200); check(confirmation() && hardware.Rate() == 240,"Waiting beyond five seconds does not switch or dismiss.");
            click(confirmation(),302); wait([&] { return !confirmation(); },"Keep-current dismisses the proposal.");
            power_event(); resume(); Sleep(1200);
            check(!confirmation() && hardware.Rate() == 240,"Dismissal survives duplicate power and resume messages.");
            hardware.Set(PowerSource::ac,60); power_event(); wait([&] { return confirmation() != nullptr; },"The next source change prompts again.");
            const int before_stale = hardware.Applications();
            hardware.Set(PowerSource::battery,60); click(confirmation(),301);
            wait([&] { return confirmation() && !IsWindowEnabled(GetDlgItem(confirmation(),301)); },"A stale approval updates to the already-matching state.");
            check(hardware.Applications() == before_stale && hardware.Rate() == 60,"A stale power approval never executes its old target.");
            click(confirmation(),303); wait([&] { return !confirmation(); },"Cross closes an updated prompt.");
            hardware.Set(PowerSource::ac,60); power_event(); wait([&] { return confirmation() != nullptr; },"AC transition prompts 240Hz.");
            { std::lock_guard lock(hardware.mutex); hardware.route = L"inner|gpu-b|solo|fixed"; }
            click(confirmation(),301); Sleep(800);
            check(confirmation() && hardware.Applications() == before_stale,"Changing route before approval invalidates the old ticket.");
            click(confirmation(),301);
            wait([&] { return hardware.Rate() == 240 && !confirmation(); },"Approving the refreshed route applies the recommendation and closes the prompt.");
            hardware.Set(PowerSource::battery,240); power_event(); wait([&] { return confirmation() != nullptr; },"Battery transition prompts 60Hz.");
            { std::lock_guard lock(hardware.mutex); hardware.fail = true; }
            const int attempts = hardware.Attempts(); click(confirmation(),301);
            wait([&] { return hardware.Attempts() > attempts && confirmation() && IsWindowEnabled(GetDlgItem(confirmation(),301)); },"A failed confirmation stays available for retry.");
            check(hardware.Rate() == 240 && LoadMode(directory) == Mode::confirmation,"Failure preserves the mode and previous rate.");
            const HWND language_confirmation = confirmation();
            const int before_language = hardware.Attempts();
            set_language(Language::english);
            check(confirmation() == language_confirmation && hardware.Attempts() == before_language &&
                LoadMode(directory) == Mode::confirmation && LoadRefreshTargets(directory) == RefreshTargets{},
                "Changing language keeps the same failed confirmation, mode and targets without contacting the display driver.");
            wchar_t english_button[64]{}; GetWindowTextW(GetDlgItem(confirmation(),301),english_button,64);
            check(std::wstring(english_button) == L"Switch to 60Hz","The existing confirmation action immediately uses English.");
            auto* translated_popup = reinterpret_cast<Popup*>(GetWindowLongPtrW(confirmation(),GWLP_USERDATA));
            check(translated_popup && translated_popup->Capture(captures/L"english-existing-confirmation.png"),"Capture the existing failed confirmation after switching to English.");
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() != nullptr; },"Open the English tray menu.");
            check(menu_text(kMenuSummary).starts_with(L"240Hz · On battery · Confirmation mode") &&
                menu_text(kMenuAuto) == L"Automatic mode","The controller's menu and summary use English.");
            dark = false; SendMessageW(host,WM_THEMECHANGED,0,0);
            check(CaptureWindow(FindTrayMenuWindow(host,menu()),captures/L"english-light-menu.png"),"Capture the light English menu.");
            dark = true; SendMessageW(host,WM_THEMECHANGED,0,0);
            check(CaptureWindow(FindTrayMenuWindow(host,menu()),captures/L"english-dark-menu.png"),"Capture the dark English menu.");
            MenuKey(host,VK_HOME);
            for (int step = 0; step < 3 && !(GetMenuState(menu(),kMenuAuto,MF_BYCOMMAND) & MF_HILITE); ++step) MenuKey(host,VK_DOWN);
            check((GetMenuState(menu(),kMenuAuto,MF_BYCOMMAND) & MF_HILITE) != 0,"Arrow navigation highlights the English Automatic mode item.");
            wait([&] { const HWND hint = FindModeHintWindow(host); return hint && IsWindowVisible(hint); },"English mode guidance appears on keyboard selection.");
            check(CaptureWindow(FindModeHintWindow(host),captures/L"english-mode-hint.png"),"Capture English mode guidance.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"Escape closes the English menu.");
            { std::lock_guard lock(hardware.mutex); hardware.fail = false; }
            click(confirmation(),301); wait([&] { return hardware.Rate() == 60 && !confirmation(); },"Retry succeeds after the driver recovers.");
            set_language(Language::chinese);
            hardware.Set(PowerSource::ac,60); power_event(); wait([&] { return confirmation() != nullptr; },"Pre-sleep prompt exists.");
            SendMessageW(host,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
            check(!confirmation(),"Suspending temporarily hides the prompt.");
            hardware.Set(PowerSource::battery,240); resume();
            wait([&] { return confirmation() != nullptr; },"Wake restores the prompt using the latest source.");
            wchar_t target[64]{}; GetWindowTextW(GetDlgItem(confirmation(),301),target,64);
            check(std::wstring(target).find(L"60Hz") != std::wstring::npos,"Sleep-time source changes update the recommendation.");
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() != nullptr; },"Left tray selection opens the unified menu.");
            check((GetMenuState(menu(),kMenu60,MF_BYCOMMAND) & MF_GRAYED) && (GetMenuState(menu(),kMenu240,MF_BYCOMMAND) & MF_GRAYED),
                "Confirmation mode disables both manual rate actions.");
            check(control(4) == ControlResult::manual_required && confirmation(),"Rejected direct switching leaves the confirmation proposal intact.");
            RECT before_manual_bounds{}; GetWindowRect(FindTrayMenuWindow(host,menu()),&before_manual_bounds);
            const int before_entering = hardware.Applications();
            check(SelectMenuItem(host,kMenuManual),"Keyboard navigation explicitly selects manual mode first.");
            wait([&] { return menu() && (GetMenuState(menu(),kMenuManual,MF_BYCOMMAND) & MF_CHECKED) &&
                LoadMode(directory) == Mode::manual && !confirmation(); },"Entering manual mode keeps the menu open and cancels the old confirmation.");
            check(hardware.Rate() == 240 && hardware.Applications() == before_entering && !notification(),
                "Entering manual mode preserves the rate and does not create a redundant mode notification.");
            RECT after_manual_bounds{}; GetWindowRect(FindTrayMenuWindow(host,menu()),&after_manual_bounds);
            check(EqualRect(&before_manual_bounds,&after_manual_bounds),"The continued menu preserves its position and width.");
            check((GetMenuState(menu(),kMenu240,MF_BYCOMMAND) & MF_GRAYED) && !(GetMenuState(menu(),kMenu60,MF_BYCOMMAND) & MF_GRAYED),
                "The continued manual menu disables the current 240Hz and enables 60Hz.");
            SendMessageW(host,WM_COMMAND,kMenu240,0); Sleep(120);
            check(hardware.Applications() == before_entering && !notification(),"A stale current-rate command has no display or popup side effect.");
            check(SelectMenuItem(host,kMenu60),"The same menu session allows selecting the other rate.");
            wait([&] { return hardware.Rate() == 60 && !menu(); },"The chosen manual rate applies and the menu closes.");
            hardware.Set(PowerSource::ac,60); power_event(); resume(); Sleep(1200);
            check(hardware.Rate() == 60 && !confirmation(),"Power and wake cannot overwrite manual mode.");
            mode(1,Mode::automatic); wait([&] { return hardware.Rate() == 240; },"Selecting automatic mode immediately reapplies its rule.");
            const int before_same = hardware.Applications();
            PostMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_RBUTTONUP,1));
            wait([&] { return menu() != nullptr; },"Right tray selection opens the same unified menu.");
            const HMENU right_menu = menu();
            SendMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_CONTEXTMENU,1));
            Sleep(250);
            check(menu() == right_menu,"The context-menu notification following right-button release must keep the same menu open.");
            SendMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_RBUTTONUP,1));
            SendMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_CONTEXTMENU,1));
            Sleep(250);
            check(menu() == right_menu,"Repeated packed right-click notifications do not close or recreate the menu.");
            dark = false; SendMessageW(host,WM_THEMECHANGED,0,0);
            check(menu() == right_menu && ClientPixel(FindTrayMenuWindow(host,right_menu),10,10) == ColorsFor({false,false}).background,
                "A theme-change message recolors the open unified menu without recreating it.");
            dark = true; SendMessageW(host,WM_SYSCOLORCHANGE,0,0);
            check(menu() == right_menu && ClientPixel(FindTrayMenuWindow(host,right_menu),10,10) == ColorsFor({true,false}).background,
                "System-color updates also recolor an open menu.");
            check(SelectMenuItem(host,kMenuManual),"The right-opened menu selects manual mode explicitly.");
            wait([&] { return menu() && LoadMode(directory) == Mode::manual &&
                (GetMenuState(menu(),kMenuManual,MF_BYCOMMAND) & MF_CHECKED); },"Right-opened menus also continue after mode selection.");
            const HMENU continued_menu = menu();
            SendMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_CONTEXTMENU,1)); Sleep(150);
            check(menu() == continued_menu,"A continued manual menu ignores duplicate right-click notifications.");
            check((GetMenuState(menu(),kMenu240,MF_BYCOMMAND) & MF_GRAYED) && hardware.Applications() == before_same && hardware.Rate() == 240,
                "Explicit mode selection preserves the current rate, which remains disabled.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"Escape closes the continued menu.");
            Sleep(180); check(!menu(),"Closing the continued menu does not schedule a late reopen.");
            check(control(5) == ControlResult::accepted,"A manual command can request the current rate for a fresh backend check.");
            Sleep(180); check(hardware.Applications() == before_same && !notification(),"A current-rate command does not call the driver or show an already-current popup.");
            hardware.Set(PowerSource::ac,60);
            SendMessageW(host,WM_COMMAND,kMenu60,0); Sleep(180);
            check(hardware.Applications() == before_same && !notification(),"If the display changes to the target before execution, the backend only refreshes state.");
            check(control(5) == ControlResult::accepted,"An explicit manual request is accepted after a fresh display-state change.");
            wait([&] { return hardware.Rate() == 240 && notification(); },"Manual requests inspect the latest hardware state instead of trusting a cached current marker.");
            SendMessageW(notification(),WM_CLOSE,0,0);
            { std::lock_guard lock(hardware.mutex); hardware.fail = true; }
            const int before_failure = hardware.Attempts();
            check(control(4) == ControlResult::accepted,"A manual attempt is accepted independently of driver success.");
            wait([&] { return hardware.Attempts() > before_failure && notification(); },"A manual driver failure is reported.");
            check(hardware.Rate() == 240 && LoadMode(directory) == Mode::manual,"A failed manual operation preserves both the mode and previous rate.");
            { std::lock_guard lock(hardware.mutex); hardware.fail = false; }
            const int before_manual = hardware.Attempts();
            { std::lock_guard lock(hardware.mutex); hardware.delay = 350; }
            check(control(4) == ControlResult::accepted,"The next manual operation is accepted.");
            wait([&] { return hardware.Attempts() > before_manual; },"Manual operation starts.");
            SendMessageW(host,WM_COMMAND,kMenuLanguageEnglish,0);
            check(LoadLanguage(directory) == Language::english && LoadMode(directory) == Mode::manual,
                "Language can be changed while a manual display operation is in flight.");
            check(control(5) == ControlResult::busy,"A duplicate submission is rejected while switching.");
            check(control(0) == ControlResult::accepted,"Selecting the existing manual mode is idempotent during switching.");
            hardware.Set(PowerSource::battery,240); power_event();
            wait([&] { return hardware.Rate() == 60; },"A power change during manual execution does not cancel the chosen target.");
            SendMessageW(host,WM_COMMAND,kMenuLanguageChinese,0);
            { std::lock_guard lock(hardware.mutex); hardware.delay = 500; }
            hardware.Set(PowerSource::ac,60); const int before_auto = hardware.Attempts(); mode(1,Mode::automatic);
            wait([&] { return hardware.Attempts() > before_auto; },"Automatic operation starts.");
            check(control(4) == ControlResult::manual_required,"An active automatic task cannot be replaced by an implicit manual command.");
            mode(0,Mode::manual);
            check(control(4) == ControlResult::accepted,"After explicit manual mode, a manual request may supersede the old automatic task.");
            Sleep(1000); check(hardware.Rate() == 60,"An old automatic result cannot overwrite the latest manual intent.");
            { std::lock_guard lock(hardware.mutex); hardware.delay = 0; }
            mode(1,Mode::automatic); wait([&] { return hardware.Rate() == 240; },"Return to automatic before session-lock validation.");
            mode(3,Mode::confirmation);
            SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
            check(control(4) == ControlResult::manual_required,"A locked direct request does not change confirmation mode.");
            mode(0,Mode::manual);
            check(control(4) == ControlResult::unavailable,"After explicit manual selection, a locked session still rejects switching.");
            SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
            Sleep(1000); check(hardware.Rate() == 240 && !confirmation(),"Unlock does not apply a blocked target or an automatic rule after manual intent.");
            hardware.Set(PowerSource::ac,60);
            { std::lock_guard lock(hardware.mutex); hardware.delay = 0; hardware.drr = true; }
            PostMessageW(host,WM_DISPLAYCHANGE,0,0); Sleep(700);
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() && (GetMenuState(menu(),kMenu60,MF_BYCOMMAND) & MF_GRAYED) &&
                (GetMenuState(menu(),kMenu240,MF_BYCOMMAND) & MF_GRAYED); },"DRR disables both menu actions.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"Escape closes the unified menu.");
            const int before_blocked = hardware.Applications(); SendMessageW(host,kControlMessage,5,0); Sleep(800);
            check(hardware.Applications() == before_blocked && hardware.Rate() == 60 && LoadMode(directory) == Mode::manual,"An unsupported manual operation stays manual without changing the display.");
            { std::lock_guard lock(hardware.mutex); hardware.drr = false; }
            hardware.Set(PowerSource::ac,60); mode(3,Mode::confirmation);
            check(SetFileAttributesW((directory/L"settings.ini").c_str(),FILE_ATTRIBUTE_READONLY) != FALSE,"Make the isolated settings read-only.");
            check(control(5) == ControlResult::manual_required,"Read-only settings do not permit implicit manual entry.");
            check(control(0) == ControlResult::not_saved,"Entering manual reports a save failure while using it for this run.");
            check(control(5) == ControlResult::accepted,"An explicitly selected runtime manual mode can still switch after a save failure.");
            wait([&] { return hardware.Rate() == 240 && LoadMode(directory) == Mode::confirmation; },"Save failure still applies the manual choice in this run, preserving the stored mode.");
            hardware.Set(PowerSource::battery,240); power_event(); resume(); Sleep(1200);
            check(hardware.Rate() == 240 && !confirmation(),"Save failure does not restore power-linked behavior during this run.");
            check(SetFileAttributesW((directory/L"settings.ini").c_str(),FILE_ATTRIBUTE_NORMAL) != FALSE,"Restore writable isolated settings.");
            mode(0,Mode::manual);
            PostMessageW(host,WM_APP+1,MAKELONG(12,18),MAKELONG(WM_CONTEXTMENU,1));
            wait([&] { return menu() != nullptr; },"A context-menu notification alone still opens the menu.");
            check(SelectMenuItem(host,kMenuManual),"Reselecting manual mode keeps the current menu task available.");
            wait([&] { return menu() != nullptr; },"Reselecting manual mode continues the menu.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu(); },"Escape closes a right-opened menu after duplicate notifications.");
            PostMessageW(host,WM_APP+1,0,NIN_KEYSELECT);
            wait([&] { return menu() != nullptr; },"Keyboard tray activation opens the unified menu.");
            check(SelectMenuItem(host,kMenuManual),"Start a continued menu before locking the session.");
            wait([&] { return menu() != nullptr; },"The menu continues before the session lock.");
            SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
            wait([&] { return !menu(); },"Session lock closes an open native menu.");
            SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);
            Sleep(180); check(!menu(),"Unlock cannot resurrect a cancelled continued menu.");
            { std::lock_guard lock(hardware.mutex); hardware.rates = {30,48,60,90,120,144,165,240}; hardware.capability = L"2560x1600|32|0"; }
            hardware.Set(PowerSource::ac,60); PostMessageW(host,WM_DISPLAYCHANGE,0,0);
            PostMessageW(host,WM_APP+1,0,NIN_SELECT);
            wait([&] { return menu() && ActionCommand(menu(),TrayActionKind::manual,120); },"A capability change rebuilds an open menu with new validated rates.");
            check(!(GetMenuState(menu(),ActionCommand(menu(),TrayActionKind::manual,90),MF_BYCOMMAND) & MF_GRAYED),"A new manual rate becomes selectable.");
            check(SelectMenuItem(host,ActionCommand(menu(),TrayActionKind::manual,90)),"Choose a dynamic manual command with arrows and Enter.");
            wait([&] { return hardware.Rate() == 90 && !menu(); },"A dynamically mapped manual rate executes correctly.");
            check(static_cast<ControlResult>(SendMessageW(host,kSwitchMessage,120,0)) == ControlResult::accepted,"The new control message accepts arbitrary validated integer rates.");
            wait([&] { return hardware.Rate() == 120; },"An arbitrary CLI-style switch applies its target.");
            const int before_current = hardware.Applications();
            Sleep(100);
            check(static_cast<ControlResult>(SendMessageW(host,kSwitchMessage,120,0)) == ControlResult::accepted,"An arbitrary current-rate request can refresh backend state.");
            Sleep(180); check(hardware.Applications() == before_current,"An already-current arbitrary target is not reapplied.");
            set_target(PowerSource::ac,165);
            check(hardware.Rate() == 120 && LoadMode(directory) == Mode::manual,"Editing targets in manual mode preserves the actual rate.");
            mode(1,Mode::automatic); wait([&] { return hardware.Rate() == 165; },"Entering automatic immediately uses the configured laptop target.");
            set_target(PowerSource::ac,144);
            wait([&] { return hardware.Rate() == 144; },"Editing the active automatic target immediately reconciles.");
            set_target(PowerSource::battery,48);
            check(hardware.Rate() == 144,"Editing the other source's target does not change a matching current rule.");
            mode(3,Mode::confirmation);
            hardware.Set(PowerSource::battery,144); power_event(); wait([&] { return confirmation() != nullptr; },"Custom battery targets participate in the confirmation flow.");
            GetWindowTextW(GetDlgItem(confirmation(),301),target,64);
            check(std::wstring(target).find(L"48Hz") != std::wstring::npos,"The persistent confirmation names the configured low rate.");
            set_target(PowerSource::battery,60);
            check(!confirmation() && hardware.Rate() == 144,"Changing confirmation targets dismisses old suggestions and keeps the actual rate.");
            power_event(); resume(); Sleep(1000);
            check(!confirmation(),"A new confirmation baseline ignores duplicate power and wake notifications.");
            hardware.Set(PowerSource::ac,60); power_event(); wait([&] { return confirmation() != nullptr; },"The next actual transition proposes the configured AC target.");
            set_target(PowerSource::ac,120);
            check(!confirmation() && hardware.Rate() == 60,"A target revision never executes an old approval.");
            hardware.Set(PowerSource::battery,120); power_event(); wait([&] { return confirmation() != nullptr; },"Another actual transition uses the new battery target.");
            const int before_capability = hardware.Applications();
            { std::lock_guard lock(hardware.mutex); hardware.capability = L"1920x1080|32|0"; hardware.rates = {30,48,60,120,240}; }
            click(confirmation(),301); Sleep(350);
            check(confirmation() && hardware.Applications() == before_capability,"An approval bound to old resolution/rates cannot apply even on the same output path.");
            click(confirmation(),301); wait([&] { return hardware.Rate() == 60 && !confirmation(); },"A revalidated confirmation can be retried after capability changes.");
            mode(0,Mode::manual); Sleep(120);
            const int before_stale_rates = hardware.Applications();
            { std::lock_guard lock(hardware.mutex); hardware.rates = {60,90}; }
            check(static_cast<ControlResult>(SendMessageW(host,kSwitchMessage,120,0)) == ControlResult::accepted,"A request can be received before its cached capability change is observed.");
            Sleep(350);
            check(hardware.Rate() == 60 && hardware.Applications() == before_stale_rates,"Fresh worker inspection rejects stale capability-bound manual requests.");
            check(static_cast<ControlResult>(SendMessageW(host,kSwitchMessage,120,0)) == ControlResult::unavailable,"Subsequent requests reject a known missing rate synchronously.");
            mode(1,Mode::automatic); hardware.Set(PowerSource::ac,60); power_event(); Sleep(800);
            check(hardware.Rate() == 60 && LoadRefreshTargets(directory).ac == 120,"A saved unavailable target is preserved without substitution.");
            SendMessageW(host,WM_COMMAND,kMenuAcAuto,0);
            wait([&] { return LoadRefreshTargets(directory).ac == 0 && hardware.Rate() == 90; },"Resetting an unavailable target uses the new maximum immediately.");
            mode(0,Mode::manual);
            { std::lock_guard lock(hardware.mutex); hardware.delay = 600; }
            const int before_busy = hardware.Attempts();
            check(control(4) == ControlResult::accepted,"Start a manual switch for target-edit exclusion.");
            wait([&] { return hardware.Attempts() > before_busy; },"The serial worker started switching.");
            SendMessageW(host,WM_COMMAND,kMenuBatteryAuto,0);
            check(LoadRefreshTargets(directory).battery == 60,"Target changes during switching are rejected without saving.");
            PostMessageW(host,WM_APP+1,0,NIN_SELECT); wait([&] { return menu() != nullptr; },"Busy state still allows viewing the unified menu.");
            check((GetMenuState(menu(),kMenuAcTarget,MF_BYCOMMAND) & MF_GRAYED) && (GetMenuState(menu(),kMenuBatteryTarget,MF_BYCOMMAND) & MF_GRAYED),"Both target submenus are disabled while switching.");
            MenuKey(host,VK_ESCAPE); wait([&] { return !menu() && hardware.Rate() == 60; },"The manual choice completes without a target revision cancelling it.");
            { std::lock_guard lock(hardware.mutex); hardware.delay = 0; }
            check(SetFileAttributesW((directory/L"settings.ini").c_str(),FILE_ATTRIBUTE_READONLY) != FALSE,"Make target settings read-only.");
            set_target(PowerSource::ac,60,false);
            check(LoadRefreshTargets(directory).ac == 0 && hardware.Rate() == 60,"A failed target save preserves disk values and manual rate.");
            check(control(1) == ControlResult::not_saved,"A mode save also reports read-only settings.");
            Sleep(250); check(hardware.Rate() == 60,"The runtime automatic rule uses the unsaved target rather than the stored default.");
            check(SetFileAttributesW((directory/L"settings.ini").c_str(),FILE_ATTRIBUTE_NORMAL) != FALSE,"Restore writable settings.");
            SendMessageW(host,WM_COMMAND,kMenuAcAuto,0); wait([&] { return hardware.Rate() == 90; },"A successful target retry restores dynamic defaults.");
            mode(0,Mode::manual); hardware.Set(PowerSource::battery,90); power_event(); resume(); Sleep(1000);
            check(hardware.Rate() == 90 && !confirmation(),"Generic manual mode remains independent of power and wake after configuration changes.");
            const auto recovery_log = [&] {
                std::ifstream input(directory/L"switcher.log",std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
            };
            const auto capture_popup = [&](HWND window, const wchar_t* filename) {
                auto* popup = reinterpret_cast<Popup*>(GetWindowLongPtrW(window,GWLP_USERDATA));
                check(popup && popup->Capture(captures/filename),"The real recovery result window can be captured for visual verification.");
            };
            const auto close_notification = [&] {
                const HWND window = notification();
                if (!window) return;
                RECT area{}; GetClientRect(window,&area);
                const int inset = MulDiv(26,GetDpiForWindow(window),96);
                const LPARAM point = MAKELPARAM(area.right-inset,inset);
                SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);
                SendMessageW(window,WM_LBUTTONUP,0,point);
                wait([&] { return !notification(); },"The drawn notification close cross hides the notice before the next scenario.");
            };
            close_notification();
            { std::lock_guard lock(hardware.mutex); hardware.fail = true; hardware.recovery = RecoveryState::verified; }
            const int manual_recovery_attempts = hardware.Attempts();
            check(control(4) == ControlResult::accepted,"A manual target can fail verification and return a verified restoration.");
            wait([&] { return hardware.Attempts() > manual_recovery_attempts && notification(); },"Verified restoration produces the target-failure notification.");
            check(hardware.Rate() == 90 && LoadMode(directory) == Mode::manual,"Verified recovery retains the original rate and manual mode.");
            wait([&] { return recovery_log().find(Utf8(L"恢复结果 · 已恢复原显示状态并验证 · 恢复接口返回 0")) != std::string::npos; },
                "The controller logs the explicit verified result and independent recovery code.");
            capture_popup(notification(),L"recovery-manual-verified.png");
            close_notification();
            { std::lock_guard lock(hardware.mutex); hardware.recovery = RecoveryState::verification_failed; }
            const int unverified_recovery_attempts = hardware.Attempts();
            check(control(4) == ControlResult::accepted,"A second manual request can report an unverified restoration.");
            wait([&] { return hardware.Attempts() > unverified_recovery_attempts && notification(); },"Unverified restoration remains a target failure.");
            wait([&] { return recovery_log().find(Utf8(L"恢复结果 · 恢复请求已提交，回读未通过验证 · 恢复接口返回 0")) != std::string::npos; },
                "The controller does not treat a successful recovery request as verification.");
            check(hardware.Rate() == 90 && hardware.Attempts() == unverified_recovery_attempts+1,"An unverified restoration does not automatically retry the target.");
            capture_popup(notification(),L"recovery-manual-unverified.png");
            close_notification();
            mode(3,Mode::confirmation);
            close_notification();
            hardware.Set(PowerSource::ac,60); power_event();
            wait([&] { return confirmation() != nullptr; },"Confirmation mode still creates a current source-change suggestion.");
            { std::lock_guard lock(hardware.mutex); hardware.recovery = RecoveryState::verified; }
            const int confirmation_recovery_attempts = hardware.Attempts();
            click(confirmation(),301);
            wait([&] { return hardware.Attempts() > confirmation_recovery_attempts && confirmation() && IsWindowEnabled(GetDlgItem(confirmation(),301)); },
                "Verified recovery leaves the failed confirmation available for an explicit retry.");
            capture_popup(confirmation(),L"recovery-confirmation-verified.png");
            if (notification()) capture_popup(notification(),L"recovery-confirmation-existing-notice.png");
            check(hardware.Rate() == 60,"Verified confirmation recovery preserves the original physical rate.");
            check(LoadMode(directory) == Mode::confirmation,"Verified confirmation recovery preserves confirmation mode.");
            check(!notification(),"Confirmation recovery does not emit a successful target notification.");
            { std::lock_guard lock(hardware.mutex); hardware.fail = false; hardware.recovery = RecoveryState::not_needed; }
            click(confirmation(),301);
            wait([&] { return hardware.Rate() == 90 && !confirmation(); },"An explicit retry can still complete the target after recovery.");
            check(StartupInitialized(directory),"All mode changes preserve the legacy startup marker.");
        } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; passed = false; }
        if (host) PostMessageW(host,WM_CLOSE,0,0);
    });
    App app(std::move(services)); AppOptions options; options.skip_startup_initialization = true;
    const int app_result = app.Run(GetModuleHandleW(nullptr),options);
    controller.join();
    SetFileAttributesW((directory/L"settings.ini").c_str(),FILE_ATTRIBUTE_NORMAL);
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,(directory/L"settings.ini").c_str());
    for (const auto* name : {L"settings.ini",L"switcher.log",L"switcher.previous.log"}) std::filesystem::remove(directory/name);
    std::filesystem::remove(directory);
    if (foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
    if (passed && !app_result) std::cout << "PASS: " << assertions << " controller assertions, using isolated simulated hardware and settings.\n";
    return passed && !app_result ? 0 : 1;
}
