#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace rrs {
// Public Windows graphics-kernel mode metadata, independent of a laptop brand.
struct PhysicalDisplayMode {
    UINT width, height, format, hz;
    DISPLAYCONFIG_RATIONAL refresh;
    UINT scanline, rotation, fixed_output;
    bool physical, virtual_rate;
};
struct PhysicalModeList {
    bool available = false;
    LONG error = 0;
    std::vector<PhysicalDisplayMode> modes;
};
enum class ModeOrigin { physical, virtual_rate, unverified };
PhysicalModeList ReadPhysicalModes(const std::wstring& device);
ModeOrigin ClassifyRefreshRate(const PhysicalModeList& modes, const DEVMODEW& current, int hz);
}
