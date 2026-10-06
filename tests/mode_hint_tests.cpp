#include "mode_hint.hpp"
#include "menu_probe.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <exception>
using namespace rrs;
namespace {
std::atomic<int> theme{0};
Appearance TestAppearance() { return {theme == 1,theme == 2}; }
LRESULT CALLBACK Owner(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* renderer = reinterpret_cast<TrayMenuRenderer*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (renderer) {
        if (message == WM_MEASUREITEM && renderer->Measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) return TRUE;
        if (message == WM_DRAWITEM && renderer->Draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
        if (message == WM_MENUSELECT) { renderer->MenuSelection(reinterpret_cast<HMENU>(lparam),LOWORD(wparam),HIWORD(wparam)); return 0; }
        if (message == WM_ENTERIDLE && wparam == MSGF_MENU) { renderer->RefreshVisibleWindows(); return 0; }
        if (message == WM_SETTINGCHANGE) { renderer->RefreshAppearance(TestAppearance()); return 0; }
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
bool WaitVisible(HWND window, bool visible, int milliseconds = 2000) {
    const auto end = GetTickCount64()+milliseconds;
    do { if ((IsWindowVisible(window) != FALSE) == visible) return true; Sleep(10); } while (GetTickCount64() < end);
    return false;
}
bool Hover(HWND owner, HMENU menu, UINT command) {
    for (int position = 0; position < GetMenuItemCount(menu); ++position) {
        if (GetMenuItemID(menu,position) != command) continue;
        RECT row{};
        if (!GetMenuItemRect(owner,menu,position,&row)) return false;
        SetCursorPos((row.left+row.right)/2,(row.top+row.bottom)/2); Sleep(60);
        return (GetMenuState(menu,command,MF_BYCOMMAND) & MF_HILITE) != 0;
    }
    return false;
}
std::wstring TipText(HWND tip, HWND owner) {
    wchar_t text[1024]{}; TOOLINFOW tool{}; tool.cbSize = TTTOOLINFOW_V2_SIZE; tool.hwnd = owner; tool.uId = 1; tool.lpszText = text;
    SendMessageW(tip,TTM_GETTEXTW,1024,reinterpret_cast<LPARAM>(&tool)); return text;
}
bool Capture(HWND window, const std::filesystem::path& path) {
    RECT rect{}; GetClientRect(window,&rect);
    HDC dc = GetDC(window), buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc,rect.right,rect.bottom);
    if (!buffer || !bitmap) { if (buffer) DeleteDC(buffer); if (bitmap) DeleteObject(bitmap); ReleaseDC(window,dc); return false; }
    const auto previous = SelectObject(buffer,bitmap);
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
    USEROBJECTFLAGS station{}; DWORD bytes = 0;
    if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&station,sizeof(station),&bytes) || !(station.dwFlags & WSF_VISIBLE)) return 77;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HWND foreground = GetForegroundWindow(); POINT cursor{}; GetCursorPos(&cursor);
    HWND owner = nullptr; HMENU menu = nullptr; int assertions = 0, result = 0;
    const auto check = [&](bool condition, const char* message) { ++assertions; if (!condition) throw std::runtime_error(message); };
    try {
        WNDCLASSW cls{}; cls.lpfnWndProc = Owner; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"RefreshRateSwitcher.HintTest";
        RegisterClassW(&cls);
        owner = CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Native mode hint tests",WS_POPUP,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
        check(owner != nullptr,"Create a native menu owner.");
        TrayMenuState state; state.mode = Mode::automatic; state.summary = L"240Hz · 外部供电 · 自动模式";
        state.input = PolicyInput{PowerSource::ac,Availability::ready,Screen{L"test",240,240,{60,240},false,false}};
        menu = CreateTrayMenu(state);
        TrayMenuRenderer renderer;
        SetWindowLongPtrW(owner,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&renderer));
        check(renderer.Attach(menu,owner,{false,false}),"Create the native tracking hint with the menu.");
        HWND tip = FindModeHintWindow(owner); check(tip != nullptr,"The hint belongs to this menu owner.");
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY),&monitor);
        const auto directory = std::filesystem::current_path()/L"hint-captures"; std::filesystem::create_directories(directory);
        const auto work = monitor.rcWork;
        std::exception_ptr driver_error;
        std::thread driver([&] {
            try {
                const auto deadline = GetTickCount64()+3000;
                while (!FindTrayMenuWindow(owner,menu) && GetTickCount64() < deadline) Sleep(10);
                check(FindTrayMenuWindow(owner,menu) != nullptr,"Open the native menu.");
                const HWND active = GetForegroundWindow();
                check(Hover(owner,menu,kMenuAuto),"Mouse hover highlights automatic mode.");
                Sleep(140); check(!IsWindowVisible(tip),"A short hover does not immediately flash a hint.");
                check(WaitVisible(tip,true),"A sustained hover displays the hint inside the native menu loop.");
                check(TipText(tip,owner).find(L"目标") != std::wstring::npos && TipText(tip,owner).find(L"唤醒") != std::wstring::npos,
                    "Automatic guidance describes configurable targets and resume reconciliation.");
                check(GetForegroundWindow() == active && ActiveMenu(owner) == menu,"Showing the hint preserves focus and the open menu.");
                RECT hint{}, menu_bounds{}, intersection{}; GetWindowRect(tip,&hint); GetWindowRect(FindTrayMenuWindow(owner,menu),&menu_bounds);
                check(hint.left >= work.left && hint.right <= work.right && hint.top >= work.top && hint.bottom <= work.bottom,"The hint fits inside the monitor work area.");
                check(!IntersectRect(&intersection,&hint,&menu_bounds),"The hint appears beside the menu without covering its actions.");
                check(Capture(tip,directory/L"auto-light.png"),"Capture the light automatic hint.");
                theme = 1; SendMessageW(owner,WM_SETTINGCHANGE,0,0);
                check(IsWindowVisible(tip) && SendMessageW(tip,TTM_GETTIPBKCOLOR,0,0) == ColorsFor({true,false}).background,
                    "An open hint follows the dark menu theme.");
                check(Capture(tip,directory/L"auto-dark.png"),"Capture the dark automatic hint.");
                theme = 2; SendMessageW(owner,WM_SETTINGCHANGE,0,0);
                check(IsWindowVisible(tip) && SendMessageW(tip,TTM_GETTIPBKCOLOR,0,0) == GetSysColor(COLOR_WINDOW),"An open hint follows high contrast system colors.");
                check(Capture(tip,directory/L"auto-contrast.png"),"Capture the high contrast hint.");
                check(Hover(owner,menu,kMenuConfirm),"Mouse hover highlights confirmation mode.");
                check(!IsWindowVisible(tip) && WaitVisible(tip,true),"Changing modes hides the old hint and shows the new one after a delay.");
                check(TipText(tip,owner).find(L"先询问") != std::wstring::npos && TipText(tip,owner).find(L"保持当前") != std::wstring::npos &&
                    TipText(tip,owner).find(L"×") != std::wstring::npos,"Confirmation guidance explains confirmation and dismissing a persistent prompt.");
                check(Capture(tip,directory/L"confirm.png"),"Capture confirmation guidance.");
                check(Hover(owner,menu,kMenuManual) && WaitVisible(tip,true),"Manual mode has its own hover hint.");
                check(TipText(tip,owner).find(L"先选此模式") != std::wstring::npos && TipText(tip,owner).find(L"不会覆盖") != std::wstring::npos,
                    "Manual guidance explains the mode prerequisite and retaining the user's choice.");
                check(Capture(tip,directory/L"manual.png"),"Capture manual guidance.");
                check((GetMenuState(menu,kMenuAuto,MF_BYCOMMAND) & MF_CHECKED) && !(GetMenuState(menu,kMenuManual,MF_BYCOMMAND) & MF_CHECKED),
                    "Hovering never selects or changes the current mode.");
                check(Hover(owner,menu,kMenuLogs) && WaitVisible(tip,false,500),"Moving to another action dismisses the hint.");
                check(Hover(owner,menu,kMenuAuto),"Begin a pending hint.");
                Sleep(60); check(Hover(owner,menu,kMenuLogs),"Leave before its delay expires.");
                Sleep(kModeHintDelayMs+100); check(!IsWindowVisible(tip),"A cancelled pending hint never appears later.");
                check(Hover(owner,menu,kMenuManual) && WaitVisible(tip,true),"Show the hint before leaving the menu.");
                SetCursorPos(menu_bounds.left-10,menu_bounds.top-10);
                check(WaitVisible(tip,false,500),"Moving outside the menu dismisses the hint without closing the menu.");
                MenuKey(owner,VK_HOME);
                for (int step = 0; step < 3 && !(GetMenuState(menu,kMenuAuto,MF_BYCOMMAND) & MF_HILITE); ++step) MenuKey(owner,VK_DOWN);
                check((GetMenuState(menu,kMenuAuto,MF_BYCOMMAND) & MF_HILITE) != 0,"Keyboard navigation selects automatic mode.");
                check(WaitVisible(tip,true) && TipText(tip,owner).find(L"目标") != std::wstring::npos,"Arrow navigation can read the automatic hint while the mouse is elsewhere.");
                MenuKey(owner,VK_DOWN);
                check(WaitVisible(tip,true) && TipText(tip,owner).find(L"先询问") != std::wstring::npos,"Keyboard selection updates the guidance.");
                MenuKey(owner,VK_ESCAPE);
            } catch (...) { driver_error = std::current_exception(); PostMessageW(owner,WM_CANCELMODE,0,0); }
        });
        SetForegroundWindow(owner);
        const UINT selected = TrackPopupMenuEx(menu,TPM_RETURNCMD | TPM_NOANIMATION | TPM_WORKAREA,work.right-1,work.bottom-1,owner,nullptr);
        driver.join(); if (driver_error) std::rethrow_exception(driver_error);
        check(selected == 0 && !IsWindowVisible(tip),"Escape closes both the menu and the hint without executing an action.");
        SetWindowLongPtrW(owner,GWLP_USERDATA,0);
        std::cout << "PASS: " << assertions << " native mode hint assertions; DPI " << GetDpiForWindow(owner) << ".\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    if (menu) DestroyTrayMenu(menu); if (owner) DestroyWindow(owner);
    if (foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
    SetCursorPos(cursor.x,cursor.y); return result;
}
