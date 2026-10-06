#pragma once
#include "appearance.hpp"
#include <commctrl.h>

namespace rrs {
inline constexpr UINT kModeHintDelayMs = 500;
HWND FindModeHintWindow(HWND owner);
class ModeHint {
public:
    ~ModeHint();
    bool Create(HWND owner, HMENU menu, HFONT font, UINT dpi, Appearance appearance);
    void Selection(HMENU menu, UINT command, UINT flags);
    void Input(const MSG& message);
    void RefreshAppearance(Appearance appearance);
    void Hide();
private:
    static void CALLBACK Delay(HWND window, UINT message, UINT_PTR timer, DWORD time);
    void Track(UINT command);
    void Show();
    bool ItemBounds(RECT& bounds) const;
    HWND window_ = nullptr, owner_ = nullptr;
    HMENU menu_ = nullptr;
    TOOLINFOW tool_{};
    UINT command_ = 0, dpi_ = 96;
    bool armed_ = false, keyboard_ = false;
    POINT last_pointer_{};
    Appearance appearance_;
    Palette colors_ = ColorsFor({});
};
}
