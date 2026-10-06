#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rrs {
enum class PowerSource { ac, battery, unknown };
enum class Mode { automatic, confirmation, manual };
enum class Trigger { startup, power_change, resume, enable, display_change };
enum class Availability { ready, absent, ambiguous, remote, error };
enum class Action { observe, wait, unchanged, apply, reject };
enum class Reason { none, paused, unknown_power, no_screen, ambiguous_screen, remote_session,
                    query_failed, cloned_source, dynamic_refresh, unsupported_mode, no_valid_modes, capabilities_changed };
// Zero means the dynamic default for this power source, not a resolved rate.
struct RefreshTargets {
    int ac = 0, battery = 0;
    bool operator==(const RefreshTargets&) const = default;
};
struct Screen {
    std::wstring route;
    int nominal_hz = 0;
    double physical_hz = 0;
    std::vector<int> supported_hz;
    bool cloned = false;
    bool dynamic_refresh = false;
    std::wstring capability_key = {};
    std::uint64_t capability_version = 0;
    double desktop_hz = 0;
    bool virtual_mode_supported = false;
};
struct PolicyInput {
    PowerSource power = PowerSource::unknown;
    Availability availability = Availability::absent;
    std::optional<Screen> screen;
};
struct Decision {
    Action action = Action::wait;
    Reason reason = Reason::none;
    int target_hz = 0;
};
int TargetRate(PowerSource source, const std::vector<int>& rates, const RefreshTargets& targets = {});
int TargetRate(const PolicyInput& input, const RefreshTargets& targets = {});
std::wstring CapabilityKey(const PolicyInput& input);
bool RateMatches(int nominal_hz, double physical_hz, int target_hz);
bool RateMatches(const Screen& screen, int target_hz);
Decision CheckTarget(const PolicyInput& input, int target_hz);
struct Confirmation {
    std::uint64_t id = 0;
    PowerSource power = PowerSource::unknown;
    std::wstring route;
    int target_hz = 0;
    Reason reason = Reason::none;
    bool needed = false;
    std::wstring capability_key;
    std::uint64_t capability_version = 0;
};
class ConfirmationPolicy {
public:
    void Reset(PowerSource baseline = PowerSource::unknown);
    const std::optional<Confirmation>& Update(const PolicyInput& input, const RefreshTargets& targets = {});
    bool Matches(const Confirmation& request, const PolicyInput& input, const RefreshTargets& targets = {}) const;
    void Dismiss(std::uint64_t id);
private:
    PowerSource last_known_power_ = PowerSource::unknown;
    std::uint64_t sequence_ = 0;
    std::optional<Confirmation> pending_;
};
class Policy {
public:
    Decision Evaluate(const PolicyInput& input, Trigger trigger, bool enabled, const RefreshTargets& targets = {});
private:
    PowerSource last_power_ = PowerSource::unknown;
    std::wstring last_route_;
    std::wstring last_capability_;
    RefreshTargets last_targets_;
};
}
