#include "popup.hpp"
#include "render_probe.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace rrs;
namespace {
int events = 0;
PopupEvent last{PopupKind::notification,PopupAction::dismiss,0};
LRESULT CALLBACK Owner(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == kPopupAction) {
        std::unique_ptr<PopupEvent> event(reinterpret_cast<PopupEvent*>(lparam));
        last = *event; ++events; return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}
int main() {
    USEROBJECTFLAGS flags{}; DWORD length = 0;
    if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&flags,sizeof(flags),&length) || !(flags.dwFlags & WSF_VISIBLE)) {
        std::cout << "SKIP: requires an interactive Windows desktop.\n"; return 77;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = Owner; cls.lpszClassName = L"RefreshRateSwitcher.PopupTest.Owner";
    if (!RegisterClassW(&cls)) return 1;
    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"RRS popup tests",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    Popup confirmation, notice;
    const HWND foreground = GetForegroundWindow();
    int assertions = 0, result = 0;
    const auto check = [&](bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); };
    const auto pump = [&](DWORD milliseconds) {
        const ULONGLONG end = GetTickCount64()+milliseconds;
        do {
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (confirmation.Translate(message)) continue;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            Sleep(10);
        } while (GetTickCount64() < end);
    };
    const auto click = [&](Popup& popup, int control) {
        SendMessageW(GetDlgItem(popup.Window(),control),BM_CLICK,0,0); pump(30);
    };
    try {
        check(owner && confirmation.Initialize(instance,owner,PopupKind::confirmation) &&
            notice.Initialize(instance,owner),"Create independent confirmation and notification instances.");
        const auto captures = std::filesystem::current_path()/L"popup-captures";
        std::filesystem::create_directories(captures);
        const std::array<PopupButton,2> confirm_buttons{{{L"切换到 60Hz",true,true},{L"保持当前",true,false}}};
        confirmation.RefreshAppearance({false,false}); notice.RefreshAppearance({false,false});
        confirmation.ShowActions(L"是否切换刷新率？",L"已拔出电源\n当前 240Hz，建议 60Hz",confirm_buttons,42);
        check(GetForegroundWindow() == foreground,"A power confirmation does not steal foreground focus.");
        pump(6100);
        check(confirmation.Visible() && events == 0,"An unanswered confirmation persists beyond five seconds.");
        check(confirmation.Capture(captures/L"confirmation.png"),"Capture the persistent confirmation.");
        check(ClientPixel(confirmation.Window(),12,12) == ColorsFor({false,false}).background,"Confirmation renders its light background.");
        check(confirmation.Capture(captures/L"light-confirmation.png"),"Capture the light confirmation.");
        confirmation.RefreshAppearance({true,false});
        check(confirmation.Visible() && ClientPixel(confirmation.Window(),12,12) == ColorsFor({true,false}).background,"A visible confirmation changes to dark without closing.");
        check(confirmation.Capture(captures/L"dark-confirmation.png"),"Capture the dark confirmation.");
        POINT secondary{5,5}; MapWindowPoints(GetDlgItem(confirmation.Window(),302),confirmation.Window(),&secondary,1);
        check(ClientPixel(confirmation.Window(),secondary.x,secondary.y) == ColorsFor({true,false}).button,"The secondary confirmation button follows the dark theme.");
        confirmation.RefreshAppearance({false,true});
        check(ClientPixel(confirmation.Window(),12,12) == GetSysColor(COLOR_WINDOW),"High-contrast confirmation follows Windows system colors.");
        confirmation.RefreshAppearance({true,false}); notice.RefreshAppearance({true,false});
        notice.Show(L"已切换至 60Hz",L"使用电池 · 自动模式");
        notice.Avoid(confirmation.Bounds());
        pump(120);
        check(notice.Visible() && confirmation.Visible(),"Notification and confirmation have independent instances.");
        check(ClientPixel(notice.Window(),12,12) == ColorsFor({true,false}).background && notice.Capture(captures/L"dark-result.png"),"The dark result notification renders and can be captured.");
        notice.RefreshAppearance({false,false});
        check(ClientPixel(notice.Window(),12,12) == ColorsFor({false,false}).background && notice.Capture(captures/L"light-result.png"),"A visible result changes to light without restarting its countdown.");
        pump(5100);
        check(notice.Visible() && confirmation.Visible(),"The result notification remains visible beyond the former five-second duration.");
        pump(4000);
        check(!notice.Visible() && confirmation.Visible(),"A notification timer cannot close the confirmation.");
        click(confirmation,301);
        check(last.kind == PopupKind::confirmation && last.action == PopupAction::first && last.token == 42,"Approval carries the displayed request token.");
        check(confirmation.Visible(),"Approval leaves the popup available while execution is pending.");
        click(confirmation,302);
        check(last.action == PopupAction::second && last.token == 42,"Keep-current carries the request token.");
        click(confirmation,303);
        check(!confirmation.Visible() && last.action == PopupAction::dismiss,"The close cross dismisses the persistent window.");
        auto disabled = confirm_buttons; disabled[0].enabled = false;
        confirmation.ShowActions(L"是否切换刷新率？",L"内屏暂不可用",disabled,43);
        POINT primary{5,5}; MapWindowPoints(GetDlgItem(confirmation.Window(),301),confirmation.Window(),&primary,1);
        check(ClientPixel(confirmation.Window(),primary.x,primary.y) == ColorsFor({true,false}).disabled_button &&
            confirmation.Capture(captures/L"dark-disabled-confirmation.png"),"Unavailable confirmation buttons use the dark disabled palette.");
        const int before = events; click(confirmation,301);
        check(events == before && !IsWindowEnabled(GetDlgItem(confirmation.Window(),301)),"An unavailable target cannot submit an approval.");
        confirmation.ShowActions(L"是否切换刷新率？",L"已拔出电源",confirm_buttons,45);
        SetForegroundWindow(confirmation.Window());
        HWND pressed = GetDlgItem(confirmation.Window(),301);
        const int before_press = events;
        SendMessageW(pressed,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(10,10));
        confirmation.ShowActions(L"是否切换刷新率？",L"已接通电源",confirm_buttons,46);
        SendMessageW(pressed,WM_LBUTTONUP,0,MAKELPARAM(10,10)); pump(30);
        check(events == before_press,"Updating a proposal cancels a click begun against the old token.");
        SetActiveWindow(confirmation.Window()); SetFocus(pressed);
        check(GetFocus() == pressed,"A clicked confirmation supports ordinary keyboard focus.");
        PostMessageW(pressed,WM_KEYDOWN,VK_TAB,0); pump(50);
        const HWND keep = GetDlgItem(confirmation.Window(),302), close = GetDlgItem(confirmation.Window(),303);
        check(GetFocus() == keep,"Tab advances to keep-current.");
        PostMessageW(keep,WM_KEYDOWN,VK_RETURN,0); pump(50);
        check(last.action == PopupAction::second && last.token == 46,"Enter submits the focused confirmation action.");
        PostMessageW(keep,WM_KEYDOWN,VK_TAB,0); pump(50);
        check(GetFocus() == close,"Tab reaches the close cross.");
        PostMessageW(close,WM_KEYDOWN,VK_RETURN,0); pump(50);
        check(!confirmation.Visible() && last.action == PopupAction::dismiss,"Enter on the cross dismisses the confirmation.");
        confirmation.ShowActions(L"是否切换刷新率？",L"已拔出电源\n当前 240Hz，建议 60Hz",confirm_buttons,47);
        notice.Show(L"已切换至 60Hz",L"使用电池 · 手动模式"); notice.Avoid(confirmation.Bounds());
        RECT overlap{}, confirmation_rect = confirmation.Bounds(), notice_rect = notice.Bounds();
        check(!IntersectRect(&overlap,&confirmation_rect,&notice_rect),"Confirmation and result notification do not overlap.");
        MONITORINFO info{}; info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromWindow(confirmation.Window(),MONITOR_DEFAULTTONEAREST),&info);
        check(confirmation_rect.left >= info.rcWork.left && confirmation_rect.top >= info.rcWork.top &&
            confirmation_rect.right <= info.rcWork.right && confirmation_rect.bottom <= info.rcWork.bottom,"Confirmation stays inside its monitor work area.");
        check(confirmation_rect.right-confirmation_rect.left == MulDiv(360,GetDpiForWindow(confirmation.Window()),96),"Confirmation dimensions follow monitor DPI.");
        SetActiveWindow(confirmation.Window()); SetFocus(pressed);
        PostMessageW(pressed,WM_KEYDOWN,VK_ESCAPE,0); pump(50);
        check(!confirmation.Visible() && notice.Visible(),"Escape closes confirmation without closing the result notification.");
        std::cout << "PASS: " << assertions << " native popup assertions; DPI " << GetDpiForWindow(confirmation.Window()) << ".\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    confirmation.Hide(); notice.Hide(); pump(30); DestroyWindow(owner);
    if (foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
    return result;
}
