#include "policy.hpp"
#include <iostream>
#include <stdexcept>
using namespace rrs;
namespace {
int assertions = 0;
void Expect(bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
PolicyInput Input(PowerSource power, int hz = 240) {
    return {power, Availability::ready, Screen{L"inner|gpu-a|solo|fixed",hz,static_cast<double>(hz),{60,240},false,false}};
}
}
int main() {
    try {
        ConfirmationPolicy policy;
        auto ac = Input(PowerSource::ac,60);
        Expect(!policy.Update(ac),"The first known source only establishes a baseline.");
        Expect(!policy.Update(ac),"A duplicate source never prompts.");
        auto battery = Input(PowerSource::battery);
        auto request = *policy.Update(battery);
        Expect(request.needed && request.target_hz == 60,"AC to battery proposes 60Hz.");
        Expect(policy.Matches(request,battery),"A current confirmation can execute.");
        Expect(policy.Update(battery)->id == request.id,"Duplicate observations preserve the same request.");
        Expect(!policy.Matches(request,ac),"A changed source invalidates a ticket even before the next observation.");
        ac = Input(PowerSource::ac,240);
        const auto matched = *policy.Update(ac);
        Expect(matched.id != request.id && !matched.needed && matched.reason == Reason::none,"A pending window remains with no switch needed.");
        policy.Dismiss(request.id);
        Expect(policy.Update(ac).has_value(),"An old close action cannot dismiss the replacement ticket.");
        policy.Dismiss(matched.id);
        ac = Input(PowerSource::ac,60);
        Expect(!policy.Update(ac),"Dismissal survives display-only changes in the same power cycle.");
        battery = Input(PowerSource::battery,60);
        Expect(!policy.Update(battery),"No new window is created when already at the recommendation.");
        request = *policy.Update(ac);
        Expect(request.target_hz == 240 && request.needed,"Battery to AC proposes 240Hz.");
        auto other_route = ac; other_route.screen->route = L"inner|gpu-b|solo|fixed";
        Expect(!policy.Matches(request,other_route),"Route changes invalidate approval.");
        const auto changed_route = *policy.Update(other_route);
        Expect(changed_route.id != request.id && changed_route.needed,"Route recovery updates the existing request.");
        auto absent = other_route; absent.availability = Availability::absent; absent.screen.reset();
        const auto waiting = *policy.Update(absent);
        Expect(waiting.reason == Reason::no_screen && !waiting.needed,"An absent internal screen disables confirmation.");
        Expect(!policy.Matches(changed_route,absent),"An absent screen cannot execute a ticket.");
        request = *policy.Update(other_route);
        Expect(request.needed && request.id != waiting.id,"Screen recovery produces a fresh usable ticket.");
        auto unknown = other_route; unknown.power = PowerSource::unknown;
        Expect(policy.Update(unknown)->reason == Reason::unknown_power,"Unknown power disables an existing proposal.");
        Expect(!policy.Matches(request,unknown),"Unknown power cannot approve a request.");
        Expect(policy.Update(other_route)->needed,"Known power restores the proposal without inventing a transition.");
        auto clone = other_route; clone.screen->cloned = true;
        Expect(policy.Update(clone)->reason == Reason::cloned_source,"Clone mode is blocked.");
        auto drr = other_route; drr.screen->dynamic_refresh = true;
        Expect(policy.Update(drr)->reason == Reason::dynamic_refresh,"DRR is blocked.");
        auto unsupported = other_route; unsupported.screen->supported_hz = {60};
        Expect(policy.Update(unsupported,{240,0})->reason == Reason::unsupported_mode,"A missing explicit target mode is blocked.");
        auto remote = other_route; remote.availability = Availability::remote;
        Expect(policy.Update(remote)->reason == Reason::remote_session,"Remote sessions are blocked.");
        auto ambiguous = other_route; ambiguous.availability = Availability::ambiguous;
        Expect(policy.Update(ambiguous)->reason == Reason::ambiguous_screen,"Ambiguous internal targets are blocked.");
        auto error = other_route; error.availability = Availability::error;
        Expect(policy.Update(error)->reason == Reason::query_failed,"Query failures are blocked.");
        request = *policy.Update(other_route);
        policy.Reset(PowerSource::ac);
        Expect(!policy.Update(ac),"Entering confirmation mode establishes a baseline and removes old requests.");
        Expect(!policy.Matches(request,ac),"A reset invalidates outstanding approvals.");
        Expect(policy.Update(Input(PowerSource::battery))->id > request.id,"Ticket IDs are never reused across mode changes.");
        policy.Reset(PowerSource::ac);
        Expect(!policy.Update(Input(PowerSource::unknown)),"Unknown power does not erase the remembered baseline.");
        Expect(policy.Update(Input(PowerSource::battery))->needed,"A source change during sleep/unknown observation is detected on recovery.");
        policy.Reset();
        Expect(!policy.Update(Input(PowerSource::unknown)),"Startup without a source does not prompt.");
        Expect(!policy.Update(Input(PowerSource::battery)),"First source recovery establishes a baseline.");
        Expect(CheckTarget(Input(PowerSource::unknown,60),240).action == Action::apply,"Manual target checking does not depend on power.");
        Expect(CheckTarget(ac,120).reason == Reason::unsupported_mode,"A rate absent from the capability snapshot is rejected.");
        auto other = Input(PowerSource::ac,60); other.screen->supported_hz = {60,120,165};
        policy.Reset(PowerSource::battery);
        request = *policy.Update(other);
        Expect(request.target_hz == 165 && request.needed,"Another laptop resolves its own AC confirmation target.");
        auto changed = other; changed.screen->supported_hz = {60,120};
        Expect(!policy.Matches(request,changed),"Changing the rate list invalidates approval on the same route.");
        auto revised = *policy.Update(changed);
        Expect(revised.id != request.id && revised.target_hz == 120,"A pending window receives a fresh target when capabilities change.");
        changed.screen->capability_key = L"1920x1080";
        Expect(!policy.Matches(revised,changed),"Changing resolution also invalidates the ticket.");
        auto restored = other; restored.screen->capability_version = 2;
        Expect(!policy.Matches(request,restored),"Restoring the same capabilities cannot reuse a ticket from an older capability generation.");
        Expect(!policy.Matches(revised,other,{120,0}),"Changing configured targets cannot execute a previous approval.");
        policy.Reset(PowerSource::ac);
        Expect(!policy.Update(other,{120,0}),"New targets plus a fresh baseline do not manufacture a power transition.");
        other.power = PowerSource::battery; other.screen->nominal_hz = 165; other.screen->physical_hz = 165;
        Expect(policy.Update(other,{120,60})->target_hz == 60,"The next real transition uses the configured battery target.");
        std::cout << "PASS: " << assertions << " confirmation assertions.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
