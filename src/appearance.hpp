#pragma once
#include <windows.h>

namespace rrs {
struct Appearance {
    bool dark = false, high_contrast = false;
    bool operator==(const Appearance&) const = default;
};
struct Palette {
    COLORREF background, text, secondary, disabled, hover, hover_text, pressed, separator;
    COLORREF accent, error, primary, primary_pressed, primary_text;
    COLORREF button, button_pressed, button_text, disabled_button;
    bool operator==(const Palette&) const = default;
};
Appearance ReadAppearance();
Palette ColorsFor(Appearance appearance);
void ApplyWindowAppearance(HWND window, Appearance appearance);
}
