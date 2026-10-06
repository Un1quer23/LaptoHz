#pragma once
#include "tray_menu.hpp"

inline HMENU ActiveMenu(HWND owner) {
    HWND window = rrs::FindTrayMenuWindow(owner);
    MENUBARINFO info{}; info.cbSize = sizeof(info);
    return window && GetMenuBarInfo(window,OBJID_CLIENT,0,&info) ? info.hMenu : nullptr;
}
inline void MenuKey(HWND owner, WPARAM key) {
    const LPARAM scan = static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC)) << 16;
    PostMessageW(owner,WM_KEYDOWN,key,scan | 1);
    PostMessageW(owner,WM_KEYUP,key,scan | 1 | (LPARAM{1} << 30) | (LPARAM{1} << 31));
    Sleep(40);
}
inline bool SelectMenuItem(HWND owner, UINT target, HMENU expected = nullptr) {
    const HMENU menu = expected ? expected : ActiveMenu(owner);
    if (!menu) return false;
    MenuKey(owner,VK_HOME);
    for (int attempt = 0; attempt < GetMenuItemCount(menu)+2; ++attempt) {
        if (expected ? !rrs::FindTrayMenuWindow(owner,expected) : ActiveMenu(owner) != menu) return false;
        if (GetMenuState(menu,target,MF_BYCOMMAND) & MF_HILITE) { MenuKey(owner,VK_RETURN); return true; }
        MenuKey(owner,VK_DOWN);
    }
    return false;
}
inline UINT ActionCommand(HMENU root, rrs::TrayActionKind kind, int hz) {
    for (int position = 0; position < GetMenuItemCount(root); ++position) {
        const UINT id = GetMenuItemID(root,position);
        if (const auto action = rrs::ResolveTrayAction(root,id); action && action->kind == kind && action->hz == hz) return id;
        if (HMENU child = GetSubMenu(root,position)) {
            for (int item = 0; item < GetMenuItemCount(child); ++item) {
                const UINT command = GetMenuItemID(child,item);
                if (const auto action = rrs::ResolveTrayAction(root,command); action && action->kind == kind && action->hz == hz) return command;
            }
        }
    }
    return 0;
}
