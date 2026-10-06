#include "menu_probe.hpp"
#include "render_probe.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
using namespace rrs;
namespace {
std::atomic<int> theme{0};
std::atomic<int> draws{0};
TrayMenuState* live_state = nullptr;
HMENU live_menu = nullptr;
Appearance TestAppearance() { return {theme == 1,theme == 2}; }
LRESULT CALLBACK MenuOwner(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* renderer = reinterpret_cast<TrayMenuRenderer*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (renderer && message == WM_MEASUREITEM && lparam && renderer->Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
    if (renderer && message == WM_DRAWITEM && lparam && renderer->Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) { ++draws; return TRUE; }
    if (renderer && message == WM_ENTERIDLE && wparam == MSGF_MENU) { renderer->RefreshVisibleWindows(); return 0; }
    if (renderer && message == WM_MENUSELECT) { renderer->MenuSelection(reinterpret_cast<HMENU>(lparam),LOWORD(wparam),HIWORD(wparam)); return 0; }
    if (message == WM_MENUCHAR) return MAKELRESULT(0,MNC_IGNORE);
    if (message == WM_APP+50 && live_state && live_menu) {
        live_state->mode = Mode::manual; live_state->summary = L"240Hz · 外部供电 · 手动模式";
        UpdateTrayMenu(live_menu,*live_state);
        if (HWND menu_window = FindTrayMenuWindow(window,live_menu)) {
            InvalidateRect(menu_window,nullptr,TRUE); UpdateWindow(menu_window);
        }
        return 0;
    }
    if (renderer && (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE)) {
        renderer->RefreshAppearance(TestAppearance()); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
bool Enabled(HMENU menu, UINT id) { return (GetMenuState(menu,id,MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED)) == 0; }
bool Checked(HMENU menu, UINT id) { return (GetMenuState(menu,id,MF_BYCOMMAND) & MF_CHECKED) != 0; }
std::wstring Label(HMENU menu, UINT id) { wchar_t text[512]{}; GetMenuStringW(menu,id,text,512,MF_BYCOMMAND); return text; }
bool Capture(HWND window, const std::filesystem::path& path) {
    RECT rect{}; GetClientRect(window,&rect);
    HDC dc = GetDC(window), buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc,rect.right,rect.bottom);
    if (!buffer || !bitmap) { if (buffer) DeleteDC(buffer); if (bitmap) DeleteObject(bitmap); ReleaseDC(window,dc); return false; }
    HGDIOBJ previous = SelectObject(buffer,bitmap);
    const BOOL painted = PrintWindow(window,buffer,PW_CLIENTONLY); SelectObject(buffer,previous);
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
    USEROBJECTFLAGS station{}; DWORD length = 0;
    if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&station,sizeof(station),&length) || !(station.dwFlags & WSF_VISIBLE)) return 77;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    TrayMenuState state; state.summary = L"240Hz · 外部供电 · 自动模式"; state.startup = true;
    state.input = PolicyInput{PowerSource::ac,Availability::ready,Screen{L"inner|gpu|fixed",240,240.0,{60,240},false,false}};
    HMENU menu = nullptr; HWND owner = nullptr; int assertions = 0, result = 0;
    const HWND foreground = GetForegroundWindow();
    POINT original_cursor{}; GetCursorPos(&original_cursor);
    const auto check = [&](bool condition, const char* text) { ++assertions; if (!condition) throw std::runtime_error(text); };
    try {
        menu = CreateTrayMenu(state); check(menu != nullptr,"Create a unified native menu.");
        check(GetMenuItemID(menu,2) == kMenuAuto && GetMenuItemID(menu,4) == kMenuManual &&
            GetSubMenu(menu,6) != nullptr && GetSubMenu(menu,7) != nullptr &&
            GetSubMenu(menu,10) == nullptr && GetMenuItemID(menu,10) == kMenu60 && GetMenuItemID(menu,11) == kMenu240,
            "Modes precede target submenus and direct top-level manual rate actions.");
        check(Checked(menu,kMenuAcAuto) && Checked(menu,kMenuBatteryAuto),"Both dynamic target defaults have radio selections.");
        check(Label(menu,kMenuAcTarget).find(L"240Hz") != std::wstring::npos && Label(menu,kMenuBatteryTarget).find(L"60Hz") != std::wstring::npos,
            "Target parents show resolved defaults.");
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"Automatic mode disables both manual rate actions.");
        check(Label(menu,kMenuRates).find(L"请先选择手动模式") != std::wstring::npos && !Enabled(menu,kMenuRates),
            "A noninteractive readable heading explains how to enable the actions.");
        check(Label(menu,kMenu60) == L"60Hz" && Label(menu,kMenu240) == L"240Hz（当前）","Rate labels separate the target from current-state text.");
        check(!Checked(menu,kMenu240) && !Checked(menu,kMenu60) && Label(menu,kMenu240).find(L"当前") != std::wstring::npos &&
            Label(menu,kMenu60).find(L"当前") == std::wstring::npos,"Automatic mode shows the current rate in text without a misleading action check.");
        check(Checked(menu,kMenuAuto) && !Checked(menu,kMenuConfirm) && !Checked(menu,kMenuManual),"Exactly one mode is selected.");
        check(Checked(menu,kMenuStartup),"Startup preference is shown.");
        for (const UINT id : {kMenu60,kMenu240,kMenuAuto,kMenuConfirm,kMenuManual,kMenuStartup,kMenuLogs,kMenuExit})
            check(Label(menu,id).find(L'&') == std::wstring::npos && Label(menu,id).find(L'(') == std::wstring::npos,"Menu labels contain no accelerator or shortcut suffix.");
        state.input->screen->nominal_hz = 60; state.input->screen->physical_hz = 60;
        state.mode = Mode::confirmation; state.startup = false; UpdateTrayMenu(menu,state);
        check(!Checked(menu,kMenu60) && !Checked(menu,kMenu240) && Label(menu,kMenu60).find(L"当前") != std::wstring::npos &&
            Label(menu,kMenu240).find(L"当前") == std::wstring::npos,"Confirmation mode updates the current-rate text without checking a switch action.");
        check(Checked(menu,kMenuConfirm) && !Checked(menu,kMenuAuto) && !Checked(menu,kMenuManual),"Mode changes update the radio selection.");
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"Confirmation mode also requires explicit manual mode before selecting a rate.");
        check(!Checked(menu,kMenuStartup),"A disabled startup preference clears its check.");
        state.mode = Mode::manual; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && Enabled(menu,kMenu240),"Manual mode disables the current 60Hz and enables the other supported rate.");
        check(Label(menu,kMenuRates) == L"手动刷新率","Manual mode removes the prerequisite explanation.");
        state.busy = true; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"A pending operation disables duplicate manual submissions.");
        check(Enabled(menu,kMenuManual),"Mode selection remains available during an operation.");
        check(!Enabled(menu,kMenuAcTarget) && !Enabled(menu,kMenuBatteryTarget),"Switching disables target edits.");
        check(Label(menu,kMenuSummary).find(L"正在切换") != std::wstring::npos,"Busy state is explained.");
        state.busy = false; state.input->screen->dynamic_refresh = true; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"DRR blocks both manual targets.");
        check(!Checked(menu,kMenu60) && Label(menu,kMenu60).find(L"当前") != std::wstring::npos,"An unavailable current rate remains labeled without a selection check.");
        state.input->screen->dynamic_refresh = false; state.input->screen->supported_hz = {60};
        state.input->screen->nominal_hz = 120; state.input->screen->physical_hz = 120; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"Old menu actions cannot submit after a capability change.");
        {
            HMENU single = CreateTrayMenu(state);
            check(Enabled(single,kMenu60) && ActionCommand(single,TrayActionKind::manual,240) == 0,"A new single-rate menu only lists the remaining validated rate.");
            check(Label(single,kMenuRates).find(L"仅支持 60Hz") != std::wstring::npos,"A single-rate panel has a clear explanation.");
            DestroyTrayMenu(single);
        }
        state.input->screen->supported_hz = {60,240}; UpdateTrayMenu(menu,state);
        check(Enabled(menu,kMenu60) && Enabled(menu,kMenu240) && Label(menu,kMenu60).find(L"当前") == std::wstring::npos &&
            Label(menu,kMenu240).find(L"当前") == std::wstring::npos,"Another actual rate leaves both supported targets available without a false current marker.");
        state.input->availability = Availability::absent; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"An absent screen disables switching.");
        state.input->availability = Availability::ready; state.input->screen->supported_hz = {60,240}; state.input->power = PowerSource::unknown; UpdateTrayMenu(menu,state);
        check(Enabled(menu,kMenu60) && Enabled(menu,kMenu240),"Manual switching can use a known display even if power is unknown.");
        state.usable = false; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"An unavailable session disables switching.");
        state.usable = true; state.input.reset(); UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenu60) && !Enabled(menu,kMenu240),"Switching waits for the first display snapshot.");
        state.events_ready = false; UpdateTrayMenu(menu,state);
        check(!Enabled(menu,kMenuAuto) && !Enabled(menu,kMenuConfirm) && Enabled(menu,kMenuManual),"Unavailable power events leave manual mode accessible.");
        state.mode = Mode::manual; UpdateTrayMenu(menu,state);
        check(Checked(menu,kMenuManual) && !Checked(menu,kMenuAuto) && !Checked(menu,kMenuConfirm) &&
            !Checked(menu,kMenu60) && !Checked(menu,kMenu240),"Manual mode is selected only in the mode group, never in the rate actions.");
        state.events_ready = true; state.mode = Mode::automatic; state.startup = true;
        state.input = PolicyInput{PowerSource::ac,Availability::ready,Screen{L"inner|gpu|fixed",240,240.0,{60,240},false,false}};
        UpdateTrayMenu(menu,state);
        {
            auto low = state; low.mode = Mode::manual;
            low.input->screen->supported_hz = {30,48,60,240}; low.input->screen->nominal_hz = 30;
            low.input->screen->physical_hz = 60; low.input->screen->desktop_hz = 30; low.input->screen->virtual_mode_supported = true;
            HMENU virtual_menu = CreateTrayMenu(low);
            const UINT current = ActionCommand(virtual_menu,TrayActionKind::manual,30);
            check(current && !Enabled(virtual_menu,current) && Label(virtual_menu,current).find(L"当前") != std::wstring::npos,
                "The selected virtual desktop rate is marked current and disabled.");
            check(Enabled(virtual_menu,kMenu60) && Label(virtual_menu,kMenu60).find(L"当前") == std::wstring::npos,
                "The higher physical rate remains a separate selectable Windows mode.");
            check(!Checked(virtual_menu,current) && !Checked(virtual_menu,kMenu60),"Virtual-rate actions do not acquire mode checkmarks.");
            DestroyTrayMenu(virtual_menu);
        }
        {
            auto multi = state; multi.mode = Mode::manual; multi.input->screen->supported_hz = {30,48,60,90,120,144,165,240};
            multi.targets = {165,48};
            HMENU expanded = CreateTrayMenu(multi);
            for (const int hz : multi.input->screen->supported_hz) {
                const UINT manual_command = ActionCommand(expanded,TrayActionKind::manual,hz);
                check(manual_command != 0 && !Checked(expanded,manual_command),"All validated manual rates are present without checks.");
                check(Enabled(expanded,manual_command) == (hz != 240),"Only the actual current rate is disabled in manual mode.");
            }
            check(Checked(expanded,ActionCommand(expanded,TrayActionKind::ac_target,165)) &&
                Checked(expanded,ActionCommand(expanded,TrayActionKind::battery_target,48)),"Custom targets are selected only within their own groups.");
            const UINT old_action = ActionCommand(expanded,TrayActionKind::manual,90);
            multi.input->screen->supported_hz = {60,144}; UpdateTrayMenu(expanded,multi);
            check(!Enabled(expanded,old_action) && ResolveTrayAction(expanded,old_action)->hz == 90,"A frozen menu command retains its original target and becomes disabled.");
            DestroyTrayMenu(expanded);
            HMENU unavailable = CreateTrayMenu(multi);
            const UINT missing = ActionCommand(unavailable,TrayActionKind::ac_target,165);
            check(missing && Checked(unavailable,missing) && !Enabled(unavailable,missing) && Label(unavailable,missing).find(L"暂不可用") != std::wstring::npos,
                "Unavailable saved targets remain visible, selected and disabled.");
            DestroyTrayMenu(unavailable);
        }
        live_state = &state; live_menu = menu;
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = MenuOwner; cls.lpszClassName = L"RefreshRateSwitcher.MenuTest";
        RegisterClassW(&cls);
        owner = CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"RRS native menu tests",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
        check(owner != nullptr,"Create the native menu owner.");
        SetForegroundWindow(owner);
        TrayMenuRenderer renderer;
        SetWindowLongPtrW(owner,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&renderer));
        check(renderer.Attach(menu,owner,{false,false}),"Attach the shared light/dark menu renderer.");
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY),&monitor);
        const auto directory = std::filesystem::current_path()/L"menu-captures"; std::filesystem::create_directories(directory);
        bool captured = false, fits = false, light = false, dark = false, contrast = false, manual = false, idle_stable = false, theme_stable = false, hover_stable = true;
        std::thread driver([&] {
            const auto deadline = GetTickCount64()+3000; HWND window = nullptr;
            while (!(window = FindTrayMenuWindow(owner,menu)) && GetTickCount64() < deadline) Sleep(10);
            if (window) {
                Sleep(80); RECT rect{}; GetWindowRect(window,&rect);
                SendMessageW(owner,WM_ENTERIDLE,MSGF_MENU,reinterpret_cast<LPARAM>(window));
                const int initial_draws = draws;
                MENUINFO initial{}; initial.cbSize = sizeof(initial); initial.fMask = MIM_BACKGROUND; GetMenuInfo(menu,&initial);
                for (int i = 0; i < 100; ++i) SendMessageW(owner,WM_ENTERIDLE,MSGF_MENU,reinterpret_cast<LPARAM>(window));
                idle_stable = draws == initial_draws;
                for (int i = 0; i < 20; ++i) SendMessageW(owner,WM_SETTINGCHANGE,0,0);
                MENUINFO repeated = initial; GetMenuInfo(menu,&repeated);
                theme_stable = draws == initial_draws && repeated.hbrBack == initial.hbrBack;
                for (const UINT id : {kMenuAuto,kMenuConfirm,kMenuManual,kMenuStartup,kMenuLogs}) {
                    RECT row{};
                    if (!GetMenuItemRect(owner,menu,id == kMenuAuto ? 2 : id == kMenuConfirm ? 3 : id == kMenuManual ? 4 : id == kMenuStartup ? 13 : 14,&row)) { hover_stable = false; break; }
                    const int before_hover = draws;
                    SetCursorPos((row.left+row.right)/2,(row.top+row.bottom)/2); Sleep(100);
                    const int repainted_rows = draws-before_hover;
                    if (!(GetMenuState(menu,id,MF_BYCOMMAND) & MF_HILITE) || repainted_rows > 4) {
                        std::cerr << "Hover command " << id << " repainted " << repainted_rows << " rows.\n";
                        hover_stable = false;
                    }
                }
                fits = rect.left >= monitor.rcWork.left && rect.top >= monitor.rcWork.top && rect.right <= monitor.rcWork.right && rect.bottom <= monitor.rcWork.bottom;
                light = ClientPixel(window,10,10) == ColorsFor({false,false}).background && Capture(window,directory/L"light-menu.png");
                theme = 1; SendMessageW(owner,WM_SETTINGCHANGE,0,0);
                dark = ActiveMenu(owner) == menu && ClientPixel(window,10,10) == ColorsFor({true,false}).background && Capture(window,directory/L"dark-menu.png");
                theme = 2; SendMessageW(owner,WM_THEMECHANGED,0,0);
                contrast = ActiveMenu(owner) == menu && ClientPixel(window,10,10) == GetSysColor(COLOR_WINDOW) && Capture(window,directory/L"contrast-menu.png");
                theme = 1; SendMessageW(owner,WM_SYSCOLORCHANGE,0,0);
                captured = Capture(window,directory/L"unified-menu.png");
                SendMessageW(owner,WM_APP+50,0,0);
                manual = !Enabled(menu,kMenu240) && Enabled(menu,kMenu60) &&
                    Capture(window,directory/L"manual-menu.png");
                if (!SelectMenuItem(owner,kMenu60)) MenuKey(owner,VK_ESCAPE);
            } else PostMessageW(owner,WM_CANCELMODE,0,0);
        });
        const UINT selected = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,
            monitor.rcWork.right-1,monitor.rcWork.bottom-1,owner,nullptr);
        driver.join();
        if (selected != kMenu60) std::cerr << "Selected command: " << selected << '\n';
        check(selected == kMenu60,"Native arrows and Enter select the other rate after entering manual mode.");
        check(manual,"A visible manual menu keeps the current target disabled and renders the enabled target.");
        check(idle_stable,"Repeated menu hover/idle messages do not repaint the entire menu.");
        check(theme_stable,"Unchanged theme notifications preserve the background brush and cause no full-menu redraw.");
        check(hover_stable,"Moving the real mouse between menu actions repaints only the affected rows.");
        check(captured,"Capture the actual native menu.");
        check(light,"The light menu renders a light background.");
        check(dark,"An open menu switches to a dark background after a settings message.");
        check(contrast,"An open menu uses system colors for high contrast.");
        check(fits,"The native menu is constrained to the work area at the screen edge.");
        const HMENU ac_submenu = GetSubMenu(menu,6);
        const UINT configured60 = ActionCommand(menu,TrayActionKind::ac_target,60);
        bool submenu_dark = false, submenu_light = false, submenu_contrast = false, submenu_fits = false, submenu_bounds = false, submenu_idle_stable = false;
        std::thread submenu_driver([&] {
            const auto deadline = GetTickCount64()+3000;
            while (!FindTrayMenuWindow(owner,menu) && GetTickCount64() < deadline) Sleep(10);
            if (!SelectMenuItem(owner,kMenuAcTarget)) { PostMessageW(owner,WM_CANCELMODE,0,0); return; }
            HWND child = nullptr;
            while (!(child = FindTrayMenuWindow(owner,ac_submenu)) && GetTickCount64() < deadline) Sleep(10);
            if (!child) { MenuKey(owner,VK_ESCAPE); MenuKey(owner,VK_ESCAPE); return; }
            SendMessageW(owner,WM_ENTERIDLE,MSGF_MENU,reinterpret_cast<LPARAM>(child));
            const int before_idle = draws;
            for (int i = 0; i < 50; ++i) SendMessageW(owner,WM_ENTERIDLE,MSGF_MENU,reinterpret_cast<LPARAM>(child));
            submenu_idle_stable = draws == before_idle;
            RECT rect{}; GetWindowRect(child,&rect);
            const auto combined = TrayMenuBounds(owner,menu);
            submenu_bounds = combined && combined->left <= rect.left && combined->right >= rect.right && combined->top <= rect.top && combined->bottom >= rect.bottom;
            submenu_fits = rect.left >= monitor.rcWork.left && rect.top >= monitor.rcWork.top && rect.right <= monitor.rcWork.right && rect.bottom <= monitor.rcWork.bottom;
            theme = 1; SendMessageW(owner,WM_THEMECHANGED,0,0);
            const int sample_y = MulDiv(30,GetDpiForWindow(owner),96);
            Capture(child,directory/L"dark-target-submenu.png");
            submenu_dark = ClientPixel(child,10,sample_y) == ColorsFor({true,false}).background;
            theme = 0; SendMessageW(owner,WM_SETTINGCHANGE,0,0);
            Capture(child,directory/L"light-target-submenu.png");
            submenu_light = ClientPixel(child,10,sample_y) == ColorsFor({false,false}).background;
            theme = 2; SendMessageW(owner,WM_SYSCOLORCHANGE,0,0);
            Capture(child,directory/L"contrast-target-submenu.png");
            submenu_contrast = ClientPixel(child,10,sample_y) == GetSysColor(COLOR_WINDOW);
            if (!SelectMenuItem(owner,configured60,ac_submenu)) { MenuKey(owner,VK_ESCAPE); MenuKey(owner,VK_ESCAPE); }
        });
        const UINT configured = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,
            monitor.rcWork.right-1,monitor.rcWork.bottom-1,owner,nullptr);
        submenu_driver.join();
        check(configured == configured60,"Arrows and Enter select a target from the native submenu.");
        check(submenu_dark && submenu_light && submenu_contrast,"Open target submenus follow light, dark and high contrast changes.");
        check(submenu_fits,"A target submenu fits inside the monitor work area.");
        check(submenu_bounds,"Popup avoidance includes the open target submenu.");
        check(submenu_idle_stable,"Root and target submenu windows are not repainted by repeated hover/idle messages.");
        std::thread cancel([&] {
            const auto deadline = GetTickCount64()+3000;
            while (!ActiveMenu(owner) && GetTickCount64() < deadline) Sleep(10);
            MenuKey(owner,VK_ESCAPE);
        });
        const UINT cancelled = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,
            monitor.rcWork.right-1,monitor.rcWork.bottom-1,owner,nullptr);
        cancel.join(); check(cancelled == 0,"Escape cancels the native menu without selecting an action.");
        for (const wchar_t key : std::wstring(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")) {
        std::thread shortcut([&] {
            const auto deadline = GetTickCount64()+3000;
            while (!ActiveMenu(owner) && GetTickCount64() < deadline) Sleep(10);
            MenuKey(owner,key);
            Sleep(80); if (ActiveMenu(owner) == menu) MenuKey(owner,VK_ESCAPE);
        });
        const UINT accelerated = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,
            monitor.rcWork.right-1,monitor.rcWork.bottom-1,owner,nullptr);
        shortcut.join(); check(accelerated == 0,"A removed letter or numeric shortcut cannot execute any menu action.");
        MSG pending{};
        while (PeekMessageW(&pending,owner,WM_KEYFIRST,WM_KEYLAST,PM_REMOVE)) {}
        }
        live_state = nullptr; live_menu = nullptr;
        {
            auto many = state; many.mode = Mode::manual; many.input->screen->supported_hz.clear();
            for (int hz = 60; hz <= 200; ++hz) many.input->screen->supported_hz.push_back(hz);
            HMENU scrolling = CreateTrayMenu(many); check(scrolling != nullptr,"Create a menu with more rates than fit vertically.");
            TrayMenuRenderer scrolling_renderer;
            check(scrolling_renderer.Attach(scrolling,owner,{true,false}),"Attach the renderer to a scrolling native menu.");
            SetWindowLongPtrW(owner,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&scrolling_renderer));
            bool scrolling_fits = false;
            const UINT last_rate = ActionCommand(scrolling,TrayActionKind::manual,200);
            std::thread scrolling_driver([&] {
                const auto deadline = GetTickCount64()+3000;
                while (!FindTrayMenuWindow(owner,scrolling) && GetTickCount64() < deadline) Sleep(10);
                if (HWND window = FindTrayMenuWindow(owner,scrolling)) {
                    Sleep(80);
                    RECT rect{}; GetWindowRect(window,&rect);
                    scrolling_fits = rect.top >= monitor.rcWork.top && rect.bottom <= monitor.rcWork.bottom;
                    MenuKey(owner,VK_END);
                    for (int step = 0; step < 10 && !(GetMenuState(scrolling,last_rate,MF_BYCOMMAND) & MF_HILITE); ++step) MenuKey(owner,VK_UP);
                    Capture(window,directory/L"scrolling-rates-menu.png");
                    MenuKey(owner,(GetMenuState(scrolling,last_rate,MF_BYCOMMAND) & MF_HILITE) ? VK_RETURN : VK_ESCAPE);
                } else PostMessageW(owner,WM_CANCELMODE,0,0);
            });
            const UINT last_selected = TrackPopupMenuEx(scrolling,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,
                monitor.rcWork.right-1,monitor.rcWork.bottom-1,owner,nullptr);
            scrolling_driver.join();
            if (last_selected != last_rate) std::cerr << "Scrolling command: " << last_selected << "; expected: " << last_rate << '\n';
            check(last_selected == last_rate && ResolveTrayAction(scrolling,last_selected)->hz == 200,"Scrolling preserves the command mapping of the last manual rate.");
            check(scrolling_fits,"Native scrolling constrains a long menu to the work area.");
            SetWindowLongPtrW(owner,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&renderer)); DestroyTrayMenu(scrolling);
        }
        SetWindowLongPtrW(owner,GWLP_USERDATA,0);
        std::cout << "PASS: " << assertions << " unified menu assertions.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    if (menu) DestroyTrayMenu(menu); if (owner) DestroyWindow(owner);
    if (foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
    SetCursorPos(original_cursor.x,original_cursor.y);
    return result;
}
