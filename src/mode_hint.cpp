#include "mode_hint.hpp"
#include "tray_menu.hpp"
#include <algorithm>
#include <uxtheme.h>

namespace rrs {
namespace {
constexpr wchar_t kContext[] = L"RefreshRateSwitcher.ModeHint.Context";
constexpr UINT_PTR kDelayTimer = 0x52525348;
struct Help { UINT command; const wchar_t* title; const wchar_t* text; };
constexpr Help kHelp[] = {
    {kMenuAuto,L"自动模式",L"按插电／电池目标自动切换。\n进入此模式，以及启动、唤醒时会核对刷新率。"},
    {kMenuConfirm,L"确认模式",L"插拔电源后先询问，确认才切换。\n弹窗会保持显示；选择“保持当前”或 × 可放弃本次切换。"},
    {kMenuManual,L"手动模式",L"先选此模式，再点击下方的刷新率。\n插拔电源和唤醒不会覆盖手动选择。"}
};
const Help* HelpFor(UINT command) {
    for (const auto& help : kHelp) if (help.command == command) return &help;
    return nullptr;
}
}
HWND FindModeHintWindow(HWND owner) {
    struct Search { HWND owner, result = nullptr; } search{owner};
    EnumThreadWindows(GetWindowThreadProcessId(owner,nullptr),[](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        if (GetWindow(window,GW_OWNER) == search.owner && GetPropW(window,kContext)) { search.result = window; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
ModeHint::~ModeHint() {
    if (IsWindow(window_)) { Hide(); RemovePropW(window_,kContext); DestroyWindow(window_); }
}
bool ModeHint::Create(HWND owner, HMENU menu, HFONT font, UINT dpi, Appearance appearance) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return false;
    owner_ = owner; menu_ = menu; dpi_ = dpi;
    GetCursorPos(&last_pointer_);
    RECT origin{}; GetWindowRect(owner_,&origin);
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        TOOLTIPS_CLASSW,nullptr,WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_NOANIMATE | TTS_NOFADE,
        origin.left,origin.top,0,0,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (!window_ || !SetPropW(window_,kContext,this)) return false;
    tool_.cbSize = TTTOOLINFOW_V2_SIZE; tool_.hwnd = owner_; tool_.uId = 1;
    tool_.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    tool_.lpszText = const_cast<wchar_t*>(L"");
    if (!SendMessageW(window_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool_))) return false;
    SetWindowTheme(window_,L"",L"");
    SendMessageW(window_,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
    RefreshAppearance(appearance); return true;
}
void ModeHint::Hide() {
    KillTimer(window_,kDelayTimer); armed_ = false; command_ = 0;
    if (IsWindowVisible(window_)) SendMessageW(window_,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&tool_));
}
void ModeHint::Track(UINT command) {
    if (!HelpFor(command)) { Hide(); return; }
    if (command == command_ && (armed_ || IsWindowVisible(window_))) return;
    Hide(); command_ = command;
    armed_ = SetTimer(window_,kDelayTimer,kModeHintDelayMs,Delay) != 0;
}
void ModeHint::Selection(HMENU menu, UINT command, UINT flags) {
    if (menu != menu_ || flags == 0xffff || (flags & (MF_POPUP | MF_SEPARATOR | MF_DISABLED | MF_GRAYED))) { Hide(); return; }
    if (!(flags & MF_MOUSESELECT)) keyboard_ = true;
    Track(command);
}
void ModeHint::Input(const MSG& message) {
    if (message.message == WM_MOUSEMOVE || message.message == WM_NCMOUSEMOVE) {
        POINT point{}; GetCursorPos(&point);
        // Showing or hiding a tooltip can synthesize mouse messages. Preserve
        // keyboard navigation until the pointer has actually moved.
        if (point.x == last_pointer_.x && point.y == last_pointer_.y) return;
        last_pointer_ = point; keyboard_ = false;
        const int position = MenuItemFromPoint(owner_,menu_,point);
        const UINT command = position >= 0 ? GetMenuItemID(menu_,position) : 0;
        if (position >= 0 && !(GetMenuState(menu_,command,MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED))) Track(command);
        else Hide();
    } else if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
        keyboard_ = true;
        switch (message.wParam) {
        case VK_UP: case VK_DOWN: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: break;
        default: Hide(); break;
        }
    } else if (message.message == WM_LBUTTONDOWN || message.message == WM_RBUTTONDOWN ||
               message.message == WM_MBUTTONDOWN) Hide();
}
bool ModeHint::ItemBounds(RECT& bounds) const {
    if (!HelpFor(command_) || !FindTrayMenuWindow(owner_,menu_) || !(GetMenuState(menu_,command_,MF_BYCOMMAND) & MF_HILITE)) return false;
    for (int position = 0; position < GetMenuItemCount(menu_); ++position)
        if (GetMenuItemID(menu_,position) == command_) return GetMenuItemRect(owner_,menu_,position,&bounds) != FALSE;
    return false;
}
void CALLBACK ModeHint::Delay(HWND window, UINT, UINT_PTR timer, DWORD) {
    KillTimer(window,timer);
    if (auto* hint = static_cast<ModeHint*>(GetPropW(window,kContext))) { hint->armed_ = false; hint->Show(); }
}
void ModeHint::Show() {
    RECT row{};
    if (!ItemBounds(row)) { Hide(); return; }
    POINT point{}; GetCursorPos(&point);
    if (!keyboard_ && !PtInRect(&row,point)) { Hide(); return; }
    const auto* help = HelpFor(command_);
    tool_.lpszText = const_cast<wchar_t*>(help->text);
    SendMessageW(window_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool_));
    SendMessageW(window_,TTM_SETTITLEW,TTI_NONE,reinterpret_cast<LPARAM>(help->title));
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromRect(&row,MONITOR_DEFAULTTONEAREST),&monitor)) { Hide(); return; }
    const int gap = MulDiv(8,dpi_,96);
    const int work_width = monitor.rcWork.right-monitor.rcWork.left;
    RECT menu{}; GetWindowRect(FindTrayMenuWindow(owner_,menu_),&menu);
    const int side_space = std::max(static_cast<int>(menu.left-monitor.rcWork.left),static_cast<int>(monitor.rcWork.right-menu.right))-gap;
    const int text_width = side_space >= MulDiv(100,dpi_,96) ? side_space-MulDiv(16,dpi_,96) : work_width-2*gap;
    SendMessageW(window_,TTM_SETMAXTIPWIDTH,0,std::max(1,std::min(MulDiv(300,dpi_,96),text_width)));
    const int estimated_width = std::max(1,std::min(MulDiv(300,dpi_,96),text_width))+MulDiv(16,dpi_,96);
    const int initial_x = menu.right+gap+estimated_width <= monitor.rcWork.right ? menu.right+gap : std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(menu.left)-estimated_width-gap);
    SendMessageW(window_,TTM_TRACKPOSITION,0,MAKELPARAM(initial_x,row.top));
    SendMessageW(window_,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&tool_));
    RECT measured{}; GetWindowRect(window_,&measured);
    const int width = measured.right-measured.left, height = measured.bottom-measured.top;
    if (!width || !height) { Hide(); return; }
    int x = menu.right+gap;
    if (x+width > monitor.rcWork.right) x = menu.left-width-gap;
    x = std::clamp(x,static_cast<int>(monitor.rcWork.left),std::max(static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width));
    int y = std::clamp(static_cast<int>(row.top),static_cast<int>(monitor.rcWork.top),
        std::max(static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height));
    if (x < menu.right && x+width > menu.left) {
        if (menu.top-height-gap >= monitor.rcWork.top) y = menu.top-height-gap;
        else if (menu.bottom+height+gap <= monitor.rcWork.bottom) y = menu.bottom+gap;
    }
    SendMessageW(window_,TTM_TRACKPOSITION,0,MAKELPARAM(x,y));
    // SetWindowPos also preserves full coordinates on very wide monitor layouts.
    SetWindowPos(window_,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE | SWP_NOACTIVATE);
}
void ModeHint::RefreshAppearance(Appearance appearance) {
    const auto colors = ColorsFor(appearance);
    if (colors_ == colors && appearance_ == appearance && SendMessageW(window_,TTM_GETTIPBKCOLOR,0,0) == colors.background) return;
    colors_ = colors; appearance_ = appearance;
    SendMessageW(window_,TTM_SETTIPBKCOLOR,colors.background,0);
    SendMessageW(window_,TTM_SETTIPTEXTCOLOR,colors.text,0);
    ApplyWindowAppearance(window_,appearance);
    if (IsWindowVisible(window_)) RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
}
}
