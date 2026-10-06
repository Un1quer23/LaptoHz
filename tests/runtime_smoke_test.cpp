#include "display.hpp"
#include <iostream>
#include <stdexcept>

using namespace rrs;
namespace {
bool WaitRate(const DisplayBackend& backend, int hz, int milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        const auto snapshot = backend.Inspect();
        if (snapshot.policy.screen && RateMatches(*snapshot.policy.screen,hz)) return true;
        Sleep(100);
    } while (GetTickCount64() < deadline);
    return false;
}
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
}
int main() {
    DisplayBackend backend;
    HWND host = FindWindowW(kWindowClass, kAppName);
    if (!host || ReadPower() != PowerSource::ac) {
        std::cout << "SKIP: requires the running application and stable AC power.\n"; return 77;
    }
    const auto initial = backend.Inspect();
    if (backend.Validate(initial,60) != DISP_CHANGE_SUCCESSFUL || backend.Validate(initial,240) != DISP_CHANGE_SUCCESSFUL ||
        TargetRate(initial.policy,LoadRefreshTargets(DataDirectory())) != 240) {
        std::cout << "SKIP: this legacy ThinkBook-specific runtime check requires 60/240Hz and the 240Hz AC target.\n"; return 77;
    }
    try {
        PostMessageW(host, WM_APP + 10, 1, 0);
        Require(WaitRate(backend, 240, 5000), "Startup should select 240Hz on AC.");
        std::cout << "PASS: startup is at 240Hz.\n";
        auto manual = backend.Apply(backend.Inspect(), 60, [] { return false; });
        Require(manual.success, "Manual test switch to 60Hz failed.");
        Sleep(1300);
        Require(WaitRate(backend, 60, 100), "Display-change event should preserve manual 60Hz.");
        std::cout << "PASS: manual refresh rate survives display-change event.\n";
        PostMessageW(host, WM_POWERBROADCAST, PBT_APMPOWERSTATUSCHANGE, 0);
        Sleep(1100);
        Require(WaitRate(backend, 60, 100), "Duplicate power notification should preserve manual override.");
        std::cout << "PASS: duplicate power notification does not override manual rate.\n";
        PostMessageW(host, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        Require(WaitRate(backend, 240, 5000), "Resume notification should reconcile to 240Hz.");
        std::cout << "PASS: resume notification applies 240Hz.\n";
        PostMessageW(host, WM_APP + 10, 0, 0);
        Sleep(300);
        Require(backend.Apply(backend.Inspect(), 60, [] { return false; }).success, "Paused manual switch failed.");
        PostMessageW(host, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
        PostMessageW(host, WM_APP + 10, 0, 0);
        Sleep(1300);
        Require(WaitRate(backend, 60, 100), "Paused app must not apply a resume rule; repeated pause must be idempotent.");
        std::cout << "PASS: pause prevents switching, including resume and repeated pause.\n";
        PostMessageW(host, WM_APP + 10, 1, 0);
        Require(WaitRate(backend, 240, 5000), "Enabling automatic mode should select 240Hz.");
        std::cout << "PASS: enabling automatic mode immediately reconciles current power.\n";
        return 0;
    } catch (const std::exception& error) {
        PostMessageW(host, WM_APP + 10, 1, 0);
        const auto current = backend.Inspect();
        backend.Apply(current,TargetRate(current.policy,LoadRefreshTargets(DataDirectory())),[] { return false; });
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
