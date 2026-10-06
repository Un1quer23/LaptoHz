#pragma once
#include "support.hpp"
#include "physical_modes.hpp"
#include <functional>
#include <optional>
#include <utility>

namespace rrs {
struct PathState {
    std::wstring identity;
    double hz = 0;
    int width = 0, height = 0, x = 0, y = 0;
    UINT32 rotation = 0, scaling = 0;
    std::optional<bool> advanced_color;
    double desktop_hz = 0;
    bool operator==(const PathState&) const = default;
};
struct DisplaySnapshot {
    PolicyInput policy;
    std::wstring device, monitor, detail;
    DEVMODEW current{};
    LONG error = 0;
    std::vector<PathState> paths;
    struct RateValidation { int hz; LONG code; bool tested = true; ModeOrigin origin = ModeOrigin::physical; };
    std::vector<RateValidation> rate_validations;
    bool physical_modes_known = false;
    LONG physical_modes_error = 0;
};
enum class RecoveryState { not_needed, verified, request_failed, verification_failed, cancelled, environment_changed };
std::wstring RecoveryStateText(RecoveryState state);
struct ApplyResult {
    bool success = false, changed = false, retryable = false;
    LONG code = 0;
    RecoveryState recovery = RecoveryState::not_needed;
    std::optional<LONG> recovery_code;
    std::wstring detail;
    DisplaySnapshot after;
};
// Empty callbacks use Windows. Injected callbacks exercise the same apply and
// recovery flow without changing the real display or sleeping in unit tests.
struct DisplayServices {
    std::function<DisplaySnapshot()> inspect;
    std::function<LONG(const std::wstring&, const DEVMODEW&, DWORD)> change_settings;
    std::function<void(DWORD)> wait;
};
class DisplayBackend {
public:
    explicit DisplayBackend(DisplayServices services = {}) : services_(std::move(services)) {}
    DisplaySnapshot Inspect() const;
    LONG Validate(const DisplaySnapshot& snapshot, int target_hz) const;
    ApplyResult Apply(const DisplaySnapshot& snapshot, int target_hz, const std::function<bool()>& cancelled) const;
private:
    LONG ChangeSettings(const std::wstring& device, DEVMODEW mode, DWORD flags) const;
    void Wait(DWORD milliseconds) const;
    DisplayServices services_;
};
std::string DiagnosticJson(const DisplaySnapshot& snapshot, const DisplayBackend& backend, const RefreshTargets& targets = {});
}
