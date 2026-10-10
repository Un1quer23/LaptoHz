#include "tray_menu.hpp"
#include "mode_hint.hpp"
#include <commctrl.h>
#include <algorithm>
#include <memory>

namespace rrs {
namespace {
struct Entry { UINT id; TrayAction action; };
struct Layout { std::vector<Entry> entries; };
Layout* MenuLayout(HMENU menu) {
    MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_MENUDATA;
    return GetMenuInfo(menu,&info) ? reinterpret_cast<Layout*>(info.dwMenuData) : nullptr;
}
bool ContainsMenu(HMENU root, HMENU candidate) {
    if (root == candidate) return true;
    for (int position = 0; position < GetMenuItemCount(root); ++position)
        if (HMENU child = GetSubMenu(root,position); child && ContainsMenu(child,candidate)) return true;
    return false;
}
thread_local HWND input_owner = nullptr;
thread_local HMENU input_menu = nullptr;
thread_local TrayMenuRenderer* input_renderer = nullptr;
constexpr UINT kPaintMenuArrows = WM_APP+51;
LRESULT CALLBACK FilterMenuInput(int code, WPARAM wparam, LPARAM lparam) {
    if (code == MSGF_MENU && input_owner && lparam && FindTrayMenuWindow(input_owner,input_menu)) {
        const auto& message = *reinterpret_cast<const MSG*>(lparam);
        if (input_renderer) input_renderer->MenuInput(message);
        if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN ||
            message.message == WM_CHAR || message.message == WM_SYSCHAR) {
            const auto key = message.wParam;
            const bool character = message.message == WM_CHAR || message.message == WM_SYSCHAR;
            if ((key >= L'0' && key <= L'9') || (key >= L'A' && key <= L'Z') ||
                (character && key >= L'a' && key <= L'z') ||
                (!character && key >= VK_NUMPAD0 && key <= VK_NUMPAD9)) return 1;
        }
    }
    return CallNextHookEx(nullptr,code,wparam,lparam);
}
void SetText(HMENU menu, UINT id, const std::wstring& text) {
    MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_STRING;
    item.dwTypeData = const_cast<wchar_t*>(text.c_str()); SetMenuItemInfoW(menu,id,FALSE,&item);
}
bool Allowed(const Decision& decision) { return decision.action == Action::apply || decision.action == Action::unchanged; }
Reason ScreenReason(const TrayMenuState& state) {
    if (!state.input) return Reason::query_failed;
    const int hz = state.input->screen && !state.input->screen->supported_hz.empty() ? state.input->screen->supported_hz.front() : 0;
    return CheckTarget(*state.input,hz).reason;
}
std::wstring TargetLabel(const TrayMenuState& state, PowerSource power) {
    const int configured = power == PowerSource::ac ? state.targets.ac : state.targets.battery;
    const auto rates = state.input && state.input->screen ? state.input->screen->supported_hz : std::vector<int>{};
    const int resolved = TargetRate(power,rates,state.targets);
    const auto value = configured ? std::to_wstring(configured)+L"Hz" : Tr(power == PowerSource::ac ? Text::highest_rate : Text::prefer_60);
    std::wstring label = Tr(Text::target_label,{Tr(power == PowerSource::ac ? Text::ac_target : Text::battery_target),value});
    if (!configured) label += resolved ? Tr(Text::resolved_rate,{std::to_wstring(resolved)}) : Tr(Text::no_rate_suffix);
    else if (!state.input || !Allowed(CheckTarget(*state.input,configured))) label += Tr(Text::unavailable_suffix);
    return label;
}
void RadioItem(HMENU menu, UINT command) {
    MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_FTYPE; item.fType = MFT_RADIOCHECK;
    SetMenuItemInfoW(menu,command,FALSE,&item);
}
}
HMENU CreateTrayMenu(const TrayMenuState& state) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    auto layout = std::make_unique<Layout>();
    const std::wstring capability = state.input ? CapabilityKey(*state.input) : L"";
    const auto version = state.input && state.input->screen ? state.input->screen->capability_version : 0;
    const auto rates = state.input && state.input->screen ? state.input->screen->supported_hz : std::vector<int>{};
    UINT next = 1000;
    AppendMenuW(menu,MF_STRING | MF_GRAYED,kMenuSummary,L""); AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,kMenuAuto,Tr(Text::mode_auto).c_str());
    AppendMenuW(menu,MF_STRING,kMenuConfirm,Tr(Text::mode_confirm).c_str());
    AppendMenuW(menu,MF_STRING,kMenuManual,Tr(Text::mode_manual).c_str());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    for (const auto source : {PowerSource::ac,PowerSource::battery}) {
        HMENU child = CreatePopupMenu();
        if (!child) { DestroyMenu(menu); return nullptr; }
        const UINT parent = source == PowerSource::ac ? kMenuAcTarget : kMenuBatteryTarget;
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(child),L"");
        MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_ID; info.wID = parent;
        SetMenuItemInfoW(menu,GetMenuItemCount(menu)-1,TRUE,&info);
        const auto kind = source == PowerSource::ac ? TrayActionKind::ac_target : TrayActionKind::battery_target;
        const UINT automatic = source == PowerSource::ac ? kMenuAcAuto : kMenuBatteryAuto;
        AppendMenuW(child,MF_STRING,automatic,Tr(source == PowerSource::ac ? Text::default_highest : Text::default_60).c_str());
        RadioItem(child,automatic); layout->entries.push_back({automatic,{kind,0,capability,version}});
        AppendMenuW(child,MF_SEPARATOR,0,nullptr);
        auto choices = rates;
        const int configured = source == PowerSource::ac ? state.targets.ac : state.targets.battery;
        if (configured && std::find(choices.begin(),choices.end(),configured) == choices.end()) choices.push_back(configured);
        std::sort(choices.begin(),choices.end());
        for (const int hz : choices) {
            const UINT id = next++;
            AppendMenuW(child,MF_STRING,id,L""); RadioItem(child,id);
            layout->entries.push_back({id,{kind,hz,capability,version}});
        }
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING | MF_GRAYED,kMenuRates,L"");
    if (rates.empty()) AppendMenuW(menu,MF_STRING | MF_GRAYED,kMenuNoRates,Tr(Text::no_rates).c_str());
    for (const int hz : rates) {
        const UINT id = hz == 60 ? kMenu60 : hz == 240 ? kMenu240 : next++;
        AppendMenuW(menu,MF_STRING,id,L""); layout->entries.push_back({id,{TrayActionKind::manual,hz,capability,version}});
    }
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,kMenuStartup,Tr(Text::startup_menu).c_str());
    AppendMenuW(menu,MF_STRING,kMenuLogs,Tr(Text::logs_menu).c_str());
    HMENU languages = CreatePopupMenu();
    if (!languages) { DestroyMenu(menu); return nullptr; }
    AppendMenuW(languages,MF_STRING,kMenuLanguageSystem,Tr(Text::language_system).c_str());
    // Native language names remain discoverable whichever language is active.
    AppendMenuW(languages,MF_STRING,kMenuLanguageChinese,L"简体中文");
    AppendMenuW(languages,MF_STRING,kMenuLanguageEnglish,L"English");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),Tr(Text::language_menu).c_str());
    MENUITEMINFOW language_info{}; language_info.cbSize = sizeof(language_info); language_info.fMask = MIIM_ID; language_info.wID = kMenuLanguage;
    SetMenuItemInfoW(menu,GetMenuItemCount(menu)-1,TRUE,&language_info);
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,kMenuExit,Tr(Text::exit_menu).c_str());
    MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_MENUDATA; info.dwMenuData = reinterpret_cast<ULONG_PTR>(layout.get());
    if (!SetMenuInfo(menu,&info)) { DestroyMenu(menu); return nullptr; }
    layout.release(); UpdateTrayMenu(menu,state); return menu;
}
void DestroyTrayMenu(HMENU menu) { if (menu) { delete MenuLayout(menu); DestroyMenu(menu); } }
std::optional<TrayAction> ResolveTrayAction(HMENU menu, UINT command) {
    if (const auto* layout = MenuLayout(menu))
        for (const auto& entry : layout->entries) if (entry.id == command) return entry.action;
    return std::nullopt;
}
void UpdateTrayMenu(HMENU menu, const TrayMenuState& state) {
    std::wstring summary = state.summary;
    const Reason reason = ScreenReason(state);
    if (state.busy) summary += L" · "+Tr(Text::switching);
    else if (!state.usable) summary += L" · "+Tr(Text::session_screen_unavailable);
    else if (reason != Reason::none) summary += L" · "+ReasonText(reason);
    SetText(menu,kMenuSummary,summary);
    std::wstring rates = Tr(Text::manual_rates);
    if (state.mode != Mode::manual) rates += L" · "+Tr(Text::select_manual);
    else if (state.busy) rates += L" · "+Tr(Text::switching);
    else if (!state.usable) rates += L" · "+Tr(Text::session_unavailable);
    else if (reason != Reason::none) rates += L" · "+ReasonText(reason);
    if (state.input && state.input->screen && state.input->screen->supported_hz.size() == 1)
        rates += L" · "+Tr(Text::only_rate,{std::to_wstring(state.input->screen->supported_hz.front())});
    SetText(menu,kMenuRates,rates);
    const auto capability = state.input ? CapabilityKey(*state.input) : L"";
    const auto version = state.input && state.input->screen ? state.input->screen->capability_version : 0;
    if (const auto* layout = MenuLayout(menu)) for (const auto& entry : layout->entries) {
        const auto& action = entry.action;
        const auto decision = state.input ? CheckTarget(*state.input,action.hz) : Decision{};
        if (action.kind == TrayActionKind::manual) {
            const bool current = state.input && state.input->screen && RateMatches(*state.input->screen,action.hz);
            const bool exact = current && state.input->screen->nominal_hz == action.hz;
            SetText(menu,entry.id,std::to_wstring(action.hz)+L"Hz"+(current ? Tr(exact ? Text::current_suffix : Text::equivalent_suffix) : L""));
            EnableMenuItem(menu,entry.id,MF_BYCOMMAND | (state.mode == Mode::manual && state.input &&
                decision.action == Action::apply && state.usable && !state.busy && capability == action.capability_key && version == action.capability_version ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(menu,entry.id,MF_BYCOMMAND | MF_UNCHECKED);
        } else {
            const int configured = action.kind == TrayActionKind::ac_target ? state.targets.ac : state.targets.battery;
            if (action.hz) SetText(menu,entry.id,std::to_wstring(action.hz)+L"Hz"+(!Allowed(decision) ? Tr(Text::unavailable_suffix) : L""));
            const bool available = !action.hz || (Allowed(decision) && capability == action.capability_key && version == action.capability_version);
            EnableMenuItem(menu,entry.id,MF_BYCOMMAND | (state.usable && !state.busy && available ? MF_ENABLED : MF_GRAYED));
            CheckMenuItem(menu,entry.id,MF_BYCOMMAND | (configured == action.hz ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    for (const auto power : {PowerSource::ac,PowerSource::battery}) {
        const UINT id = power == PowerSource::ac ? kMenuAcTarget : kMenuBatteryTarget;
        SetText(menu,id,TargetLabel(state,power));
        EnableMenuItem(menu,id,MF_BYCOMMAND | (state.usable && !state.busy ? MF_ENABLED : MF_GRAYED));
    }
    CheckMenuRadioItem(menu,kMenuAuto,kMenuManual,state.mode == Mode::automatic ? kMenuAuto :
        state.mode == Mode::confirmation ? kMenuConfirm : kMenuManual,MF_BYCOMMAND);
    for (const UINT id : {kMenuAuto,kMenuConfirm}) EnableMenuItem(menu,id,MF_BYCOMMAND | (state.events_ready ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu,kMenuStartup,MF_BYCOMMAND | (state.startup ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(menu,kMenuLanguageSystem,kMenuLanguageEnglish,
        state.language == Language::chinese ? kMenuLanguageChinese : state.language == Language::english ? kMenuLanguageEnglish : kMenuLanguageSystem,MF_BYCOMMAND);
}
HWND FindTrayMenuWindow(HWND owner, HMENU expected) {
    struct Search { HMENU expected; HWND result = nullptr; } search{expected};
    const DWORD thread = GetWindowThreadProcessId(owner,nullptr);
    GUITHREADINFO gui{}; gui.cbSize = sizeof(gui);
    if (!thread || !GetGUIThreadInfo(thread,&gui) || gui.hwndMenuOwner != owner) return nullptr;
    EnumThreadWindows(thread,[](HWND window, LPARAM parameter) -> BOOL {
        auto& item = *reinterpret_cast<Search*>(parameter);
        MENUBARINFO info{}; info.cbSize = sizeof(info);
        if (IsWindowVisible(window) && GetMenuBarInfo(window,OBJID_CLIENT,0,&info) && info.hMenu &&
            (!item.expected || item.expected == info.hMenu)) { item.result = window; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::optional<RECT> TrayMenuBounds(HWND owner, HMENU root) {
    struct Search { HMENU root; std::optional<RECT> bounds; } search{root,std::nullopt};
    if (!root || !FindTrayMenuWindow(owner,root)) return std::nullopt;
    EnumThreadWindows(GetWindowThreadProcessId(owner,nullptr),[](HWND window, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        MENUBARINFO info{}; info.cbSize = sizeof(info);
        if (IsWindowVisible(window) && GetMenuBarInfo(window,OBJID_CLIENT,0,&info) && ContainsMenu(search.root,info.hMenu)) {
            RECT rect{}; GetWindowRect(window,&rect);
            if (search.bounds) UnionRect(&*search.bounds,&*search.bounds,&rect); else search.bounds = rect;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.bounds;
}
TrayMenuRenderer::TrayMenuRenderer() = default;
TrayMenuRenderer::~TrayMenuRenderer() {
    hint_.reset();
    for (const HWND window : subclassed_windows_)
        RemoveWindowSubclass(window,MenuWindowProc,reinterpret_cast<UINT_PTR>(this));
    if (input_hook_) {
        UnhookWindowsHookEx(input_hook_); input_owner = previous_owner_; input_menu = previous_menu_; input_renderer = previous_renderer_;
    }
    if (font_) DeleteObject(font_);
    if (background_) DeleteObject(background_);
}
const TrayMenuRenderer::Row* TrayMenuRenderer::Find(ULONG_PTR data) const {
    for (const auto& row : rows_) if (reinterpret_cast<ULONG_PTR>(&row) == data) return &row;
    return nullptr;
}
bool TrayMenuRenderer::Attach(HMENU menu, HWND owner, Appearance appearance, UINT minimum_width) {
    if (menu_ || !menu || !owner || GetWindowThreadProcessId(owner,nullptr) != GetCurrentThreadId()) return false;
    menu_ = menu; owner_ = owner; dpi_ = GetDpiForWindow(owner); if (!dpi_) dpi_ = 96;
    NONCLIENTMETRICSW metrics{}; metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,dpi_)) return false;
    font_ = CreateFontIndirectW(&metrics.lfMenuFont); if (!font_) return false;
    std::vector<HMENU> menus{menu};
    for (size_t i = 0; i < menus.size(); ++i) {
        const HMENU parent = menus[i];
        for (int position = 0; position < GetMenuItemCount(parent); ++position) {
            MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
            GetMenuItemInfoW(parent,position,TRUE,&info);
            rows_.push_back({parent,info.wID,(info.fType & MFT_SEPARATOR) != 0,info.hSubMenu != nullptr,(info.fType & MFT_RADIOCHECK) != 0});
            if (info.hSubMenu) menus.push_back(info.hSubMenu);
        }
    }
    HDC dc = GetDC(owner_); const auto previous = SelectObject(dc,font_);
    for (const HMENU parent : menus) {
        int longest = 0;
        for (int position = 0; position < GetMenuItemCount(parent); ++position) {
            wchar_t text[1024]{}; GetMenuStringW(parent,position,text,1024,MF_BYPOSITION);
            SIZE extent{}; GetTextExtentPoint32W(dc,text,lstrlenW(text),&extent); longest = std::max(longest,static_cast<int>(extent.cx));
        }
        const UINT width = static_cast<UINT>(std::clamp(std::max(longest+MulDiv(64,dpi_,96),
            parent == menu_ ? static_cast<int>(minimum_width) : 0),MulDiv(parent == menu_ ? 240 : 180,dpi_,96),MulDiv(420,dpi_,96)));
        if (parent == menu_) width_ = width;
        size_t row_index = 0;
        for (auto& row : rows_) if (row.menu == parent) {
            row.width = width;
            MENUITEMINFOW info{}; info.cbSize = sizeof(info); info.fMask = MIIM_FTYPE | MIIM_DATA;
            info.fType = MFT_OWNERDRAW | (row.radio ? MFT_RADIOCHECK : 0); info.dwItemData = reinterpret_cast<ULONG_PTR>(&row);
            if (row.separator) { info.fMask |= MIIM_STATE; info.fState = MFS_DISABLED; }
            SetMenuItemInfoW(parent,static_cast<UINT>(row_index++),TRUE,&info);
        }
    }
    SelectObject(dc,previous); ReleaseDC(owner_,dc);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(owner_,MONITOR_DEFAULTTONEAREST),&monitor)) {
        MENUINFO height{}; height.cbSize = sizeof(height); height.fMask = MIM_MAXHEIGHT | MIM_APPLYTOSUBMENUS;
        // Leave room for native borders/scroll arrows rather than relying on
        // the default long-menu layout, which can overlap the taskbar.
        height.cyMax = static_cast<UINT>(std::max(1,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-MulDiv(16,dpi_,96)));
        SetMenuInfo(menu_,&height);
    }
    RefreshAppearance(appearance);
    if (!background_) return false;
    hint_ = std::make_unique<ModeHint>();
    if (!hint_->Create(owner_,menu_,font_,dpi_,appearance)) return false;
    previous_owner_ = input_owner; previous_menu_ = input_menu; previous_renderer_ = input_renderer;
    input_owner = owner_; input_menu = menu_; input_renderer = this;
    input_hook_ = SetWindowsHookExW(WH_MSGFILTER,FilterMenuInput,nullptr,GetCurrentThreadId());
    if (!input_hook_) { input_owner = previous_owner_; input_menu = previous_menu_; input_renderer = previous_renderer_; }
    return input_hook_ != nullptr;
}
void TrayMenuRenderer::RefreshAppearance(Appearance appearance) {
    const auto colors = ColorsFor(appearance);
    if (background_ && colors == colors_ && appearance == appearance_) {
        RefreshVisibleWindows(); return;
    }
    colors_ = colors; appearance_ = appearance;
    if (hint_) hint_->RefreshAppearance(appearance);
    HBRUSH brush = CreateSolidBrush(colors_.background);
    MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_BACKGROUND | MIM_STYLE | MIM_APPLYTOSUBMENUS;
    info.hbrBack = brush; info.dwStyle = MNS_NOCHECK;
    if (brush && SetMenuInfo(menu_,&info)) {
        if (background_) DeleteObject(background_); background_ = brush;
    } else if (brush) DeleteObject(brush);
    StyleVisibleWindows(true);
}
void TrayMenuRenderer::RefreshVisibleWindows() { StyleVisibleWindows(false); }
void TrayMenuRenderer::MenuSelection(HMENU menu, UINT command, UINT flags) {
    if (hint_) hint_->Selection(menu,command,flags);
    QueueArrowPaint();
}
void TrayMenuRenderer::MenuInput(const MSG& message) { if (hint_) hint_->Input(message); }
void TrayMenuRenderer::StyleVisibleWindows(bool repaint) {
    struct Paint { TrayMenuRenderer* renderer; bool repaint; std::vector<std::pair<HWND,HMENU>> visible; } paint{this,repaint,{}};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window, LPARAM parameter) -> BOOL {
        auto& paint = *reinterpret_cast<Paint*>(parameter);
        auto& renderer = *paint.renderer;
        MENUBARINFO bar{}; bar.cbSize = sizeof(bar);
        if (IsWindowVisible(window) && GetMenuBarInfo(window,OBJID_CLIENT,0,&bar) && ContainsMenu(renderer.menu_,bar.hMenu)) {
            const auto identity = std::make_pair(window,bar.hMenu);
            paint.visible.push_back(identity);
            if (paint.repaint || std::find(renderer.styled_windows_.begin(),renderer.styled_windows_.end(),identity) == renderer.styled_windows_.end()) {
                ApplyWindowAppearance(window,renderer.appearance_);
                if (SetWindowSubclass(window,MenuWindowProc,reinterpret_cast<UINT_PTR>(&renderer),reinterpret_cast<DWORD_PTR>(&renderer)) &&
                    std::find(renderer.subclassed_windows_.begin(),renderer.subclassed_windows_.end(),window) == renderer.subclassed_windows_.end())
                    renderer.subclassed_windows_.push_back(window);
                RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
            }
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&paint));
    styled_windows_ = std::move(paint.visible);
}
LRESULT CALLBACK TrayMenuRenderer::MenuWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    auto* renderer = reinterpret_cast<TrayMenuRenderer*>(data);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window,MenuWindowProc,id);
        return DefSubclassProc(window,message,wparam,lparam);
    }
    const LRESULT result = message == kPaintMenuArrows ? 0 : DefSubclassProc(window,message,wparam,lparam);
    if (message == WM_PRINTCLIENT || (message == WM_PRINT && (lparam & PRF_CLIENT))) {
        renderer->DrawSubmenuArrows(window,reinterpret_cast<HDC>(wparam));
    } else if (message == WM_PAINT || message == kPaintMenuArrows) {
        HDC dc = GetDC(window);
        if (dc) { renderer->DrawSubmenuArrows(window,dc); ReleaseDC(window,dc); }
    }
    return result;
}
void TrayMenuRenderer::QueueArrowPaint() const {
    for (const HWND window : subclassed_windows_)
        if (IsWindowVisible(window)) PostMessageW(window,kPaintMenuArrows,0,0);
}
void TrayMenuRenderer::DrawSubmenuArrows(HWND window, HDC dc) const {
    MENUBARINFO bar{}; bar.cbSize = sizeof(bar);
    if (!dc || !GetMenuBarInfo(window,OBJID_CLIENT,0,&bar) || !ContainsMenu(menu_,bar.hMenu)) return;
    const auto unit = [&](int value) { return MulDiv(value,dpi_,96); };
    const int saved = SaveDC(dc);
    for (int position = 0; position < GetMenuItemCount(bar.hMenu); ++position) {
        if (!GetSubMenu(bar.hMenu,position)) continue;
        RECT area{};
        if (!GetMenuItemRect(owner_,bar.hMenu,position,&area)) continue;
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&area),2);
        const UINT state = GetMenuState(bar.hMenu,position,MF_BYPOSITION);
        const bool disabled = (state & (MF_GRAYED | MF_DISABLED)) != 0;
        const bool selected = (state & MF_HILITE) && !disabled;
        const COLORREF background = selected ? colors_.hover : colors_.background;
        const COLORREF foreground = disabled ? colors_.disabled : selected ? colors_.hover_text : colors_.text;
        // Native popup menus paint their own arrow after WM_DRAWITEM, using
        // system menu colors. Replace that whole slot after native painting.
        RECT slot = area; slot.left = std::max(slot.left,slot.right-unit(24));
        HBRUSH brush = CreateSolidBrush(background); FillRect(dc,&slot,brush); DeleteObject(brush);
        const int x = area.right-unit(12), center = (area.top+area.bottom)/2;
        HPEN pen = CreatePen(PS_SOLID,std::max(1,unit(1)),foreground);
        const auto previous = SelectObject(dc,pen);
        MoveToEx(dc,x-unit(3),center-unit(4),nullptr); LineTo(dc,x+unit(1),center); LineTo(dc,x-unit(3),center+unit(4));
        SelectObject(dc,previous); DeleteObject(pen);
    }
    RestoreDC(dc,saved);
}
bool TrayMenuRenderer::Measure(MEASUREITEMSTRUCT& item) const {
    const Row* row = Find(item.itemData);
    if (item.CtlType != ODT_MENU || !row) return false;
    item.itemWidth = row->width; item.itemHeight = MulDiv(row->separator ? 8 : 26,dpi_,96); return true;
}
bool TrayMenuRenderer::Draw(const DRAWITEMSTRUCT& item) const {
    const Row* row = Find(item.itemData);
    if (item.CtlType != ODT_MENU || !row || reinterpret_cast<HMENU>(item.hwndItem) != row->menu) return false;
    const int saved = SaveDC(item.hDC); auto unit = [&](int value) { return MulDiv(value,dpi_,96); };
    const bool disabled = (item.itemState & ODS_DISABLED) != 0, selected = (item.itemState & ODS_SELECTED) && !disabled;
    HBRUSH brush = CreateSolidBrush(selected ? colors_.hover : colors_.background);
    FillRect(item.hDC,&item.rcItem,brush); DeleteObject(brush);
    const int center = (item.rcItem.top+item.rcItem.bottom)/2;
    if (row->separator) {
        HPEN pen = CreatePen(PS_SOLID,1,colors_.separator); SelectObject(item.hDC,pen);
        MoveToEx(item.hDC,item.rcItem.left+unit(8),center,nullptr); LineTo(item.hDC,item.rcItem.right-unit(8),center);
        RestoreDC(item.hDC,saved); DeleteObject(pen); return true;
    }
    const COLORREF text = disabled ? (row->id == kMenuSummary || row->id == kMenuRates ? colors_.secondary : colors_.disabled) : selected ? colors_.hover_text : colors_.text;
    if (item.itemState & ODS_CHECKED) {
        const int x = item.rcItem.left+unit(17);
        if (row->radio || (row->id >= kMenuAuto && row->id <= kMenuManual)) {
            HBRUSH dot = CreateSolidBrush(text); SelectObject(item.hDC,dot); SelectObject(item.hDC,GetStockObject(NULL_PEN));
            Ellipse(item.hDC,x-unit(2),center-unit(2),x+unit(2)+1,center+unit(2)+1);
            SelectObject(item.hDC,GetStockObject(NULL_BRUSH)); DeleteObject(dot);
        } else {
            HPEN pen = CreatePen(PS_SOLID,std::max(1,unit(1)),text); auto old = SelectObject(item.hDC,pen);
            MoveToEx(item.hDC,x-unit(4),center,nullptr); LineTo(item.hDC,x-unit(1),center+unit(3)); LineTo(item.hDC,x+unit(5),center-unit(4));
            SelectObject(item.hDC,old); DeleteObject(pen);
        }
    }
    wchar_t label[1024]{}; GetMenuStringW(row->menu,row->id,label,1024,MF_BYCOMMAND);
    SelectObject(item.hDC,font_); SetBkMode(item.hDC,TRANSPARENT); SetTextColor(item.hDC,text);
    RECT area = item.rcItem; area.left += unit(34); area.right -= unit(row->submenu ? 26 : 14);
    DrawTextW(item.hDC,label,-1,&area,DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(item.hDC,saved);
    if (row->submenu) QueueArrowPaint();
    return true;
}
}
