#include "policy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace rrs {
int TargetRate(PowerSource source, const std::vector<int>& rates, const RefreshTargets& targets) {
    if (source == PowerSource::unknown) return 0;
    const int configured = source == PowerSource::ac ? targets.ac : targets.battery;
    if (configured > 1) return configured;
    std::vector<int> valid;
    for (const int hz : rates) if (hz > 1) valid.push_back(hz);
    if (valid.empty()) return 0;
    std::sort(valid.begin(),valid.end());
    if (source == PowerSource::ac) return valid.back();
    for (const int preferred : {60,59})
        if (std::find(valid.begin(),valid.end(),preferred) != valid.end()) return preferred;
    const auto first = std::lower_bound(valid.begin(),valid.end(),60);
    return first != valid.end() ? *first : valid.back();
}
int TargetRate(const PolicyInput& input, const RefreshTargets& targets) {
    return TargetRate(input.power,input.screen ? input.screen->supported_hz : std::vector<int>{},targets);
}
std::wstring CapabilityKey(const PolicyInput& input) {
    std::wstring key = std::to_wstring(static_cast<int>(input.availability));
    if (!input.screen) return key;
    const auto& screen = *input.screen;
    key += L"|" + screen.route + L"|" + screen.capability_key + (screen.cloned ? L"|clone" : L"|solo") +
        (screen.dynamic_refresh ? L"|drr" : L"|fixed") + (screen.virtual_mode_supported ? L"|virtual-aware" : L"|signal-only");
    for (const int hz : screen.supported_hz) key += L"|" + std::to_wstring(hz);
    // The current rate is deliberately excluded: external manual choices must
    // still survive duplicate power/display notifications in automatic mode.
    return key;
}
bool RateMatches(int nominal, double physical, int target) {
    if (target <= 1 || nominal <= 1 || !std::isfinite(physical) || physical <= 1) return false;
    // DEVMODE reports integer Hz; the actual signal may be truncated (e.g.
    // nominal 143, actual 143.998) or use a 1000/1001 timing (e.g. 59.94).
    if (nominal == target && std::abs(physical-target) < 1.0) return true;
    if (std::floor(physical) == nominal && std::round(physical) == target && std::abs(physical-target) < 0.5) return true;
    if (std::abs(physical-std::round(physical)) < 0.01) return false;
    const int upper = std::max(nominal,target);
    return std::abs(nominal-target) == 1 && std::abs(physical-upper*1000.0/1001.0) < 0.1;
}
bool RateMatches(const Screen& screen, int target) {
    if (!screen.virtual_mode_supported) return RateMatches(screen.nominal_hz,screen.physical_hz,target);
    if (screen.dynamic_refresh || !std::isfinite(screen.physical_hz) || screen.physical_hz <= 1) return false;
    const bool signal_matches = RateMatches(screen.nominal_hz,screen.physical_hz,target);
    const bool desktop_matches = RateMatches(screen.nominal_hz,screen.desktop_hz,target) ||
        (signal_matches && std::isfinite(screen.desktop_hz) && screen.desktop_hz > 1 &&
            screen.desktop_hz <= std::numeric_limits<int>::max() && std::abs(screen.desktop_hz-std::round(screen.desktop_hz)) < 0.01 &&
            RateMatches(static_cast<int>(std::round(screen.desktop_hz)),screen.physical_hz,target));
    if (!desktop_matches) return false;
    // Windows 11 can expose a fixed desktop rate below the physical signal
    // even when DRR is disabled. Accept only explicit CCD virtual-mode
    // readback, with the selected desktop rate and a valid, faster signal.
    return signal_matches || screen.physical_hz > screen.desktop_hz+0.5;
}
Decision CheckTarget(const PolicyInput& input, int target) {
    if (input.availability == Availability::absent) return {Action::wait, Reason::no_screen, target};
    if (input.availability == Availability::ambiguous) return {Action::reject, Reason::ambiguous_screen, target};
    if (input.availability == Availability::remote) return {Action::wait, Reason::remote_session, target};
    if (input.availability != Availability::ready || !input.screen) return {Action::reject, Reason::query_failed, target};
    const auto& screen = *input.screen;
    if (screen.cloned) return {Action::reject, Reason::cloned_source, target};
    if (screen.dynamic_refresh) return {Action::reject, Reason::dynamic_refresh, target};
    if (screen.supported_hz.empty()) return {Action::reject, Reason::no_valid_modes, target};
    if (target <= 1) return {Action::reject, Reason::unsupported_mode, target};
    if (std::find(screen.supported_hz.begin(), screen.supported_hz.end(), target) == screen.supported_hz.end())
        return {Action::reject, Reason::unsupported_mode, target};
    return {RateMatches(screen,target) ? Action::unchanged : Action::apply, Reason::none, target};
}
void ConfirmationPolicy::Reset(PowerSource baseline) {
    last_known_power_ = baseline;
    pending_.reset();
}
const std::optional<Confirmation>& ConfirmationPolicy::Update(const PolicyInput& input, const RefreshTargets& targets) {
    bool changed = false;
    if (input.power != PowerSource::unknown) {
        changed = last_known_power_ != PowerSource::unknown && input.power != last_known_power_;
        last_known_power_ = input.power;
    }
    const int target = TargetRate(last_known_power_,input.screen ? input.screen->supported_hz : std::vector<int>{},targets);
    auto decision = CheckTarget(input, target);
    if (input.power == PowerSource::unknown) decision = {Action::wait, Reason::unknown_power, target};
    if (!pending_ && (!changed || decision.action == Action::unchanged)) return pending_;
    Confirmation candidate;
    candidate.power = input.power;
    candidate.route = input.screen ? input.screen->route : L"";
    candidate.target_hz = target;
    candidate.reason = decision.reason;
    candidate.needed = decision.action == Action::apply;
    candidate.capability_key = CapabilityKey(input);
    candidate.capability_version = input.screen ? input.screen->capability_version : 0;
    if (!pending_ || candidate.power != pending_->power || candidate.route != pending_->route ||
        candidate.target_hz != pending_->target_hz || candidate.reason != pending_->reason || candidate.needed != pending_->needed ||
        candidate.capability_key != pending_->capability_key || candidate.capability_version != pending_->capability_version) {
        candidate.id = ++sequence_;
        pending_ = std::move(candidate);
    }
    return pending_;
}
bool ConfirmationPolicy::Matches(const Confirmation& request, const PolicyInput& input, const RefreshTargets& targets) const {
    if (!pending_ || pending_->id != request.id || !pending_->needed || pending_->reason != Reason::none ||
        input.power == PowerSource::unknown || input.power != request.power || TargetRate(input,targets) != request.target_hz ||
        !input.screen || input.screen->route != request.route || CapabilityKey(input) != request.capability_key ||
        input.screen->capability_version != request.capability_version) return false;
    return CheckTarget(input, request.target_hz).action == Action::apply;
}
void ConfirmationPolicy::Dismiss(std::uint64_t id) {
    if (pending_ && pending_->id == id) pending_.reset();
}
Decision Policy::Evaluate(const PolicyInput& input, Trigger trigger, bool enabled, const RefreshTargets& targets) {
    const bool power_changed = input.power != last_power_;
    const std::wstring route = input.screen ? input.screen->route : L"";
    const bool route_changed = route != last_route_;
    const auto capability = CapabilityKey(input);
    const bool capability_changed = capability != last_capability_, targets_changed = targets != last_targets_;
    last_power_ = input.power;
    last_route_ = route;
    last_capability_ = capability; last_targets_ = targets;
    const int target = TargetRate(input,targets);
    if (!enabled) return {Action::observe, Reason::paused, target};
    if (input.power == PowerSource::unknown) return {Action::wait, Reason::unknown_power, 0};
    if (input.availability == Availability::absent) return {Action::wait, Reason::no_screen, target};
    if (input.availability == Availability::ambiguous) return {Action::reject, Reason::ambiguous_screen, target};
    if (input.availability == Availability::remote) return {Action::wait, Reason::remote_session, target};
    if (input.availability != Availability::ready || !input.screen) return {Action::reject, Reason::query_failed, target};
    const bool forced = trigger == Trigger::startup || trigger == Trigger::resume || trigger == Trigger::enable;
    if (!forced && !power_changed && !route_changed && !capability_changed && !targets_changed) return {Action::observe, Reason::none, target};
    return CheckTarget(input, target);
}
}
