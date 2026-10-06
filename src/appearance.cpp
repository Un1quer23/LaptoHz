#include "appearance.hpp"
#include <dwmapi.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <iterator>

namespace rrs {
namespace {
// Public IUISettings3 ABI: only GetColorValue is used. A small declaration avoids
// the conflicting generated Foundation templates in portable MinGW headers.
struct UiColor { BYTE A, R, G, B; };
struct UiSettings3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetColorValue(INT32 type, UiColor* color) = 0;
};
constexpr GUID kUiSettings3{0x03021be4,0x5254,0x4781,{0x81,0x94,0x51,0x68,0xf7,0xd0,0x6d,0x7b}};
static_assert(sizeof(UiColor) == 4);
}
Appearance ReadAppearance() {
    HIGHCONTRASTW contrast{}; contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags & HCF_HIGHCONTRASTON)) return {false,true};
    bool dark = false, resolved = false;
    const HRESULT apartment = RoInitialize(RO_INIT_SINGLETHREADED);
    if (SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE) {
        HSTRING name = nullptr;
        constexpr wchar_t type[] = L"Windows.UI.ViewManagement.UISettings";
        if (SUCCEEDED(WindowsCreateString(type,static_cast<UINT32>(std::size(type)-1),&name))) {
            IInspectable* object = nullptr;
            if (SUCCEEDED(RoActivateInstance(name,&object))) {
                UiSettings3* settings = nullptr;
                if (SUCCEEDED(object->QueryInterface(kUiSettings3,reinterpret_cast<void**>(&settings)))) {
                    UiColor foreground{};
                    if (SUCCEEDED(settings->GetColorValue(1,&foreground))) {
                        dark = 5*foreground.G + 2*foreground.R + foreground.B > 8*128; resolved = true;
                    }
                    settings->Release();
                }
                object->Release();
            }
            WindowsDeleteString(name);
        }
        if (SUCCEEDED(apartment)) RoUninitialize();
    }
    // A failed UISettings activation must not prevent opening the tray menu.
    if (!resolved) {
        DWORD light = 1, bytes = sizeof(light);
        if (RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes) == ERROR_SUCCESS) dark = light == 0;
    }
    return {dark,false};
}
Palette ColorsFor(Appearance appearance) {
    if (appearance.high_contrast) {
        const auto background = GetSysColor(COLOR_WINDOW), text = GetSysColor(COLOR_WINDOWTEXT);
        const auto selected = GetSysColor(COLOR_HIGHLIGHT), selected_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
        return {background,text,text,GetSysColor(COLOR_GRAYTEXT),selected,selected_text,selected,text,
            selected,selected,selected,selected,selected_text,GetSysColor(COLOR_BTNFACE),selected,
            GetSysColor(COLOR_BTNTEXT),GetSysColor(COLOR_BTNFACE)};
    }
    if (appearance.dark) return {RGB(32,32,32),RGB(242,242,242),RGB(190,190,190),RGB(145,145,145),
        RGB(49,49,49),RGB(242,242,242),RGB(61,61,61),RGB(65,65,65),RGB(116,177,255),RGB(255,137,132),
        RGB(31,104,213),RGB(23,78,164),RGB(255,255,255),RGB(49,49,49),RGB(61,61,61),RGB(218,232,255),RGB(43,43,43)};
    return {RGB(248,250,253),RGB(28,37,52),RGB(77,91,111),RGB(131,142,158),RGB(230,235,243),RGB(28,37,52),
        RGB(214,222,234),RGB(210,216,225),RGB(31,104,213),RGB(192,66,61),RGB(31,104,213),RGB(23,78,164),RGB(255,255,255),
        RGB(232,239,250),RGB(214,222,234),RGB(31,79,148),RGB(234,238,244)};
}
void ApplyWindowAppearance(HWND window, Appearance appearance) {
    if (!window) return;
    const BOOL dark = appearance.dark && !appearance.high_contrast;
    DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    const COLORREF border = ColorsFor(appearance).separator;
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
}
}
