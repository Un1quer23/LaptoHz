#include "popup.hpp"
#include "localization.hpp"
#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <memory>

namespace rrs {
namespace {
constexpr wchar_t kPopupClass[] = L"RefreshRateSwitcher.Notification.v1";
constexpr int kFirstButton = 301, kSecondButton = 302, kCloseButton = 303;
HFONT PopupFont(UINT dpi, int size, int weight = FW_NORMAL) {
    return CreateFontW(-MulDiv(size,dpi,96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,
        CurrentLanguage() == Language::english ? L"Segoe UI" : L"Microsoft YaHei UI");
}
RECT BodyRect(const RECT& client, PopupKind kind, UINT dpi) {
    const auto unit = [dpi](int value) { return MulDiv(value,dpi,96); };
    return RECT{unit(22),unit(49),client.right-unit(kind == PopupKind::notification ? 64 : 22),
        client.bottom-unit(kind == PopupKind::notification ? 12 : 62)};
}
POINT FitPosition(int x, int y, SIZE size, const RECT& work) {
    return POINT{std::clamp(x,static_cast<int>(work.left),static_cast<int>(std::max(work.left,work.right-size.cx))),
        std::clamp(y,static_cast<int>(work.top),static_cast<int>(std::max(work.top,work.bottom-size.cy)))};
}
struct MonitorLookup { const std::wstring* device; HMONITOR monitor = nullptr; };
BOOL CALLBACK FindMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    auto* lookup = reinterpret_cast<MonitorLookup*>(parameter);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) && *lookup->device == info.szDevice) { lookup->monitor = monitor; return FALSE; }
    return TRUE;
}
}
Popup::~Popup() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
    if (button_font_) DeleteObject(button_font_);
}
bool Popup::Initialize(HINSTANCE instance, HWND owner, PopupKind kind) {
    owner_ = owner; kind_ = kind;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.lpfnWndProc = Procedure;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.lpszClassName = kPopupClass;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto caption = Tr(kind == PopupKind::notification ? Text::notification_caption : Text::confirmation_caption);
    const DWORD ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (kind == PopupKind::notification ? WS_EX_NOACTIVATE : 0);
    hwnd_ = CreateWindowExW(ex_style, kPopupClass, caption.c_str(),
        WS_POPUP | WS_CLIPCHILDREN, 0, 0, 360, 112, owner_, nullptr, instance, this);
    if (hwnd_) dpi_ = GetDpiForWindow(hwnd_);
    if (hwnd_) { const DWORD preference = 2; DwmSetWindowAttribute(hwnd_, static_cast<DWMWINDOWATTRIBUTE>(33), &preference, sizeof(preference)); }
    if (hwnd_ && kind_ != PopupKind::notification) {
        for (size_t i = 0; i < controls_.size(); ++i) {
            controls_[i] = CreateWindowExW(0, L"BUTTON", i == 2 ? Tr(Text::close).c_str() : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstButton + i)), instance, nullptr);
            if (!controls_[i]) return false;
        }
    }
    RefreshAppearance(ReadAppearance());
    return hwnd_ != nullptr;
}
void Popup::RefreshLanguage() {
    if (!hwnd_) return;
    SetWindowTextW(hwnd_,Tr(kind_ == PopupKind::notification ? Text::notification_caption : Text::confirmation_caption).c_str());
    if (controls_[2]) SetWindowTextW(controls_[2],Tr(Text::close).c_str());
}
void Popup::RefreshAppearance(Appearance appearance) {
    colors_ = ColorsFor(appearance); ApplyWindowAppearance(hwnd_,appearance);
    if (Visible()) RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
}
void Popup::Show(const std::wstring& title, const std::wstring& body, bool error, const std::wstring& device) {
    if (!hwnd_ || kind_ != PopupKind::notification) return;
    title_ = title; body_ = body; error_ = error;
    close_hovered_ = false; close_pressed_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    deadline_ = GetTickCount64() + 9000;
    Position(device);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
    ScheduleCountdownTick();
}
void Popup::Position(const std::wstring& device) {
    MonitorLookup lookup{&device};
    if (!device.empty()) EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&lookup));
    HMONITOR monitor = lookup.monitor ? lookup.monitor : MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info); GetMonitorInfoW(monitor, &info);
    // Moving first lets Windows report the destination monitor's DPI. Its
    // intermediate suggested rectangle is superseded by the content layout.
    positioning_ = true;
    SetWindowPos(hwnd_, HWND_TOPMOST, info.rcWork.right - 2, info.rcWork.bottom - 2, 1, 1, SWP_NOACTIVATE);
    dpi_ = GetDpiForWindow(hwnd_);
    const SIZE size = ContentSize(MulDiv(360,dpi_,96),MulDiv(kind_ == PopupKind::notification ? 112 : 196,dpi_,96),info.rcWork);
    const int margin = MulDiv(16,dpi_,96);
    const POINT position = FitPosition(info.rcWork.right-size.cx-margin,info.rcWork.bottom-size.cy-margin,size,info.rcWork);
    SetWindowPos(hwnd_, HWND_TOPMOST, position.x, position.y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    positioning_ = false;
    LayoutButtons();
}
SIZE Popup::ContentSize(int width, int minimum_height, const RECT& work) const {
    const int margin = MulDiv(16,dpi_,96);
    const int maximum_width = std::max(1,static_cast<int>(work.right-work.left)-2*margin);
    const int maximum_height = std::max(1,static_cast<int>(work.bottom-work.top)-2*margin);
    width = std::clamp(width,1,maximum_width);
    HDC dc = GetDC(hwnd_);
    HFONT title_font = PopupFont(dpi_,18,FW_SEMIBOLD);
    const auto original = SelectObject(dc,title_font);
    SIZE title_size{}; GetTextExtentPoint32W(dc,title_.c_str(),static_cast<int>(title_.size()),&title_size);
    // Translated titles and actions can be wider than their Chinese equivalents.
    width = std::min(maximum_width,std::max(width,static_cast<int>(title_size.cx)+MulDiv(70,dpi_,96)));
    HFONT font = PopupFont(dpi_,13);
    SelectObject(dc,font);
    if (kind_ == PopupKind::confirmation) {
        int longest = 0;
        for (const auto& button : buttons_) {
            SIZE size{}; GetTextExtentPoint32W(dc,button.text.c_str(),static_cast<int>(button.text.size()),&size);
            longest = std::max(longest,static_cast<int>(size.cx));
        }
        width = std::min(maximum_width,std::max(width,2*longest+MulDiv(88,dpi_,96)));
    }
    const auto measure = [&] {
        const RECT client{0,0,width,0};
        RECT body = BodyRect(client,kind_,dpi_);
        const int padding = body.top-body.bottom;
        // Measure with exactly the font, width and wrapping flags used to paint.
        const int text_height = DrawTextW(dc,body_.c_str(),-1,&body,DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
        return text_height+padding;
    };
    int height = measure();
    if (height > maximum_height && width < maximum_width) { width = maximum_width; height = measure(); }
    SelectObject(dc,original); DeleteObject(font); DeleteObject(title_font); ReleaseDC(hwnd_,dc);
    return SIZE{width,std::min(maximum_height,std::max(minimum_height,height))};
}
void Popup::ChangeDpi(UINT dpi, const RECT& suggested) {
    if (dpi) dpi_ = dpi;
    if (positioning_) return;
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromRect(&suggested,MONITOR_DEFAULTTONEAREST),&info)) return;
    const SIZE size = ContentSize(suggested.right-suggested.left,suggested.bottom-suggested.top,info.rcWork);
    const POINT position = FitPosition(suggested.left,suggested.top,size,info.rcWork);
    SetWindowPos(hwnd_,nullptr,position.x,position.y,size.cx,size.cy,SWP_NOZORDER | SWP_NOACTIVATE);
    LayoutButtons();
    RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE | RDW_ALLCHILDREN);
}
void Popup::ShowActions(const std::wstring& title, const std::wstring& body, const std::array<PopupButton, 2>& buttons,
    std::uint64_t token, const std::wstring& device, bool error) {
    if (!hwnd_ || kind_ == PopupKind::notification) return;
    if (token_ != token) {
        // Cancel a press begun against the previous proposal/mode before relabeling.
        for (HWND control : controls_) SendMessageW(control, WM_CANCELMODE, 0, 0);
    }
    title_ = title; body_ = body; buttons_ = buttons; token_ = token; error_ = error;
    for (size_t i = 0; i < buttons_.size(); ++i) {
        SetWindowTextW(controls_[i], buttons_[i].text.c_str()); EnableWindow(controls_[i], buttons_[i].enabled);
    }
    Position(device);
    InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
    for (HWND control : controls_) { InvalidateRect(control, nullptr, TRUE); UpdateWindow(control); }
}
void Popup::Hide() {
    if (!hwnd_) return;
    KillTimer(hwnd_, 1);
    deadline_ = 0;
    close_hovered_ = false; close_pressed_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    const bool was_visible = IsWindowVisible(hwnd_) != FALSE;
    ShowWindow(hwnd_, SW_HIDE);
    if (was_visible && owner_) PostMessageW(owner_, kPopupClosed, static_cast<WPARAM>(kind_), 0);
}
void Popup::Emit(PopupAction action) {
    auto event = std::make_unique<PopupEvent>(PopupEvent{kind_, action, token_});
    if (PostMessageW(owner_, kPopupAction, 0, reinterpret_cast<LPARAM>(event.get()))) event.release();
}
void Popup::Dismiss() { if (kind_ != PopupKind::notification) Emit(PopupAction::dismiss); Hide(); }
bool Popup::Translate(MSG& message) {
    if (kind_ == PopupKind::notification || !Visible() || (message.hwnd != hwnd_ && !IsChild(hwnd_, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) { Dismiss(); return true; }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
        const HWND focus = GetFocus();
        for (HWND control : controls_) if (control == focus && IsWindowEnabled(control)) {
            SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED), reinterpret_cast<LPARAM>(control)); return true;
        }
    }
    return IsDialogMessageW(hwnd_, &message) != FALSE;
}
RECT Popup::Bounds() const { RECT rect{}; if (hwnd_) GetWindowRect(hwnd_, &rect); return rect; }
void Popup::Avoid(const RECT& occupied) {
    if (!Visible()) return;
    const RECT own = Bounds(); RECT overlap{}; if (!IntersectRect(&overlap, &own, &occupied)) return;
    MONITORINFO info{}; info.cbSize = sizeof(info); GetMonitorInfoW(MonitorFromRect(&own, MONITOR_DEFAULTTONEAREST), &info);
    const int gap = MulDiv(16, dpi_, 96), height = own.bottom-own.top;
    const int top = occupied.top-height-gap;
    const int y = top >= info.rcWork.top ? top : std::min(static_cast<int>(occupied.bottom)+gap, static_cast<int>(info.rcWork.bottom)-height);
    SetWindowPos(hwnd_, nullptr, own.left, std::max(static_cast<int>(info.rcWork.top), y), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
void Popup::LayoutButtons() {
    if (kind_ == PopupKind::notification || !controls_[0]) return;
    const UINT dpi = dpi_; auto unit = [dpi](int value) { return MulDiv(value,dpi,96); };
    RECT rect{}; GetClientRect(hwnd_, &rect);
    const int width = (rect.right-unit(56))/2, y = rect.bottom-unit(52);
    SetWindowPos(controls_[0], nullptr, unit(22), y, width, unit(34), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(controls_[1], nullptr, unit(34)+width, y, width, unit(34), SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT close = CloseButtonRect();
    SetWindowPos(controls_[2], nullptr, close.left, close.top, close.right-close.left, close.bottom-close.top, SWP_NOZORDER | SWP_NOACTIVATE);
    HFONT font = PopupFont(dpi,13);
    for (HWND control : controls_) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    if (button_font_) DeleteObject(button_font_); button_font_ = font;
}
RECT Popup::CloseButtonRect() const {
    RECT rect{}; GetClientRect(hwnd_, &rect);
    const UINT dpi = dpi_;
    auto unit = [dpi](int value) { return MulDiv(value, dpi, 96); };
    return RECT{rect.right-unit(40), unit(12), rect.right-unit(12), unit(40)};
}
void Popup::ScheduleCountdownTick() {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline_) { Hide(); return; }
    // Align each redraw with the next whole second of the monotonic deadline.
    SetTimer(hwnd_, 1, static_cast<UINT>((deadline_ - now - 1) % 1000 + 1), nullptr);
}
LRESULT CALLBACK Popup::Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Popup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Popup*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_PAINT: self->Paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: if (lparam) self->DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)); return TRUE;
    case WM_MOUSEACTIVATE: return self->kind_ == PopupKind::notification ? MA_NOACTIVATE : MA_ACTIVATE;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            const int id = LOWORD(wparam);
            if (id == kCloseButton) self->Dismiss();
            else if (id == kFirstButton || id == kSecondButton) {
                const size_t index = static_cast<size_t>(id-kFirstButton);
                if (self->buttons_[index].enabled) self->Emit(index ? PopupAction::second : PopupAction::first);
            }
        }
        return 0;
    case DM_GETDEFID: return MAKELONG(kFirstButton, DC_HASDEFID);
    case DM_SETDEFID: return 0;
    case WM_MOUSEMOVE: {
        if (self->kind_ != PopupKind::notification) break;
        const RECT button = self->CloseButtonRect();
        const POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const bool hovered = PtInRect(&button, position) != FALSE;
        if (hovered != self->close_hovered_) {
            self->close_hovered_ = hovered; InvalidateRect(hwnd, &button, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        return 0;
    }
    case WM_MOUSELEAVE: {
        self->close_hovered_ = false;
        const RECT button = self->CloseButtonRect(); InvalidateRect(hwnd, &button, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (self->kind_ != PopupKind::notification) break;
        const RECT button = self->CloseButtonRect();
        const POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (PtInRect(&button, position)) {
            self->close_pressed_ = true; self->close_hovered_ = true;
            SetCapture(hwnd); InvalidateRect(hwnd, &button, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (self->kind_ != PopupKind::notification) break;
        const RECT button = self->CloseButtonRect();
        const POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const bool dismiss = self->close_pressed_ && PtInRect(&button, position);
        self->close_pressed_ = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (dismiss) self->Hide();
        else InvalidateRect(hwnd, &button, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED: case WM_CANCELMODE: {
        self->close_pressed_ = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        const RECT button = self->CloseButtonRect(); InvalidateRect(hwnd, &button, FALSE);
        return 0;
    }
    case WM_CLOSE: self->Dismiss(); return 0;
    case WM_TIMER:
        if (wparam == 1 && self->kind_ == PopupKind::notification && self->Visible()) {
            if (GetTickCount64() >= self->deadline_) self->Hide();
            else { InvalidateRect(hwnd, nullptr, FALSE); self->ScheduleCountdownTick(); }
        }
        return 0;
    case WM_DPICHANGED:
        if (lparam) self->ChangeDpi(HIWORD(wparam),*reinterpret_cast<const RECT*>(lparam));
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
void Popup::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd_, &paint);
    RECT rect{}; GetClientRect(hwnd_, &rect);
    const UINT dpi = dpi_;
    auto unit = [dpi](int value) { return MulDiv(value, dpi, 96); };
    HBRUSH background = CreateSolidBrush(colors_.background);
    FillRect(dc, &rect, background); DeleteObject(background);
    HBRUSH accent = CreateSolidBrush(error_ ? colors_.error : colors_.accent);
    RECT bar{0,0,unit(5),rect.bottom}; FillRect(dc, &bar, accent); DeleteObject(accent);
    SetBkMode(dc, TRANSPARENT);
    HFONT title_font = PopupFont(dpi,18,FW_SEMIBOLD);
    HFONT body_font = PopupFont(dpi,13);
    HGDIOBJ old = SelectObject(dc, title_font);
    SetTextColor(dc, colors_.text);
    const RECT close_rect = CloseButtonRect();
    RECT title_rect{unit(22),unit(17),close_rect.left-unit(8),unit(44)};
    DrawTextW(dc, title_.c_str(), -1, &title_rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, body_font); SetTextColor(dc, colors_.secondary);
    RECT body_rect = BodyRect(rect,kind_,dpi);
    DrawTextW(dc, body_.c_str(), -1, &body_rect, DT_WORDBREAK | DT_NOPREFIX);
    if (kind_ == PopupKind::notification) {
    const ULONGLONG now = GetTickCount64();
    const auto seconds = deadline_ > now ? (deadline_ - now + 999) / 1000 : 1;
    const std::wstring countdown = std::to_wstring(seconds) + L"s";
    RECT countdown_rect{rect.right-unit(56),rect.bottom-unit(35),rect.right-unit(20),rect.bottom-unit(12)};
    SetTextColor(dc, colors_.secondary);
    DrawTextW(dc, countdown.c_str(), -1, &countdown_rect, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
    if (close_hovered_) {
        HBRUSH hover = CreateSolidBrush(close_pressed_ ? colors_.pressed : colors_.hover);
        FillRect(dc, &close_rect, hover); DeleteObject(hover);
    }
    HPEN cross = CreatePen(PS_SOLID, std::max(1, unit(2)), close_hovered_ ? colors_.hover_text : colors_.secondary);
    HGDIOBJ old_pen = SelectObject(dc, cross);
    const int inset = unit(9);
    MoveToEx(dc, close_rect.left+inset, close_rect.top+inset, nullptr);
    LineTo(dc, close_rect.right-inset, close_rect.bottom-inset);
    MoveToEx(dc, close_rect.right-inset, close_rect.top+inset, nullptr);
    LineTo(dc, close_rect.left+inset, close_rect.bottom-inset);
    SelectObject(dc, old_pen); DeleteObject(cross);
    }
    SelectObject(dc, old); DeleteObject(title_font); DeleteObject(body_font);
    EndPaint(hwnd_, &paint);
}
void Popup::DrawButton(const DRAWITEMSTRUCT& item) {
    if (item.CtlType != ODT_BUTTON) return;
    const bool close = item.CtlID == kCloseButton;
    const size_t index = item.CtlID == kSecondButton ? 1 : 0;
    const bool enabled = (item.itemState & ODS_DISABLED) == 0, pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = !close && buttons_[index].primary;
    const COLORREF color = close ? (pressed ? colors_.pressed : colors_.background) : !enabled ? colors_.disabled_button :
        primary ? (pressed ? colors_.primary_pressed : colors_.primary) : (pressed ? colors_.button_pressed : colors_.button);
    HBRUSH brush = CreateSolidBrush(color); FillRect(item.hDC, &item.rcItem, brush); DeleteObject(brush);
    if (close) {
        const UINT dpi = dpi_; const int inset = MulDiv(9,dpi,96);
        HPEN pen = CreatePen(PS_SOLID,std::max(1,MulDiv(2,dpi,96)),pressed ? colors_.hover_text : colors_.secondary); HGDIOBJ old = SelectObject(item.hDC, pen);
        MoveToEx(item.hDC,item.rcItem.left+inset,item.rcItem.top+inset,nullptr); LineTo(item.hDC,item.rcItem.right-inset,item.rcItem.bottom-inset);
        MoveToEx(item.hDC,item.rcItem.right-inset,item.rcItem.top+inset,nullptr); LineTo(item.hDC,item.rcItem.left+inset,item.rcItem.bottom-inset);
        SelectObject(item.hDC,old); DeleteObject(pen);
    } else {
        SetBkMode(item.hDC,TRANSPARENT); SetTextColor(item.hDC,!enabled ? colors_.disabled : primary ? colors_.primary_text : pressed ? colors_.hover_text : colors_.button_text);
        HGDIOBJ old = SelectObject(item.hDC,button_font_); RECT text = item.rcItem;
        DrawTextW(item.hDC,buttons_[index].text.c_str(),-1,&text,DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(item.hDC,old);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = item.rcItem; InflateRect(&focus,-3,-3); DrawFocusRect(item.hDC,&focus);
    }
}
bool Popup::Capture(const std::filesystem::path& path) const {
    if (!Visible()) return false;
    RECT rect{}; GetClientRect(hwnd_, &rect);
    HDC window_dc = GetDC(hwnd_);
    HDC memory_dc = CreateCompatibleDC(window_dc);
    HBITMAP image = CreateCompatibleBitmap(window_dc, rect.right, rect.bottom);
    if (!memory_dc || !image) {
        if (image) DeleteObject(image); if (memory_dc) DeleteDC(memory_dc); ReleaseDC(hwnd_, window_dc); return false;
    }
    HGDIOBJ previous = SelectObject(memory_dc, image);
    const BOOL copied = PrintWindow(hwnd_, memory_dc, PW_CLIENTONLY);
    SelectObject(memory_dc, previous);
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    bool success = false;
    if (copied && Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
        {
            Gdiplus::Bitmap bitmap(image, nullptr);
            const CLSID png_encoder{0x557cf406, 0x1a04, 0x11d3, {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            success = bitmap.Save(path.c_str(), &png_encoder, nullptr) == Gdiplus::Ok;
        }
        Gdiplus::GdiplusShutdown(token);
    }
    DeleteObject(image); DeleteDC(memory_dc); ReleaseDC(hwnd_, window_dc);
    return success;
}
}
