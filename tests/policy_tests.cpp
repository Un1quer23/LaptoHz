#include "policy.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace rrs;
namespace {
int assertions = 0;
void Expect(bool condition, const char* name) {
    ++assertions;
    if (!condition) throw std::runtime_error(name);
}
PolicyInput Input(PowerSource power, int hz) {
    PolicyInput input;
    input.power = power; input.availability = Availability::ready;
    input.screen = Screen{L"internal|gpu-1|solo|fixed", hz, static_cast<double>(hz), {60,120,240}, false, false};
    return input;
}
}
int main() {
    try {
        Expect(TargetRate(PowerSource::ac,{60,240}) == 240, "AC rule");
        Expect(TargetRate(PowerSource::battery,{60,240}) == 60, "Battery rule");
        Expect(TargetRate(PowerSource::unknown,{60,240}) == 0, "Unknown power does not guess");
        Expect(RateMatches(240, 239.76, 240), "Fractional signal frequency is accepted");
        Expect(!RateMatches(240, 60, 240), "Nominal frequency alone is insufficient");
        Expect(!RateMatches(60, 0, 60), "Unknown physical frequency is not confirmed");
        {
            Policy policy;
            const auto decision = policy.Evaluate(Input(PowerSource::ac, 60), Trigger::startup, true);
            Expect(decision.action == Action::apply && decision.target_hz == 240, "Startup reconciles AC");
        }
        {
            Policy policy;
            Expect(policy.Evaluate(Input(PowerSource::battery, 240), Trigger::startup, true).target_hz == 60, "Startup reconciles battery");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 240);
            Expect(policy.Evaluate(input, Trigger::startup, true).action == Action::unchanged, "No redundant switch at target");
            input.screen->nominal_hz = 120; input.screen->physical_hz = 120;
            Expect(policy.Evaluate(input, Trigger::display_change, true).action == Action::observe, "Manual override survives display notification");
            Expect(policy.Evaluate(input, Trigger::power_change, true).action == Action::observe, "Duplicate AC notification preserves manual override");
            input.power = PowerSource::battery;
            auto decision = policy.Evaluate(input, Trigger::power_change, true);
            Expect(decision.action == Action::apply && decision.target_hz == 60, "Actual source change ends manual override");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 240);
            policy.Evaluate(input, Trigger::startup, true);
            input.screen->nominal_hz = 120; input.screen->physical_hz = 120;
            Expect(policy.Evaluate(input, Trigger::resume, true).action == Action::apply, "Resume re-applies rule");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 60);
            Expect(policy.Evaluate(input, Trigger::startup, false).reason == Reason::paused, "Paused does not change display");
            Expect(policy.Evaluate(input, Trigger::enable, true).action == Action::apply, "Enabling applies latest source immediately");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::unknown, 240);
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::unknown_power, "Unknown power waits");
            input.power = PowerSource::battery;
            Expect(policy.Evaluate(input, Trigger::power_change, true).action == Action::apply, "Recovery from unknown power");
        }
        {
            Policy policy;
            PolicyInput absent; absent.power = PowerSource::ac;
            Expect(policy.Evaluate(absent, Trigger::startup, true).reason == Reason::no_screen, "External-only mode waits");
            Expect(policy.Evaluate(Input(PowerSource::ac, 60), Trigger::display_change, true).action == Action::apply, "Internal reactivation applies rule");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 240);
            policy.Evaluate(input, Trigger::startup, true);
            input.screen->route = L"internal|gpu-2|solo|fixed";
            input.screen->nominal_hz = 60; input.screen->physical_hz = 60;
            Expect(policy.Evaluate(input, Trigger::display_change, true).action == Action::apply, "GPU route change re-enumerates target");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 60); input.screen->cloned = true;
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::cloned_source, "Clone source is never changed");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 240); input.screen->dynamic_refresh = true;
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::dynamic_refresh, "DRR is not mistaken for fixed target");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 60); input.screen->supported_hz = {60,120};
            Expect(policy.Evaluate(input, Trigger::startup, true,{240,0}).reason == Reason::unsupported_mode, "Explicit unavailable target is not substituted");
        }
        {
            Policy policy;
            auto input = Input(PowerSource::ac, 60); input.availability = Availability::ambiguous;
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::ambiguous_screen, "Ambiguous internal target is not guessed");
            input.availability = Availability::remote;
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::remote_session, "Remote session waits");
            input.availability = Availability::error;
            Expect(policy.Evaluate(input, Trigger::startup, true).reason == Reason::query_failed, "Query failure does not apply stale mode");
        }
        for (const int highest : {90,120,144,165,240}) {
            const std::vector<int> rates{60,highest};
            Expect(TargetRate(PowerSource::ac,rates) == highest,"Default AC follows each laptop's maximum.");
            Expect(TargetRate(PowerSource::battery,rates) == 60,"Battery prefers 60 across laptop combinations.");
            auto input = Input(PowerSource::ac,60); input.screen->supported_hz = rates;
            Expect(CheckTarget(input,highest).action == Action::apply,"Any validated refresh-rate target can apply.");
        }
        Expect(TargetRate(PowerSource::battery,{30,48,60,120,240}) == 60,"Low modes do not replace the preferred battery default.");
        Expect(TargetRate(PowerSource::battery,{48,59,120}) == 59,"59Hz is the second battery preference.");
        Expect(TargetRate(PowerSource::battery,{90,165}) == 90,"High-only panels use their lowest rate above 60.");
        Expect(TargetRate(PowerSource::battery,{30,48}) == 48,"A panel entirely below 60 uses its highest rate.");
        Expect(TargetRate(PowerSource::ac,{60}) == 60 && TargetRate(PowerSource::battery,{60}) == 60,"Single-rate panels share the same target.");
        Expect(TargetRate(PowerSource::ac,{}) == 0,"No validated rates produce no invented default.");
        Expect(TargetRate(PowerSource::ac,{165,60,90,1,0}) == 165,"Rate selection ignores invalid values and ordering.");
        Expect(TargetRate(PowerSource::battery,{30,48,60},{120,30}) == 30,"Explicit low battery targets are honored.");
        Expect(TargetRate(PowerSource::ac,{60,120},{165,0}) == 165,"Unavailable explicit targets remain configured.");
        Expect(RateMatches(60,59.94,60) && RateMatches(120,119.88,120),"Fractional rates are accepted for the corresponding nominal mode.");
        Expect(RateMatches(59,59.94,60) && RateMatches(60,59.94,59),"Known fractional aliases survive driver rounding in either direction.");
        Expect(RateMatches(143,143.998,144) && RateMatches(143,143.998,143),"Truncated physical rates do not cause false rollback.");
        Expect(!RateMatches(119,119.0,120) && !RateMatches(60,60.0,59),"Different adjacent integer rates are not blanket aliases.");
        Expect(!RateMatches(144,60,144) && !RateMatches(60,std::numeric_limits<double>::infinity(),60),"Wrong or nonfinite physical rates are rejected.");
        {
            auto input = Input(PowerSource::battery,30);
            auto& screen = *input.screen;
            screen.supported_hz = {30,48,60,240}; screen.physical_hz = 60; screen.desktop_hz = 30;
            Expect(!RateMatches(screen,30),"A nominal-only lower rate cannot bypass physical validation.");
            screen.virtual_mode_supported = true;
            Expect(RateMatches(screen,30) && !RateMatches(screen,60),"Explicit virtual readback distinguishes 30Hz desktop from 60Hz signal.");
            Expect(CheckTarget(input,30).action == Action::unchanged && CheckTarget(input,60).action == Action::apply,
                "An already applied virtual rate is not re-applied or confused with its physical rate.");
            screen.nominal_hz = 48; screen.desktop_hz = 48;
            Expect(RateMatches(screen,48),"Fixed 48Hz desktop at 60Hz signal is accepted without an integer divider assumption.");
            screen.nominal_hz = 30;
            Expect(!RateMatches(screen,30),"Stale desktop readback cannot confirm the new nominal mode.");
            screen.desktop_hz = 30; screen.physical_hz = 0;
            Expect(!RateMatches(screen,30),"Virtual modes still require a readable physical signal.");
            screen.physical_hz = std::numeric_limits<double>::infinity();
            Expect(!RateMatches(screen,30),"Nonfinite signal readback rejects a virtual target.");
            screen.physical_hz = 24;
            Expect(!RateMatches(screen,30),"A signal below the selected desktop rate is not accepted as virtualization.");
            screen.physical_hz = 60; screen.dynamic_refresh = true;
            Expect(!RateMatches(screen,30) && CheckTarget(input,30).reason == Reason::dynamic_refresh,"Virtual support does not bypass the DRR boundary.");
            screen.dynamic_refresh = false; screen.nominal_hz = 60; screen.desktop_hz = 60;
            screen.physical_hz = 59;
            Expect(!RateMatches(screen,60),"Adjacent integer signal mismatch is still rejected.");
            screen.physical_hz = 59.94;
            Expect(RateMatches(screen,60),"Fractional physical timing still works in virtual-aware paths.");
            screen.desktop_hz = 0;
            Expect(!RateMatches(screen,60),"Missing desktop rate is not silently replaced in an explicitly virtual-aware path.");
            screen.nominal_hz = 59; screen.desktop_hz = 59; screen.physical_hz = 59.94;
            Expect(RateMatches(screen,59) && RateMatches(screen,60),"Integer-rounded CCD desktop readback retains proven fractional aliases.");
            screen.nominal_hz = 60;
            Expect(RateMatches(screen,59) && RateMatches(screen,60),"DEVMODE and CCD may round the same fractional signal differently.");
            screen.nominal_hz = 143; screen.desktop_hz = 143; screen.physical_hz = 143.998;
            Expect(RateMatches(screen,144),"Truncated desktop and nominal reports still verify against fractional physical timing.");
            screen.nominal_hz = 59; screen.desktop_hz = 59; screen.physical_hz = 60;
            Expect(!RateMatches(screen,60),"An exact neighboring integer signal does not justify a fractional alias.");
            screen.nominal_hz = 60;
            Expect(!RateMatches(screen,60),"A mismatched exact desktop integer is not hidden by correct physical timing.");
            screen.desktop_hz = 30; screen.physical_hz = 59.94;
            Expect(!RateMatches(screen,60),"Fractional signal evidence cannot bypass a stale unrelated desktop rate.");
        }
        {
            Policy policy; auto input = Input(PowerSource::ac,240);
            policy.Evaluate(input,Trigger::startup,true);
            input.screen->supported_hz = {60,165};
            Expect(policy.Evaluate(input,Trigger::display_change,true).target_hz == 165,"Capability changes re-resolve automatic defaults.");
            input.screen->nominal_hz = 60; input.screen->physical_hz = 60;
            Expect(policy.Evaluate(input,Trigger::display_change,true).action == Action::observe,"Changing only the current rate is not a capability change.");
            input.screen->capability_key = L"1920x1080|32|0";
            Expect(policy.Evaluate(input,Trigger::display_change,true).action == Action::apply,"Resolution changes also trigger revalidation.");
            Expect(policy.Evaluate(input,Trigger::display_change,true,{120,0}).reason == Reason::unsupported_mode,"Changing targets invalidates the old policy decision.");
            input.power = PowerSource::battery;
            Expect(policy.Evaluate(input,Trigger::power_change,true,{120,0}).action == Action::unchanged,"An invalid AC rule does not block a valid battery rule.");
            input.screen->supported_hz.clear();
            Expect(policy.Evaluate(input,Trigger::display_change,true).reason == Reason::no_valid_modes,"Driver-rejected candidates do not become defaults.");
        }
        std::cout << "PASS: " << assertions << " policy assertions.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
