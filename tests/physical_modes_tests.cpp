#include "physical_modes.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace rrs;
int main() {
    int assertions = 0;
    const auto check = [&](bool condition, const char* message) { ++assertions; if (!condition) throw std::runtime_error(message); };
    DEVMODEW current{}; current.dmPelsWidth = 2560; current.dmPelsHeight = 1600; current.dmBitsPerPel = 32;
    const auto mode = [](UINT hz, bool virtual_rate = false) {
        return PhysicalDisplayMode{2560,1600,22,hz,{hz,1},1,1,0,true,virtual_rate};
    };
    try {
        for (const auto& rates : {std::vector<int>{60,90},{60,120},{60,144},{60,165},{60,240},{120,165},{240},{30,48,60,240}}) {
            PhysicalModeList list; list.available = true;
            for (const int hz : rates) list.modes.push_back(mode(hz));
            for (const int hz : rates) check(ClassifyRefreshRate(list,current,hz) == ModeOrigin::physical,"Actual panel timings are supported without a fixed-rate or brand whitelist.");
            check(ClassifyRefreshRate(list,current,75) == ModeOrigin::unverified,"An absent rate is never invented.");
        }
        PhysicalModeList thinkbook{true,0,{mode(30,true),mode(48,true),mode(60),mode(120,true),mode(240)}};
        for (const int hz : {30,48,120}) check(ClassifyRefreshRate(thinkbook,current,hz) == ModeOrigin::virtual_rate,"A virtual alias remains excluded even with PhysicalModeSupported set.");
        for (const int hz : {60,240}) check(ClassifyRefreshRate(thinkbook,current,hz) == ModeOrigin::physical,"The ThinkBook physical timings remain available.");
        thinkbook.modes.push_back(mode(48));
        check(ClassifyRefreshRate(thinkbook,current,48) == ModeOrigin::physical,"An actual low-rate timing takes precedence over a duplicate virtual alias.");
        auto fractional = mode(59); fractional.refresh = {60000,1001};
        PhysicalModeList fraction{true,0,{fractional}};
        check(ClassifyRefreshRate(fraction,current,60) == ModeOrigin::physical,"59.94Hz can match the driver's rounded 60Hz GDI label.");
        check(ClassifyRefreshRate(fraction,current,59) == ModeOrigin::physical,"The 59Hz GDI label can retain its fractional timing.");
        fraction.modes[0] = mode(119); fraction.modes[0].refresh = {120000,1001};
        check(ClassifyRefreshRate(fraction,current,120) == ModeOrigin::physical,"119.88Hz physical timing survives integer rounding.");
        check(ClassifyRefreshRate(fraction,current,60) == ModeOrigin::unverified,"A multiple of a timing is not itself a supported physical timing.");
        for (int mismatch = 0; mismatch < 8; ++mismatch) {
            auto changed = mode(60);
            switch (mismatch) {
            case 0: changed.width = 1920; break;
            case 1: changed.height = 1080; break;
            case 2: changed.format = 23; break;
            case 3: changed.rotation = 2; break;
            case 4: changed.fixed_output = 1; break;
            case 5: changed.scanline = 2; break;
            case 6: changed.scanline = 0; break;
            case 7: changed.physical = false; break;
            }
            check(ClassifyRefreshRate({true,0,{changed}},current,60) == ModeOrigin::unverified,"Other display parameters and unsupported physical flags cannot certify a rate.");
        }
        auto rotated = mode(144); rotated.rotation = 2; current.dmDisplayOrientation = 1;
        check(ClassifyRefreshRate({true,0,{rotated}},current,144) == ModeOrigin::physical,"Rotation matches the current GDI orientation.");
        auto interlaced = rotated; interlaced.scanline = 2; current.dmDisplayFlags = DM_INTERLACED;
        check(ClassifyRefreshRate({true,0,{interlaced}},current,144) == ModeOrigin::physical,"Interlaced timings only match the corresponding current mode.");
        check(ClassifyRefreshRate({false,ERROR_NOT_SUPPORTED,{interlaced}},current,144) == ModeOrigin::unverified,"A failed capability query cannot silently fall back to virtual desktop rates.");
        check(ClassifyRefreshRate({},current,0) == ModeOrigin::unverified,"Invalid refresh rates are never offered.");
        std::cout << "PASS: " << assertions << " physical mode classification assertions.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
