#pragma once
#include "support.hpp"
#include "appearance.hpp"
#include <vector>
#include <memory>

namespace rrs {
class ModeHint;
inline constexpr UINT kMenuSummary = 100, kMenuStartup = 102, kMenuLogs = 103, kMenuExit = 104;
inline constexpr UINT kMenuRates = 106;
inline constexpr UINT kMenuNoRates = 107, kMenuAcTarget = 130, kMenuBatteryTarget = 131;
inline constexpr UINT kMenuAcAuto = 200, kMenuBatteryAuto = 201;
inline constexpr UINT kMenuAuto = 110, kMenuConfirm = 111, kMenuManual = 112;
inline constexpr UINT kMenu60 = 120, kMenu240 = 121;
struct TrayMenuState {
    std::wstring summary;
    std::optional<PolicyInput> input;
    Mode mode = Mode::automatic;
    bool busy = false, usable = true, events_ready = true, startup = false;
    RefreshTargets targets;
};
enum class TrayActionKind { manual, ac_target, battery_target };
struct TrayAction { TrayActionKind kind; int hz; std::wstring capability_key; std::uint64_t capability_version = 0; };
HMENU CreateTrayMenu(const TrayMenuState& state);
void DestroyTrayMenu(HMENU menu);
std::optional<TrayAction> ResolveTrayAction(HMENU menu, UINT command);
void UpdateTrayMenu(HMENU menu, const TrayMenuState& state);
HWND FindTrayMenuWindow(HWND owner, HMENU expected = nullptr);
std::optional<RECT> TrayMenuBounds(HWND owner, HMENU root);
class TrayMenuRenderer {
public:
    ~TrayMenuRenderer();
    TrayMenuRenderer();
    TrayMenuRenderer(const TrayMenuRenderer&) = delete;
    TrayMenuRenderer& operator=(const TrayMenuRenderer&) = delete;
    bool Attach(HMENU menu, HWND owner, Appearance appearance, UINT minimum_width = 0);
    UINT Width() const { return width_; }
    void RefreshAppearance(Appearance appearance);
    void RefreshVisibleWindows();
    void MenuSelection(HMENU menu, UINT command, UINT flags);
    void MenuInput(const MSG& message);
    bool Measure(MEASUREITEMSTRUCT& item) const;
    bool Draw(const DRAWITEMSTRUCT& item) const;
private:
    struct Row { HMENU menu; UINT id; bool separator, submenu, radio; UINT width = 0; };
    const Row* Find(ULONG_PTR data) const;
    HMENU menu_ = nullptr;
    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    HHOOK input_hook_ = nullptr;
    HWND previous_owner_ = nullptr;
    HMENU previous_menu_ = nullptr;
    TrayMenuRenderer* previous_renderer_ = nullptr;
    std::unique_ptr<ModeHint> hint_;
    UINT dpi_ = 96, width_ = 0;
    Palette colors_ = ColorsFor({});
    Appearance appearance_;
    std::vector<std::pair<HWND,HMENU>> styled_windows_;
    void StyleVisibleWindows(bool repaint);
    std::vector<Row> rows_;
};
}
