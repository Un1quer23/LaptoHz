#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include "appearance.hpp"

namespace rrs {
inline constexpr UINT kPopupClosed = WM_APP + 30, kPopupAction = WM_APP + 31;
enum class PopupKind { notification, confirmation };
enum class PopupAction { dismiss, first, second };
struct PopupEvent { PopupKind kind; PopupAction action; std::uint64_t token; };
struct PopupButton { std::wstring text; bool enabled = true, primary = false; };
class Popup {
public:
    ~Popup();
    bool Initialize(HINSTANCE instance, HWND owner, PopupKind kind = PopupKind::notification);
    void Show(const std::wstring& title, const std::wstring& body, bool error = false, const std::wstring& device = {});
    void Hide();
    void ShowActions(const std::wstring& title, const std::wstring& body, const std::array<PopupButton, 2>& buttons,
        std::uint64_t token, const std::wstring& device = {}, bool error = false);
    bool Translate(MSG& message);
    HWND Window() const { return hwnd_; }
    RECT Bounds() const;
    void Avoid(const RECT& occupied);
    bool Capture(const std::filesystem::path& path) const;
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void RefreshAppearance(Appearance appearance);
    void RefreshLanguage();
private:
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void Paint();
    void DrawButton(const DRAWITEMSTRUCT& item);
    void Position(const std::wstring& device);
    SIZE ContentSize(int width, int minimum_height, const RECT& work) const;
    void ChangeDpi(UINT dpi, const RECT& suggested);
    void LayoutButtons();
    void Emit(PopupAction action);
    void Dismiss();
    RECT CloseButtonRect() const;
    void ScheduleCountdownTick();
    HWND hwnd_ = nullptr, owner_ = nullptr;
    PopupKind kind_ = PopupKind::notification;
    std::array<HWND, 3> controls_{};
    std::array<PopupButton, 2> buttons_{};
    HFONT button_font_ = nullptr;
    std::uint64_t token_ = 0;
    std::wstring title_, body_;
    UINT dpi_ = 96;
    bool positioning_ = false;
    bool error_ = false;
    bool close_hovered_ = false, close_pressed_ = false;
    ULONGLONG deadline_ = 0;
    Palette colors_ = ColorsFor({});
};
}
