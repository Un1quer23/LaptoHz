#include "popup.hpp"
#include "display.hpp"
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
std::wstring RecoveryDetail(RecoveryState state) {
    DisplaySnapshot saved;
    saved.device = L"test-display";
    saved.policy = {PowerSource::ac,Availability::ready,
        Screen{L"inner|source|test-display|solo|fixed",240,240,{60,120,240},false,false,L"2560x1600|32",0,240,true}};
    saved.current.dmSize = sizeof(saved.current);
    saved.current.dmFields = DM_DISPLAYFREQUENCY | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    saved.current.dmDisplayFrequency = 240;
    saved.current.dmPelsWidth = 2560; saved.current.dmPelsHeight = 1600; saved.current.dmBitsPerPel = 32;
    saved.paths = {{L"inner",240,2560,1600,0,0,1,1,false,240}, {L"outer",60,1920,1080,2560,0,1,1,false,60}};
    saved.physical_modes_known = true;
    auto incorrect = saved;
    incorrect.policy.screen->nominal_hz = 60; incorrect.policy.screen->desktop_hz = 60;
    incorrect.current.dmDisplayFrequency = 60; incorrect.paths[0].desktop_hz = 60;
    int stage = 0;
    DisplayBackend backend({[&] { return stage ? incorrect : saved; },
        [&](const std::wstring&,const DEVMODEW&,DWORD flags) {
            if (flags == CDS_TEST) return LONG{DISP_CHANGE_SUCCESSFUL};
            return LONG{++stage == 2 && state == RecoveryState::request_failed ? DISP_CHANGE_FAILED : DISP_CHANGE_SUCCESSFUL};
        },[](DWORD) {}});
    const auto result = backend.Apply(saved,60,[] { return false; });
    if (result.success || result.recovery != state) throw std::runtime_error("The real backend must produce the expected recovery failure.");
    return result.detail;
}
bool BodyFits(Popup& popup,const std::wstring& body,PopupKind kind,UINT dpi) {
    RECT client{}; GetClientRect(popup.Window(),&client);
    const auto unit = [dpi](int value) { return MulDiv(value,dpi,96); };
    RECT area{unit(22),unit(49),client.right-unit(kind == PopupKind::notification ? 64 : 22),
        client.bottom-unit(kind == PopupKind::notification ? 12 : 62)};
    const int available = area.bottom-area.top;
    HDC dc = GetDC(popup.Window());
    HFONT font = CreateFontW(-unit(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    const auto previous = SelectObject(dc,font);
    const int required = DrawTextW(dc,body.c_str(),-1,&area,DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc,previous); DeleteObject(font); ReleaseDC(popup.Window(),dc);
    return required <= available;
}
RECT ChangeDpi(Popup& popup,UINT previous_dpi,UINT next_dpi) {
    const RECT original = popup.Bounds();
    MONITORINFO info{}; info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(popup.Window(),MONITOR_DEFAULTTONEAREST),&info);
    const int width = MulDiv(original.right-original.left,next_dpi,previous_dpi);
    const int height = MulDiv(original.bottom-original.top,next_dpi,previous_dpi);
    RECT suggested{info.rcWork.right-width-20,info.rcWork.bottom-height-20,info.rcWork.right-20,info.rcWork.bottom-20};
    SendMessageW(popup.Window(),WM_DPICHANGED,MAKEWPARAM(next_dpi,next_dpi),reinterpret_cast<LPARAM>(&suggested));
    return suggested;
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
        const RECT countdown_dpi = ChangeDpi(notice,GetDpiForWindow(notice.Window()),192);
        const RECT countdown_bounds = notice.Bounds();
        check(EqualRect(&countdown_dpi,&countdown_bounds),"A notification applies the suggested DPI bounds without activation.");
        pump(4000);
        check(!notice.Visible() && confirmation.Visible(),"DPI relayout does not restart the nine-second timer or close the confirmation.");
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
        const RECT focused_dpi = ChangeDpi(confirmation,GetDpiForWindow(confirmation.Window()),144);
        const RECT focused_bounds = confirmation.Bounds();
        check(EqualRect(&focused_dpi,&focused_bounds) && GetFocus() == pressed && confirmation.Visible(),
            "DPI relayout applies the confirmation bounds while preserving focus and visibility.");
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
        for (const auto recovery : {RecoveryState::request_failed,RecoveryState::verification_failed}) {
            const auto detail = RecoveryDetail(recovery);
            const std::wstring prefix = recovery == RecoveryState::request_failed ? L"request-failed" : L"verification-failed";
            check(detail.find(L"信号 240Hz") != std::wstring::npos,"The backend error includes the final observed physical frequency.");
            for (const bool unsaved : {false,true}) {
                const std::wstring result_body = (unsaved ? L"设置未保存，请确认配置目录可写\n" : L"")+detail;
                const std::wstring confirmation_body = L"已拔出电源\n当前 240Hz，建议 60Hz\n"+result_body;
                notice.Show(L"刷新率切换未完成",result_body,true);
                confirmation.ShowActions(L"刷新率切换未完成",confirmation_body,confirm_buttons,48,L"",true);
                UINT previous_dpi = GetDpiForWindow(notice.Window());
                const std::wstring name = prefix+(unsaved ? L"-unsaved" : L"");
                check(notice.Capture(captures/(name+L"-native-notice.png")) &&
                    confirmation.Capture(captures/(name+L"-native-confirmation.png")),"Capture the real backend error at the current monitor DPI.");
                for (const UINT dpi : {previous_dpi,144u,168u,192u,96u}) {
                    ChangeDpi(notice,previous_dpi,dpi); ChangeDpi(confirmation,previous_dpi,dpi);
                    check(BodyFits(notice,result_body,PopupKind::notification,dpi),"The notification fits the complete backend error, including unsaved settings, at each DPI.");
                    check(BodyFits(confirmation,confirmation_body,PopupKind::confirmation,dpi),"The confirmation fits the complete backend error above its actions at each DPI.");
                    LOGFONTW font{};
                    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(confirmation.Window(),301),WM_GETFONT,0,0)),sizeof(font),&font);
                    check(font.lfHeight == -MulDiv(13,dpi,96),"Confirmation action fonts follow the new DPI, including a smaller DPI.");
                    previous_dpi = dpi;
                }
                check(notice.Capture(captures/(name+L"-scaled-notice.png")) &&
                    confirmation.Capture(captures/(name+L"-scaled-confirmation.png")),
                    "Capture complete recovery errors after repeated DPI changes.");
            }
        }
        const std::wstring boundary_body = L"已拔出电源\n当前 240Hz，建议 60Hz\n设置未保存，请确认配置目录可写\n"+
            RecoveryDetail(RecoveryState::request_failed);
        confirmation.ShowActions(L"刷新率切换未完成",boundary_body,confirm_buttons,48,L"",true);
        MONITORINFO work{}; work.cbSize = sizeof(work);
        GetMonitorInfoW(MonitorFromWindow(confirmation.Window(),MONITOR_DEFAULTTONEAREST),&work);
        const UINT native_dpi = GetDpiForWindow(confirmation.Window());
        RECT narrow{work.rcWork.right-MulDiv(260,native_dpi,96),work.rcWork.bottom-MulDiv(70,native_dpi,96),work.rcWork.right,work.rcWork.bottom};
        SendMessageW(confirmation.Window(),WM_DPICHANGED,MAKEWPARAM(native_dpi,native_dpi),reinterpret_cast<LPARAM>(&narrow));
        RECT fitted = confirmation.Bounds();
        check(BodyFits(confirmation,boundary_body,PopupKind::confirmation,native_dpi) &&
            fitted.bottom-fitted.top > narrow.bottom-narrow.top,"A narrow suggested rectangle grows to fit all error details.");
        check(fitted.left >= work.rcWork.left && fitted.top >= work.rcWork.top &&
            fitted.right <= work.rcWork.right && fitted.bottom <= work.rcWork.bottom,"An expanded error popup is clamped to the work area.");
        RECT oversized{work.rcWork.left-200,work.rcWork.top-200,work.rcWork.right+200,work.rcWork.bottom+200};
        SendMessageW(confirmation.Window(),WM_DPICHANGED,MAKEWPARAM(native_dpi,native_dpi),reinterpret_cast<LPARAM>(&oversized));
        fitted = confirmation.Bounds();
        check(fitted.left >= work.rcWork.left && fitted.top >= work.rcWork.top && fitted.right <= work.rcWork.right &&
            fitted.bottom <= work.rcWork.bottom && BodyFits(confirmation,boundary_body,PopupKind::confirmation,native_dpi),
            "Oversized DPI suggestions stay within the work area and retain the complete error.");
        confirmation.ShowActions(L"是否切换刷新率？",L"当前 240Hz，建议 60Hz",confirm_buttons,49);
        notice.Show(L"已切换至 60Hz",L"使用电池 · 自动模式");
        const RECT short_confirmation = confirmation.Bounds(), short_notice = notice.Bounds();
        const UINT dpi = GetDpiForWindow(confirmation.Window());
        check(short_confirmation.bottom-short_confirmation.top == MulDiv(196,dpi,96) &&
            short_notice.bottom-short_notice.top == MulDiv(112,dpi,96),"Short messages return to the existing compact popup heights.");
        notice.Hide();
        const RECT hidden_dpi = ChangeDpi(notice,GetDpiForWindow(notice.Window()),144);
        const RECT hidden_bounds = notice.Bounds();
        check(EqualRect(&hidden_dpi,&hidden_bounds) && !notice.Visible(),"A DPI message does not show a hidden notification.");
        std::cout << "PASS: " << assertions << " native popup assertions; DPI " << GetDpiForWindow(confirmation.Window()) << ".\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    confirmation.Hide(); notice.Hide(); pump(30); DestroyWindow(owner);
    if (foreground && IsWindow(foreground)) SetForegroundWindow(foreground);
    return result;
}
