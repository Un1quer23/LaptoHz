#include "display.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace rrs;
namespace {
DisplaySnapshot Snapshot(int nominal = 240, double desktop = 240, double physical = 240) {
    DisplaySnapshot value;
    value.device = L"test-display";
    value.policy = {PowerSource::ac,Availability::ready,
        Screen{L"inner|source|test-display|solo|fixed",nominal,physical,{60,120,240},false,false,L"2560x1600|32",0,desktop,true}};
    value.current.dmSize = sizeof(value.current);
    value.current.dmFields = DM_DISPLAYFREQUENCY | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    value.current.dmDisplayFrequency = static_cast<DWORD>(nominal);
    value.current.dmPelsWidth = 2560; value.current.dmPelsHeight = 1600; value.current.dmBitsPerPel = 32;
    value.paths = {{L"inner",physical,2560,1600,0,0,1,1,false,desktop},
                   {L"outer",60,1920,1080,2560,0,1,1,false,60}};
    value.physical_modes_known = true;
    return value;
}
struct Fixture {
    DisplaySnapshot saved = Snapshot();
    std::vector<DisplaySnapshot> target_reads{Snapshot(60,60,240)};
    std::vector<DisplaySnapshot> recovery_reads{saved};
    LONG validation_code = DISP_CHANGE_SUCCESSFUL, target_code = DISP_CHANGE_SUCCESSFUL, recovery_code = DISP_CHANGE_SUCCESSFUL;
    int target_hz = 60, stage = 0, target_read_count = 0, recovery_read_count = 0;
    int validation_calls = 0, target_calls = 0, recovery_calls = 0;
    bool cancelled = false;
    struct Request { std::wstring device; DEVMODEW mode; DWORD flags; };
    std::vector<Request> requests;
    std::vector<std::pair<int,DWORD>> waits;
    std::function<void()> on_recovery;
    std::function<void(int)> on_target_read, on_recovery_read;
    std::function<void(int)> on_wait;
    DisplaySnapshot Inspect() {
        if (!stage) return saved;
        if (stage == 1) {
            const auto index = static_cast<size_t>(target_read_count++);
            auto value = target_reads[std::min(index,target_reads.size()-1)];
            if (on_target_read) on_target_read(target_read_count);
            return value;
        }
        const auto index = static_cast<size_t>(recovery_read_count++);
        auto value = recovery_reads[std::min(index,recovery_reads.size()-1)];
        if (on_recovery_read) on_recovery_read(recovery_read_count);
        return value;
    }
    LONG Change(const std::wstring& device, const DEVMODEW& mode, DWORD flags) {
        requests.push_back({device,mode,flags});
        if (flags == CDS_TEST) { ++validation_calls; return validation_code; }
        if (!target_calls) { ++target_calls; stage = 1; return target_code; }
        ++recovery_calls; stage = 2;
        if (on_recovery) on_recovery();
        return recovery_code;
    }
    ApplyResult Run() {
        DisplayBackend backend({[&] { return Inspect(); },
            [&](const std::wstring& device, const DEVMODEW& mode, DWORD flags) { return Change(device,mode,flags); },
            [&](DWORD milliseconds) { waits.push_back({stage,milliseconds}); if (on_wait) on_wait(stage); }});
        return backend.Apply(saved,target_hz,[&] { return cancelled; });
    }
};
}
int main() {
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) { ++assertions; if (!condition) throw std::runtime_error(message); };
    const auto failed_target = [&](const ApplyResult& result, RecoveryState expected) {
        const auto state_message = "Expected recovery state "+std::to_string(static_cast<int>(expected))+", observed "+
            std::to_string(static_cast<int>(result.recovery))+" after "+std::to_string(assertions)+" assertions: "+Utf8(result.detail);
        check(result.recovery == expected,state_message.c_str());
        check(!result.success && !result.changed && !result.retryable,"Recovery never converts a failed target into success or an automatic retry.");
        check(result.code == DISP_CHANGE_SUCCESSFUL,"The original target interface code is preserved independently.");
        check(result.detail.find(RecoveryStateText(expected)) != std::wstring::npos,"The result detail includes the recovery outcome.");
    };
    const auto mismatch = [&](DisplaySnapshot observed) {
        Fixture test; test.recovery_reads = {std::move(observed)};
        auto result = test.Run();
        failed_target(result,RecoveryState::verification_failed);
        check(test.recovery_calls == 1 && test.recovery_read_count == 4,"A mismatch is read four times without resubmitting the original mode.");
        check(result.recovery_code == DISP_CHANGE_SUCCESSFUL,"A successful recovery submission is not confused with successful verification.");
    };
    try {
        {
            Fixture test; auto result = test.Run();
            failed_target(result,RecoveryState::verified);
            check(test.validation_calls == 1 && test.target_calls == 1 && test.recovery_calls == 1,"Driver validation, target and recovery are each submitted once.");
            check(test.target_read_count == 4 && test.recovery_read_count == 1,"Restoration is verified immediately when the first read matches.");
            check(result.recovery_code == DISP_CHANGE_SUCCESSFUL && result.after.policy.screen->nominal_hz == 240,"The verified original state and recovery code are returned.");
            check(test.requests.back().device == test.saved.device && test.requests.back().mode.dmDisplayFrequency == 240 &&
                test.requests.back().flags == 0,"Restoration submits the original mode to the original device.");
            check(test.waits.size() == 3 && test.waits[0].second == 150,"The existing target verification waits remain bounded.");
        }
        {
            Fixture test; test.recovery_reads = {Snapshot(60,60,240),Snapshot(60,60,240),test.saved};
            auto result = test.Run(); failed_target(result,RecoveryState::verified);
            check(test.recovery_read_count == 3 && test.waits.size() == 5,"Delayed restoration is read immediately and then at 150ms intervals.");
            check(test.recovery_calls == 1,"Delayed restoration never resubmits recovery.");
        }
        {
            Fixture test; test.recovery_code = DISP_CHANGE_FAILED;
            auto result = test.Run(); failed_target(result,RecoveryState::request_failed);
            check(result.recovery_code == DISP_CHANGE_FAILED && test.recovery_read_count == 1,"A rejected recovery preserves its code and observes the final state once.");
            check(result.detail.find(L"返回码 -1") != std::wstring::npos,"The request failure detail includes its independent interface code.");
        }
        {
            Fixture test; test.recovery_reads = {Snapshot(60,60,240)};
            auto result = test.Run(); failed_target(result,RecoveryState::verification_failed);
            check(test.recovery_read_count == 4 && test.waits.size() == 6 && test.recovery_calls == 1,"Recovery has four reads and only three additional 150ms waits.");
            check(result.after.policy.screen->desktop_hz == 60,"An unverified result retains the last observed state.");
        }
        {
            Fixture test; auto error = test.saved; error.policy.availability = Availability::error; error.policy.screen.reset();
            error.error = ERROR_GEN_FAILURE; error.detail = L"模拟读取失败"; test.recovery_reads = {error};
            auto result = test.Run(); failed_target(result,RecoveryState::verification_failed);
            check(test.recovery_read_count == 4 && result.after.error == ERROR_GEN_FAILURE,"Transient query errors use the bounded read budget and retain their error.");
            check(result.detail.find(error.detail) != std::wstring::npos,"Unreadable recovery state is described rather than declared restored.");
            test = Fixture{}; test.recovery_reads = {error,test.saved};
            result = test.Run(); failed_target(result,RecoveryState::verified);
            check(test.recovery_read_count == 2,"A transient query error may recover on the next read.");
        }
        {
            Fixture test; test.on_target_read = [&](int count) { if (count == 4) test.cancelled = true; };
            auto result = test.Run(); failed_target(result,RecoveryState::cancelled);
            check(test.recovery_calls == 0 && !result.recovery_code,"Cancellation before restoration prevents the native recovery request.");
            check(result.detail.find(L"未请求恢复") != std::wstring::npos,"Cancellation before submission says recovery was not requested.");
        }
        {
            Fixture test; test.on_recovery = [&] { test.cancelled = true; };
            auto result = test.Run(); failed_target(result,RecoveryState::cancelled);
            check(test.recovery_calls == 1 && test.recovery_read_count == 0 && result.recovery_code == 0,"Cancellation after submission retains its code and does not claim verification.");
            check(result.detail.find(L"未请求恢复") == std::wstring::npos,"An already submitted recovery is not mislabeled as never requested.");
        }
        {
            Fixture test; test.recovery_reads = {Snapshot(60,60,240)};
            test.on_wait = [&](int stage) { if (stage == 2) test.cancelled = true; };
            auto result = test.Run(); failed_target(result,RecoveryState::cancelled);
            check(test.recovery_read_count == 1 && test.recovery_calls == 1,"Cancellation during a recovery wait stops further reads and writes.");
        }
        {
            Fixture test; test.on_recovery_read = [&](int) { test.cancelled = true; };
            auto result = test.Run(); failed_target(result,RecoveryState::cancelled);
            check(test.recovery_read_count == 1,"Cancellation during inspection prevents a stale success result.");
        }
        for (int change = 0; change < 7; ++change) {
            Fixture test; auto changed = test.target_reads.front();
            if (change == 0) changed.policy.screen->route = L"another|source";
            if (change == 1) changed.policy.screen->supported_hz = {60,120};
            if (change == 2) changed.device = L"another-display";
            if (change == 3) changed.paths.pop_back();
            if (change == 4) changed.paths.back().identity = L"another-outer";
            if (change == 5) { changed.policy.availability = Availability::absent; changed.policy.screen.reset(); }
            if (change == 6) changed.policy.screen->dynamic_refresh = true;
            test.target_reads = {changed};
            auto result = test.Run(); failed_target(result,RecoveryState::environment_changed);
            check(test.recovery_calls == 0 && !result.recovery_code,"Changed output, capabilities, topology or session never receive the stale original mode.");
        }
        for (bool topology : {false,true}) {
            Fixture test; auto changed = test.saved;
            if (topology) changed.paths.pop_back(); else changed.policy.screen->capability_key += L"|new";
            test.recovery_reads = {changed};
            auto result = test.Run(); failed_target(result,RecoveryState::environment_changed);
            check(test.recovery_read_count == 1 && test.recovery_calls == 1 && result.recovery_code == 0,"An environment change during restoration terminates verification without a second request.");
        }
        for (double rate : {59.94,119.88,143.998}) {
            Fixture test; const int lower = static_cast<int>(std::floor(rate)), upper = static_cast<int>(std::round(rate));
            test.target_hz = 240; test.saved = Snapshot(lower,lower,rate);
            auto restored = Snapshot(upper,upper,rate); test.target_reads = {Snapshot(240,60,rate)}; test.recovery_reads = {restored};
            auto result = test.Run(); failed_target(result,RecoveryState::verified);
            check(test.requests.back().mode.dmDisplayFrequency == static_cast<DWORD>(lower),"Restoration requests the original nominal label, while readback accepts proven driver rounding.");
            test.saved = Snapshot(upper,rate,rate); test.recovery_reads = {test.saved};
            test.stage = 0; test.target_calls = test.recovery_calls = 0;
            result = test.Run(); failed_target(result,RecoveryState::verified);
        }
        for (int desktop : {30,48,120}) {
            Fixture test; const int physical = desktop == 120 ? 240 : 60;
            test.saved = Snapshot(desktop,desktop,physical); test.target_hz = desktop == 120 ? 60 : 240;
            test.target_reads = {Snapshot(test.target_hz,test.target_hz,test.target_hz == 60 ? 240 : 60)};
            test.recovery_reads = {test.saved};
            auto result = test.Run(); failed_target(result,RecoveryState::verified);
            check(result.after.policy.screen->desktop_hz == desktop && result.after.policy.screen->physical_hz == physical,"The original virtual desktop and faster physical signal are restored independently.");
            test = Fixture{}; test.saved = Snapshot(desktop,desktop,physical); test.target_hz = desktop == 120 ? 60 : 240;
            test.target_reads = {Snapshot(test.target_hz,test.target_hz,test.target_hz == 60 ? 240 : 60)};
            test.recovery_reads = {Snapshot(physical,physical,physical)};
            result = test.Run(); failed_target(result,RecoveryState::verification_failed);
        }
        for (int field = 0; field < 10; ++field) {
            auto changed = Snapshot();
            if (field == 0) ++changed.current.dmPelsWidth;
            if (field == 1) ++changed.current.dmPelsHeight;
            if (field == 2) changed.current.dmBitsPerPel = 16;
            if (field == 3) changed.current.dmDisplayOrientation = DMDO_90;
            if (field == 4) ++changed.current.dmDisplayFlags;
            if (field == 5) ++changed.current.dmDisplayFixedOutput;
            if (field == 6) ++changed.paths.front().scaling;
            if (field == 7) changed.paths.front().advanced_color = true;
            if (field == 8) ++changed.paths.front().x;
            if (field == 9) changed.paths.back().hz = 120;
            mismatch(changed);
        }
        for (double invalid : {0.0,1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            auto changed = Snapshot(); changed.policy.screen->physical_hz = invalid; mismatch(changed);
            changed = Snapshot(); changed.policy.screen->desktop_hz = invalid; mismatch(changed);
            for (bool desktop : {false,true}) {
                Fixture test;
                (desktop ? test.saved.policy.screen->desktop_hz : test.saved.policy.screen->physical_hz) = invalid;
                test.recovery_reads = {test.saved};
                failed_target(test.Run(),RecoveryState::verification_failed);
            }
        }
        mismatch(Snapshot(0,240,240));
        mismatch(Snapshot(240,240,239.8));
        {
            Fixture test; test.saved = Snapshot(60,60,60); test.target_hz = 240; test.target_reads = {Snapshot(240,240,60)};
            test.recovery_reads = {Snapshot(59,59,59)};
            auto result = test.Run(); failed_target(result,RecoveryState::verification_failed);
            test = Fixture{}; test.saved = Snapshot(60,60,60); test.target_hz = 240; test.target_reads = {Snapshot(240,240,60)};
            test.recovery_reads = {Snapshot(60,59,60)};
            result = test.Run(); failed_target(result,RecoveryState::verification_failed);
            test = Fixture{}; test.saved = Snapshot(60,60,60); test.target_hz = 240; test.target_reads = {Snapshot(240,240,60)};
            test.recovery_reads = {Snapshot(59,59,60)};
            result = test.Run(); failed_target(result,RecoveryState::verification_failed);
        }
        {
            Fixture test; test.recovery_reads = {Snapshot(240,240,239.95)};
            auto result = test.Run(); failed_target(result,RecoveryState::verified);
            check(result.after.policy.screen->physical_hz == 239.95,"Small actual-signal differences within the 0.1Hz tolerance are accepted.");
        }
        {
            Fixture test; test.target_reads = {Snapshot(60,60,60)};
            auto result = test.Run();
            check(result.success && result.changed && result.recovery == RecoveryState::not_needed && !result.recovery_code,
                "Successful target application retains normal behavior without restoration.");
            check(test.recovery_calls == 0,"A successful target never submits recovery.");
            test = Fixture{}; test.target_hz = 240; result = test.Run();
            check(result.success && !result.changed && test.target_calls == 0 && test.recovery_calls == 0,"An already matching target avoids writes and recovery.");
            test = Fixture{}; test.validation_code = DISP_CHANGE_BADMODE; result = test.Run();
            check(!result.success && result.code == DISP_CHANGE_BADMODE && test.target_calls == 0 && test.recovery_calls == 0,"Rejected driver validation never applies or restores.");
            test = Fixture{}; test.target_code = DISP_CHANGE_FAILED; result = test.Run();
            check(!result.success && result.retryable && result.recovery == RecoveryState::not_needed && !result.recovery_code,
                "Existing retry behavior for a rejected target submission is preserved.");
        }
        std::cout << "PASS: " << assertions << " recovery assertions using the real backend flow with injected Windows operations.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
