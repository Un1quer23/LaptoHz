#include "display.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    using namespace rrs;
    DisplayBackend backend;
    const auto original = backend.Inspect();
    if (original.policy.availability != Availability::ready || !original.policy.screen) {
        std::cout << "SKIP: no active internal display in this session.\n"; return 77;
    }
    std::cout << DiagnosticJson(original, backend);
    if (original.policy.screen->cloned || original.policy.screen->dynamic_refresh) {
        std::cout << "SKIP: requires fixed refresh rate and an independent internal source.\n"; return 77;
    }
    if (original.policy.screen->supported_hz.empty()) { std::cout << "SKIP: no validated refresh-rate modes.\n"; return 77; }
    for (const int hz : original.policy.screen->supported_hz)
        if (backend.Validate(original,hz) != DISP_CHANGE_SUCCESSFUL) { std::cerr << "FAIL: validated rate rejected by driver.\n"; return 1; }
    if (argc < 2 || std::string(argv[1]) != "--switch") {
        std::cout << "PASS: read-only display identification and driver validation.\n"; return 0;
    }
    int code = 0;
    for (const int hz : original.policy.screen->supported_hz) {
        const auto result = backend.Apply(backend.Inspect(), hz, [] { return false; });
        std::cout << "Requested " << hz << "Hz; success=" << result.success << "; changed=" << result.changed
                  << "; detail=" << Utf8(result.detail) << '\n';
        if (!result.success) { code = 1; break; }
        std::cout << DiagnosticJson(result.after, backend);
    }
    // Restore the original actual mode rather than inventing a power rule.
    auto current = backend.Inspect();
    const int restore = original.policy.screen->nominal_hz;
    if (CheckTarget(current.policy,restore).reason == Reason::none) {
        const auto result = backend.Apply(current, restore, [] { return false; });
        std::cout << "Restore " << restore << "Hz: " << result.success << '\n';
        if (!result.success) code = 1;
    } else code = 1;
    return code;
}
