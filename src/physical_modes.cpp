#include "physical_modes.hpp"
#include "policy.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>

namespace rrs {
namespace {
// ABI of d3dkmthk.h from the Windows SDK. LLVM-MinGW does not ship that header.
// Flags occupy two UINTs: the first has monitor-validation flags; the second
// has PhysicalModeSupported (bit 7) and VirtualRefreshRate (bit 8).
// https://github.com/microsoft/libdxg/blob/main/include/dxg/d3dkmthk.h
struct OpenAdapter { WCHAR device[32]; UINT adapter; LUID luid; UINT source; };
struct NativeMode {
    UINT width, height, format, hz;
    DISPLAYCONFIG_RATIONAL refresh;
    UINT scanline, rotation, fixed_output, flags[2];
};
struct ModeList { UINT adapter, source; NativeMode* modes; UINT count; };
struct CloseAdapter { UINT adapter; };
static_assert(sizeof(OpenAdapter) == 80 && offsetof(OpenAdapter,adapter) == 64);
static_assert(sizeof(NativeMode) == 44 && offsetof(NativeMode,flags) == 36);
static_assert(sizeof(void*) == 8 && sizeof(ModeList) == 24 && offsetof(ModeList,modes) == 8);
constexpr LONG kBufferTooSmall = static_cast<LONG>(0xc0000023u);
constexpr UINT kPhysical = 1u << 7, kVirtual = 1u << 8;
UINT BitsPerPixel(UINT format) {
    // D3DDDIFORMAT RGB/paletted display formats; unknown formats cannot certify
    // a physical mode for the current GDI color depth.
    switch (format) {
    case 20: return 24;
    case 21: case 22: case 31: case 32: case 33: case 35: case 119: return 32;
    case 23: case 24: case 25: case 26: case 29: case 30: case 40: return 16;
    case 27: case 41: return 8;
    case 36: case 113: return 64;
    default: return 0;
    }
}
}
PhysicalModeList ReadPhysicalModes(const std::wstring& device) {
    PhysicalModeList result;
    if (device.empty() || device.size() >= 32) { result.error = ERROR_INVALID_PARAMETER; return result; }
    struct Library {
        HMODULE module = LoadLibraryExW(L"gdi32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        ~Library() { if (module) FreeLibrary(module); }
    } library;
    if (!library.module) { result.error = static_cast<LONG>(GetLastError()); return result; }
    const auto open = reinterpret_cast<LONG(WINAPI*)(OpenAdapter*)>(GetProcAddress(library.module,"D3DKMTOpenAdapterFromGdiDisplayName"));
    const auto read = reinterpret_cast<LONG(WINAPI*)(ModeList*)>(GetProcAddress(library.module,"D3DKMTGetDisplayModeList"));
    const auto close = reinterpret_cast<LONG(WINAPI*)(const CloseAdapter*)>(GetProcAddress(library.module,"D3DKMTCloseAdapter"));
    if (!open || !read || !close) { result.error = ERROR_PROC_NOT_FOUND; return result; }
    OpenAdapter adapter{}; std::copy(device.begin(),device.end(),adapter.device);
    result.error = open(&adapter);
    if (result.error < 0) return result;
    struct AdapterGuard {
        UINT adapter; LONG(WINAPI* close)(const CloseAdapter*);
        ~AdapterGuard() { const CloseAdapter item{adapter}; close(&item); }
    } guard{adapter.adapter,close};
    ModeList list{adapter.adapter,adapter.source,nullptr,0};
    result.error = read(&list);
    if (result.error < 0 && result.error != kBufferTooSmall) return result;
    std::vector<NativeMode> buffer;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!list.count || list.count > 65536) { result.error = ERROR_INVALID_DATA; return result; }
        buffer.resize(list.count); list.modes = buffer.data();
        result.error = read(&list);
        if (result.error == kBufferTooSmall) continue;
        if (result.error < 0) return result;
        if (list.count > buffer.size()) { result.error = ERROR_INVALID_DATA; return result; }
        result.modes.reserve(list.count);
        for (UINT i = 0; i < list.count; ++i) {
            const auto& mode = buffer[i];
            result.modes.push_back({mode.width,mode.height,mode.format,mode.hz,mode.refresh,mode.scanline,mode.rotation,
                mode.fixed_output,(mode.flags[1] & kPhysical) != 0,(mode.flags[1] & kVirtual) != 0});
        }
        result.available = true; result.error = 0; return result;
    }
    return result;
}
ModeOrigin ClassifyRefreshRate(const PhysicalModeList& modes, const DEVMODEW& current, int hz) {
    if (!modes.available || hz <= 1) return ModeOrigin::unverified;
    bool virtual_rate = false;
    for (const auto& mode : modes.modes) {
        const bool interlaced = mode.scanline == 2 || mode.scanline == 3;
        if (mode.width != current.dmPelsWidth || mode.height != current.dmPelsHeight ||
            BitsPerPixel(mode.format) != current.dmBitsPerPel || mode.rotation != current.dmDisplayOrientation+1 ||
            mode.fixed_output != current.dmDisplayFixedOutput || mode.scanline == 0 ||
            interlaced != ((current.dmDisplayFlags & DM_INTERLACED) != 0) ||
            mode.hz <= 1 || mode.hz > static_cast<UINT>(std::numeric_limits<int>::max())) continue;
        const double actual = mode.refresh.Denominator ? static_cast<double>(mode.refresh.Numerator)/mode.refresh.Denominator : 0;
        if (!RateMatches(static_cast<int>(mode.hz),actual,hz)) continue;
        if (mode.physical && !mode.virtual_rate) return ModeOrigin::physical;
        virtual_rate |= mode.virtual_rate;
    }
    return virtual_rate ? ModeOrigin::virtual_rate : ModeOrigin::unverified;
}
}
