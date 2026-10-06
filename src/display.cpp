#include "display.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <cstring>

namespace rrs {
namespace {
constexpr UINT32 kQueryFlags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE | 0x40;
constexpr UINT32 kBoostRefreshRate = 0x10;
std::wstring AdapterKey(const LUID& id) {
    return std::to_wstring(id.HighPart) + L":" + std::to_wstring(id.LowPart);
}
std::wstring SourceKey(const DISPLAYCONFIG_PATH_INFO& path) {
    return AdapterKey(path.sourceInfo.adapterId) + L":" + std::to_wstring(path.sourceInfo.id);
}
std::wstring TargetKey(const DISPLAYCONFIG_PATH_INFO& path) {
    return AdapterKey(path.targetInfo.adapterId) + L":" + std::to_wstring(path.targetInfo.id);
}
bool IsInternal(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology) {
    return technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS ||
           technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
}
double Ratio(DISPLAYCONFIG_RATIONAL rate) {
    return rate.Denominator ? static_cast<double>(rate.Numerator) / rate.Denominator : 0;
}
UINT32 ModeIndex(const DISPLAYCONFIG_PATH_INFO& path, bool target) {
    const UINT32 packed = target ? path.targetInfo.modeInfoIdx : path.sourceInfo.modeInfoIdx;
    return (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) ? packed >> 16 : packed;
}
std::optional<bool> AdvancedColor(const DISPLAYCONFIG_PATH_INFO& path) {
    struct ColorInfo {
        DISPLAYCONFIG_DEVICE_INFO_HEADER header;
        UINT32 flags;
        UINT32 encoding;
        UINT32 bits_per_channel;
    } color{};
    color.header.type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(9);
    color.header.size = sizeof(color);
    color.header.adapterId = path.targetInfo.adapterId;
    color.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS) return std::nullopt;
    return (color.flags & 2) != 0;
}
std::wstring ChangeError(LONG code) {
    switch (code) {
    case DISP_CHANGE_SUCCESSFUL: return L"成功";
    case DISP_CHANGE_RESTART: return L"驱动要求重新启动，未进行自动切换";
    case DISP_CHANGE_BADMODE: return L"驱动不支持目标模式";
    case DISP_CHANGE_FAILED: return L"显示驱动暂时无法切换";
    case DISP_CHANGE_BADPARAM: return L"显示参数无效";
    case DISP_CHANGE_BADFLAGS: return L"显示参数标志无效";
    case DISP_CHANGE_BADDUALVIEW: return L"当前多屏模式不支持独立切换";
    default: return L"显示接口返回 " + std::to_wstring(code);
    }
}
DEVMODEW TargetMode(const DisplaySnapshot& snapshot, int hz) {
    DEVMODEW mode = snapshot.current;
    mode.dmSize = sizeof(mode);
    mode.dmDriverExtra = 0;
    mode.dmDisplayFrequency = static_cast<DWORD>(hz);
    mode.dmFields |= DM_DISPLAYFREQUENCY;
    return mode;
}
bool SameSurroundings(const DisplaySnapshot& before, const DisplaySnapshot& after) {
    if (before.paths.size() != after.paths.size() || before.current.dmPelsWidth != after.current.dmPelsWidth ||
        before.current.dmPelsHeight != after.current.dmPelsHeight || before.current.dmBitsPerPel != after.current.dmBitsPerPel ||
        before.current.dmDisplayOrientation != after.current.dmDisplayOrientation ||
        before.current.dmDisplayFlags != after.current.dmDisplayFlags ||
        before.current.dmDisplayFixedOutput != after.current.dmDisplayFixedOutput) return false;
    for (size_t i = 0; i < before.paths.size(); ++i) {
        auto old_path = before.paths[i], new_path = after.paths[i];
        // Only the internal target's refresh rate is allowed to change.
        if (old_path.identity == before.policy.screen->route.substr(0, before.policy.screen->route.find(L'|'))) {
            old_path.hz = new_path.hz;
            old_path.desktop_hz = new_path.desktop_hz;
        }
        if (!(old_path == new_path)) return false;
    }
    return true;
}
bool CloseRate(double before, double after) {
    return std::isfinite(before) && std::isfinite(after) && before > 1 && after > 1 && std::abs(before-after) < 0.1;
}
bool SameRecoveryEnvironment(const DisplaySnapshot& before, const DisplaySnapshot& after) {
    if (!before.policy.screen || !after.policy.screen || before.device != after.device ||
        CapabilityKey(before.policy) != CapabilityKey(after.policy) || before.paths.size() != after.paths.size()) return false;
    for (size_t i = 0; i < before.paths.size(); ++i)
        if (before.paths[i].identity != after.paths[i].identity) return false;
    return true;
}
bool OriginalModeRestored(const DisplaySnapshot& before, const DisplaySnapshot& after) {
    if (!SameRecoveryEnvironment(before,after) || !SameSurroundings(before,after)) return false;
    const auto& original = *before.policy.screen;
    const auto& observed = *after.policy.screen;
    if (!CloseRate(original.physical_hz,observed.physical_hz)) return false;
    const int nominal_label = std::max(original.nominal_hz,observed.nominal_hz);
    if (original.nominal_hz <= 1 || observed.nominal_hz <= 1 ||
        (original.nominal_hz != observed.nominal_hz &&
         !(RateMatches(original.nominal_hz,original.physical_hz,nominal_label) &&
           RateMatches(observed.nominal_hz,observed.physical_hz,nominal_label)))) return false;
    if (CloseRate(original.desktop_hz,observed.desktop_hz)) return true;
    // CCD can round the same fractional timing to neighboring integer labels.
    // Require fractional signal evidence on both snapshots; a virtual desktop
    // rate must not be substituted with its faster physical signal.
    const auto integer_rate = [](double hz) {
        return std::isfinite(hz) && hz > 1 && hz <= std::numeric_limits<int>::max() && std::abs(hz-std::round(hz)) < 0.01;
    };
    if (!integer_rate(original.desktop_hz) || !integer_rate(observed.desktop_hz)) return false;
    const int old_desktop = static_cast<int>(std::round(original.desktop_hz));
    const int new_desktop = static_cast<int>(std::round(observed.desktop_hz));
    const int desktop_label = std::max(old_desktop,new_desktop);
    return RateMatches(old_desktop,original.physical_hz,desktop_label) &&
           RateMatches(new_desktop,observed.physical_hz,desktop_label);
}
std::wstring ObservedState(const DisplaySnapshot& snapshot) {
    if (snapshot.policy.availability != Availability::ready || !snapshot.policy.screen)
        return snapshot.detail.empty() ? L"当前显示状态不可读" : snapshot.detail;
    const auto& screen = *snapshot.policy.screen;
    std::wostringstream text;
    text << L"当前标称 " << screen.nominal_hz << L"Hz，桌面 " << screen.desktop_hz << L"Hz，信号 " << screen.physical_hz << L"Hz";
    return text.str();
}
std::wstring FirmwareString(const unsigned char* begin, const unsigned char* end, unsigned int index) {
    if (!index) return L"";
    unsigned int current = 1;
    for (const auto* position = begin; position < end && *position;) {
        const auto* finish = position;
        while (finish < end && *finish) ++finish;
        if (current++ == index) {
            const auto* value = reinterpret_cast<const char*>(position);
            const int count = static_cast<int>(finish-position);
            UINT page = CP_UTF8;
            int length = MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,value,count,nullptr,0);
            if (!length) { page = CP_ACP; length = MultiByteToWideChar(page,0,value,count,nullptr,0); }
            std::wstring decoded(length,L'\0');
            if (length) MultiByteToWideChar(page,0,value,count,decoded.data(),length);
            return decoded;
        }
        position = finish < end ? finish+1 : end;
    }
    return L"";
}
std::pair<std::wstring,std::wstring> ComputerIdentity() {
    constexpr DWORD provider = 0x52534d42; // RSMB, Windows raw SMBIOS provider.
    const UINT size = GetSystemFirmwareTable(provider,0,nullptr,0);
    if (size < 8) return {};
    std::vector<unsigned char> buffer(size);
    const UINT received = GetSystemFirmwareTable(provider,0,buffer.data(),size);
    if (received < 8 || received > size) return {};
    DWORD length = 0; std::memcpy(&length,buffer.data()+4,sizeof(length));
    if (length > received-8) return {};
    const auto* end = buffer.data()+8+length;
    for (const auto* entry = buffer.data()+8; entry+4 <= end;) {
        const unsigned int formatted = entry[1];
        if (formatted < 4 || static_cast<size_t>(end-entry) < formatted) break;
        const auto* strings = entry+formatted;
        const auto* finish = strings;
        while (finish+1 < end && !(finish[0] == 0 && finish[1] == 0)) ++finish;
        if (finish+1 >= end) break;
        if (entry[0] == 1 && formatted >= 6)
            return {FirmwareString(strings,finish+1,entry[4]),FirmwareString(strings,finish+1,entry[5])};
        if (entry[0] == 127) break;
        entry = finish+2;
    }
    return {};
}
}
std::wstring RecoveryStateText(RecoveryState state) {
    switch (state) {
    case RecoveryState::not_needed: return L"无需恢复";
    case RecoveryState::verified: return L"已恢复原显示状态并验证";
    case RecoveryState::request_failed: return L"恢复原显示状态的请求失败";
    case RecoveryState::verification_failed: return L"恢复请求已提交，回读未通过验证";
    case RecoveryState::cancelled: return L"操作已取消，恢复结果未验证";
    case RecoveryState::environment_changed: return L"显示环境已变化或不可确认，恢复结果未验证";
    }
    return L"恢复结果未知";
}
LONG DisplayBackend::ChangeSettings(const std::wstring& device, DEVMODEW mode, DWORD flags) const {
    return services_.change_settings ? services_.change_settings(device,mode,flags) :
        ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,flags,nullptr);
}
void DisplayBackend::Wait(DWORD milliseconds) const {
    if (services_.wait) services_.wait(milliseconds); else Sleep(milliseconds);
}
DisplaySnapshot DisplayBackend::Inspect() const {
    if (services_.inspect) return services_.inspect();
    DisplaySnapshot result;
    result.policy.power = ReadPower();
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        result.policy.availability = Availability::remote;
        result.detail = ReasonText(Reason::remote_session);
        return result;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG rc = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 3 && rc == ERROR_INSUFFICIENT_BUFFER; ++attempt) {
        UINT32 path_count = 0, mode_count = 0;
        rc = GetDisplayConfigBufferSizes(kQueryFlags, &path_count, &mode_count);
        if (rc != ERROR_SUCCESS) break;
        if (!path_count) { result.policy.availability = Availability::absent; return result; }
        paths.resize(path_count);
        modes.resize(mode_count);
        rc = QueryDisplayConfig(kQueryFlags, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
        if (rc == ERROR_SUCCESS) { paths.resize(path_count); modes.resize(mode_count); }
    }
    if (rc != ERROR_SUCCESS) {
        result.policy.availability = rc == ERROR_ACCESS_DENIED ? Availability::remote : Availability::error;
        result.error = rc;
        result.detail = NativeError(rc);
        return result;
    }
    std::vector<size_t> internal;
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto& path = paths[i];
        if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !path.targetInfo.targetAvailable) continue;
        PathState state;
        state.identity = TargetKey(path);
        state.rotation = path.targetInfo.rotation;
        state.scaling = path.targetInfo.scaling;
        state.advanced_color = AdvancedColor(path);
        state.desktop_hz = Ratio(path.targetInfo.refreshRate);
        const UINT32 target_index = ModeIndex(path, true);
        if (target_index < modes.size() && modes[target_index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            state.hz = Ratio(modes[target_index].targetMode.targetVideoSignalInfo.vSyncFreq);
        const UINT32 source_index = ModeIndex(path, false);
        if (source_index < modes.size() && modes[source_index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            const auto& source = modes[source_index].sourceMode;
            state.width = static_cast<int>(source.width); state.height = static_cast<int>(source.height);
            state.x = source.position.x; state.y = source.position.y;
        }
        result.paths.push_back(state);
        if (IsInternal(path.targetInfo.outputTechnology)) internal.push_back(i);
    }
    std::sort(result.paths.begin(), result.paths.end(), [](const auto& a, const auto& b) { return a.identity < b.identity; });
    if (internal.empty()) { result.policy.availability = Availability::absent; return result; }
    if (internal.size() != 1) { result.policy.availability = Availability::ambiguous; return result; }
    const auto& path = paths[internal.front()];
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name{};
    source_name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source_name), path.sourceInfo.adapterId, path.sourceInfo.id};
    rc = DisplayConfigGetDeviceInfo(&source_name.header);
    if (rc != ERROR_SUCCESS) {
        result.policy.availability = Availability::error; result.error = rc; result.detail = NativeError(rc); return result;
    }
    result.device = source_name.viewGdiDeviceName;
    DISPLAYCONFIG_TARGET_DEVICE_NAME target_name{};
    target_name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target_name), path.targetInfo.adapterId, path.targetInfo.id};
    if (DisplayConfigGetDeviceInfo(&target_name.header) == ERROR_SUCCESS) result.monitor = target_name.monitorFriendlyDeviceName;
    result.current.dmSize = sizeof(result.current);
    if (!EnumDisplaySettingsExW(result.device.c_str(), ENUM_CURRENT_SETTINGS, &result.current, 0)) {
        result.policy.availability = Availability::error; result.error = ERROR_GEN_FAILURE;
        result.detail = L"无法读取内置屏幕的当前显示模式"; return result;
    }
    Screen screen;
    screen.cloned = std::count_if(paths.begin(), paths.end(), [&](const auto& p) {
        return (p.flags & DISPLAYCONFIG_PATH_ACTIVE) && p.targetInfo.targetAvailable && SourceKey(p) == SourceKey(path);
    }) > 1;
    screen.dynamic_refresh = (path.flags & kBoostRefreshRate) != 0;
    screen.route = TargetKey(path) + L"|" + SourceKey(path) + L"|" + result.device +
        (screen.cloned ? L"|clone" : L"|solo") + (screen.dynamic_refresh ? L"|drr" : L"|fixed");
    screen.nominal_hz = static_cast<int>(result.current.dmDisplayFrequency);
    auto internal_state = std::find_if(result.paths.begin(), result.paths.end(), [&](const auto& p) { return p.identity == TargetKey(path); });
    screen.physical_hz = internal_state->hz;
    screen.desktop_hz = internal_state->desktop_hz;
    screen.virtual_mode_supported = (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
    for (DWORD index = 0; ; ++index) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(result.device.c_str(), index, &mode, 0)) break;
        if (mode.dmPelsWidth == result.current.dmPelsWidth && mode.dmPelsHeight == result.current.dmPelsHeight &&
            mode.dmBitsPerPel == result.current.dmBitsPerPel && mode.dmDisplayOrientation == result.current.dmDisplayOrientation &&
            mode.dmDisplayFlags == result.current.dmDisplayFlags && mode.dmDisplayFrequency > 1)
            screen.supported_hz.push_back(static_cast<int>(mode.dmDisplayFrequency));
    }
    std::sort(screen.supported_hz.begin(), screen.supported_hz.end());
    screen.supported_hz.erase(std::unique(screen.supported_hz.begin(), screen.supported_hz.end()), screen.supported_hz.end());
    const auto enumerated = screen.supported_hz;
    screen.supported_hz.clear();
    const auto physical_modes = ReadPhysicalModes(result.device);
    result.physical_modes_known = physical_modes.available;
    result.physical_modes_error = physical_modes.error;
    for (const int hz : enumerated) {
        const auto origin = ClassifyRefreshRate(physical_modes,result.current,hz);
        auto mode = TargetMode(result,hz);
        const bool tested = !screen.cloned && !screen.dynamic_refresh && origin == ModeOrigin::physical;
        const LONG validation = !tested ? DISP_CHANGE_BADMODE :
            ChangeSettings(result.device,mode,CDS_TEST);
        result.rate_validations.push_back({hz,validation,tested,origin});
        if (validation == DISP_CHANGE_SUCCESSFUL) screen.supported_hz.push_back(hz);
    }
    screen.capability_key = std::to_wstring(result.current.dmPelsWidth)+L"x"+std::to_wstring(result.current.dmPelsHeight)+L"|"+
        std::to_wstring(result.current.dmBitsPerPel)+L"|"+std::to_wstring(result.current.dmDisplayOrientation)+L"|"+
        std::to_wstring(result.current.dmDisplayFlags)+L"|"+std::to_wstring(result.current.dmDisplayFixedOutput);
    result.policy.screen = std::move(screen);
    result.policy.availability = physical_modes.available ? Availability::ready : Availability::error;
    if (!physical_modes.available) {
        result.error = physical_modes.error;
        result.detail = L"无法读取内屏的物理刷新率能力，暂不提供切换";
    }
    return result;
}
LONG DisplayBackend::Validate(const DisplaySnapshot& snapshot, int hz) const {
    if (snapshot.policy.availability != Availability::ready || !snapshot.policy.screen || snapshot.policy.screen->cloned ||
        snapshot.policy.screen->dynamic_refresh || hz <= 1)
        return DISP_CHANGE_BADMODE;
    const auto& rates = snapshot.policy.screen->supported_hz;
    if (std::find(rates.begin(), rates.end(), hz) == rates.end()) return DISP_CHANGE_BADMODE;
    auto mode = TargetMode(snapshot, hz);
    return ChangeSettings(snapshot.device,mode,CDS_TEST);
}
ApplyResult DisplayBackend::Apply(const DisplaySnapshot& snapshot, int hz, const std::function<bool()>& cancelled) const {
    ApplyResult result;
    result.after = Inspect();
    auto& before = result.after;
    if (!snapshot.policy.screen || !before.policy.screen || CapabilityKey(before.policy) != CapabilityKey(snapshot.policy)) {
        result.detail = ReasonText(Reason::capabilities_changed); result.code = DISP_CHANGE_BADMODE; return result;
    }
    if (cancelled()) { result.detail = L"操作已取消"; return result; }
    result.code = Validate(before, hz);
    if (result.code != DISP_CHANGE_SUCCESSFUL) {
        result.detail = ChangeError(result.code); result.retryable = result.code == DISP_CHANGE_FAILED; return result;
    }
    if (RateMatches(*before.policy.screen,hz) && RateMatches(before.policy.screen->nominal_hz,before.policy.screen->physical_hz,hz)) {
        result.success = true; return result;
    }
    const DisplaySnapshot saved = before;
    auto mode = TargetMode(saved, hz);
    if (cancelled()) { result.detail = L"操作已取消"; return result; }
    result.code = ChangeSettings(saved.device,mode,0);
    if (result.code != DISP_CHANGE_SUCCESSFUL) {
        result.detail = ChangeError(result.code); result.retryable = result.code == DISP_CHANGE_FAILED;
        result.after = Inspect(); return result;
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (cancelled()) break;
        if (attempt) Wait(150);
        if (cancelled()) break;
        result.after = Inspect();
        if (cancelled()) break;
        if (result.after.policy.screen && result.after.policy.screen->route == saved.policy.screen->route &&
            RateMatches(*result.after.policy.screen,hz) &&
            RateMatches(result.after.policy.screen->nominal_hz,result.after.policy.screen->physical_hz,hz) &&
            SameSurroundings(saved, result.after)) {
            result.success = true; result.changed = true; result.detail = L"刷新率已切换并验证"; return result;
        }
    }
    result.detail = L"切换后的显示状态未通过验证";
    if (result.after.policy.screen && result.after.policy.screen->route == saved.policy.screen->route &&
        !RateMatches(*result.after.policy.screen,hz)) {
        const auto& observed = *result.after.policy.screen;
        std::wostringstream text;
        text << L"刷新率回读不符（目标 " << hz << L"Hz，标称 " << observed.nominal_hz << L"Hz，桌面 "
             << observed.desktop_hz << L"Hz，信号 " << observed.physical_hz << L"Hz）";
        result.detail = text.str();
    }
    const auto finish = [&](RecoveryState state) {
        result.recovery = state;
        const auto target_detail = result.detail;
        result.detail = RecoveryStateText(state);
        if (state == RecoveryState::request_failed)
            result.detail += L"（"+ChangeError(*result.recovery_code)+L"，返回码 "+std::to_wstring(*result.recovery_code)+L"）";
        if (!result.recovery_code && (state == RecoveryState::cancelled || state == RecoveryState::environment_changed))
            result.detail += L"，未请求恢复";
        result.detail += L"\n"+target_detail;
        if (state != RecoveryState::verified) result.detail += L"；"+ObservedState(result.after);
        return result;
    };
    // Submit the original mode only once, while its source and capabilities are
    // still identifiable. Recovery verification never retries the target mode.
    if (cancelled()) return finish(RecoveryState::cancelled);
    if (!SameRecoveryEnvironment(saved,result.after)) return finish(RecoveryState::environment_changed);
    auto rollback = saved.current;
    rollback.dmSize = sizeof(rollback); rollback.dmDriverExtra = 0;
    if (cancelled()) return finish(RecoveryState::cancelled);
    result.recovery_code = ChangeSettings(saved.device,rollback,0);
    if (*result.recovery_code != DISP_CHANGE_SUCCESSFUL) {
        result.after = Inspect();
        return finish(RecoveryState::request_failed);
    }
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (cancelled()) return finish(RecoveryState::cancelled);
        if (attempt) Wait(150);
        if (cancelled()) return finish(RecoveryState::cancelled);
        result.after = Inspect();
        if (cancelled()) return finish(RecoveryState::cancelled);
        // A transient query error can recover on the next bounded read. An
        // identifiable topology/capability change invalidates the old state.
        if (result.after.policy.availability == Availability::error) continue;
        if (!SameRecoveryEnvironment(saved,result.after)) return finish(RecoveryState::environment_changed);
        if (OriginalModeRestored(saved,result.after)) return finish(RecoveryState::verified);
    }
    return finish(RecoveryState::verification_failed);
}
std::string DiagnosticJson(const DisplaySnapshot& snapshot, const DisplayBackend& backend, const RefreshTargets& targets) {
    std::ostringstream out;
    const auto identity = ComputerIdentity();
    out << std::setprecision(8) << "{\n  \"version\": " << JsonString(kVersion) << ",\n  \"power\": " << JsonString(PowerText(snapshot.policy.power))
        << ",\n  \"availability\": " << static_cast<int>(snapshot.policy.availability)
        << ",\n  \"device\": " << JsonString(snapshot.device) << ",\n  \"monitor\": " << JsonString(snapshot.monitor)
        << ",\n  \"resolution\": [" << snapshot.current.dmPelsWidth << ',' << snapshot.current.dmPelsHeight << "]"
        << ",\n  \"bitsPerPixel\": " << snapshot.current.dmBitsPerPel
        << ",\n  \"physicalModesKnown\": " << (snapshot.physical_modes_known ? "true" : "false")
        << ",\n  \"physicalModesError\": " << snapshot.physical_modes_error
        << ",\n  \"computerManufacturer\": " << JsonString(identity.first)
        << ",\n  \"computerModel\": " << JsonString(identity.second)
        << ",\n  \"graphicsAdapters\": [";
    std::set<std::wstring> adapters;
    for (DWORD index = 0; ; ++index) {
        DISPLAY_DEVICEW adapter{}; adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr,index,&adapter,0)) break;
        if (!(adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) && adapter.DeviceString[0]) adapters.insert(adapter.DeviceString);
    }
    bool first_adapter = true;
    for (const auto& adapter : adapters) { if (!first_adapter) out << ','; first_adapter = false; out << JsonString(adapter); }
    const auto configured = [](int hz) { return hz ? std::to_string(hz) : std::string("\"auto\""); };
    const auto resolved = [&](PowerSource source) {
        const int hz = TargetRate(source,snapshot.policy.screen ? snapshot.policy.screen->supported_hz : std::vector<int>{},targets);
        const bool available = hz && CheckTarget(snapshot.policy,hz).reason == Reason::none;
        return std::string("{\"hz\":")+(hz ? std::to_string(hz) : "null")+",\"available\":"+(available ? "true" : "false")+"}";
    };
    out << "],\n  \"configuredTargets\": {\"AC\":" << configured(targets.ac) << ",\"Battery\":" << configured(targets.battery) << "}"
        << ",\n  \"resolvedTargets\": {\"AC\":" << resolved(PowerSource::ac) << ",\"Battery\":" << resolved(PowerSource::battery) << "}"
        << ",\n  \"rateValidation\": [";
    for (size_t i = 0; i < snapshot.rate_validations.size(); ++i) {
        if (i) out << ',';
        const auto& rate = snapshot.rate_validations[i];
        const auto origin = rate.origin == ModeOrigin::physical ? L"physical" : rate.origin == ModeOrigin::virtual_rate ? L"virtual" : L"unverified";
        const auto detail = rate.tested ? ChangeError(rate.code) : rate.origin == ModeOrigin::virtual_rate ? L"虚拟桌面刷新率，未列为屏幕物理档位" :
            rate.origin == ModeOrigin::unverified ? L"无法确认此档位为当前显示参数下的物理刷新率" :
            ReasonText(snapshot.policy.screen && snapshot.policy.screen->cloned ? Reason::cloned_source : Reason::dynamic_refresh);
        out << "{\"hz\":" << rate.hz << ",\"code\":" << rate.code << ",\"tested\":" << (rate.tested ? "true" : "false") << ",\"available\":"
            << (rate.code == DISP_CHANGE_SUCCESSFUL ? "true" : "false") << ",\"origin\":" << JsonString(origin) << ",\"detail\":" << JsonString(detail) << "}";
    }
    out << ']';
    if (snapshot.policy.screen) {
        const auto& screen = *snapshot.policy.screen;
        out << ",\n  \"nominalHz\": " << screen.nominal_hz << ",\n  \"physicalHz\": " << screen.physical_hz
            << ",\n  \"desktopHz\": " << screen.desktop_hz
            << ",\n  \"virtualModeSupported\": " << (screen.virtual_mode_supported ? "true" : "false")
            << ",\n  \"cloned\": " << (screen.cloned ? "true" : "false")
            << ",\n  \"dynamicRefreshEnabled\": " << (screen.dynamic_refresh ? "true" : "false") << ",\n  \"supportedHz\": [";
        for (size_t i = 0; i < screen.supported_hz.size(); ++i) { if (i) out << ','; out << screen.supported_hz[i]; }
        out << "],\n  \"validation60\": " << backend.Validate(snapshot, 60)
            << ",\n  \"validation240\": " << backend.Validate(snapshot, 240);
    }
    out << ",\n  \"error\": " << snapshot.error << ",\n  \"detail\": " << JsonString(snapshot.detail) << "\n}\n";
    return out.str();
}
}
