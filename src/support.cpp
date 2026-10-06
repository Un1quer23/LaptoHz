#include "support.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace rrs {
std::filesystem::path DataDirectory() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value)))
        throw std::runtime_error("Cannot locate LocalAppData.");
    std::filesystem::path path(value);
    CoTaskMemFree(value);
    // Preserve the existing settings, startup preference and diagnostic logs.
    path /= L"RefreshRateSwitcher";
    std::filesystem::create_directories(path);
    return path;
}
std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) throw std::runtime_error("Cannot locate executable.");
    return std::filesystem::path(std::wstring(buffer.data(), count));
}
std::wstring NativeError(LONG code) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = length ? std::wstring(buffer, length) : L"错误代码 " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text;
}
PowerSource ReadPower() {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) return PowerSource::unknown;
    if (status.ACLineStatus == 1) return PowerSource::ac;
    if (status.ACLineStatus == 0) return PowerSource::battery;
    return PowerSource::unknown;
}
std::wstring PowerText(PowerSource power) {
    return power == PowerSource::ac ? L"外部供电" : power == PowerSource::battery ? L"使用电池" : L"电源状态未知";
}
std::wstring ReasonText(Reason reason) {
    switch (reason) {
    case Reason::none: return L"";
    case Reason::paused: return L"自动切换已暂停";
    case Reason::unknown_power: return L"等待电源状态恢复";
    case Reason::no_screen: return L"等待内置屏幕启用";
    case Reason::ambiguous_screen: return L"发现多个内置屏幕，暂不切换";
    case Reason::remote_session: return L"远程或非活动会话，暂不切换";
    case Reason::query_failed: return L"暂时无法读取显示状态";
    case Reason::cloned_source: return L"复制模式暂不切换，请使用扩展或仅内屏模式";
    case Reason::dynamic_refresh: return L"请先在 Windows 高级显示设置中关闭动态刷新率";
    case Reason::no_valid_modes: return L"当前没有通过驱动校验的刷新率档位";
    case Reason::capabilities_changed: return L"显示能力已变化，请按当前档位重新选择";
    case Reason::unsupported_mode: return L"当前显示模式不支持目标刷新率";
    }
    return L"";
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count) return "[invalid text]";
    std::string out(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}
std::string JsonString(const std::wstring& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : Utf8(text)) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
bool WriteText(const std::filesystem::path& file, const std::string& text) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    return stream.good();
}
bool ResolveDiagnosticLogPath(const std::filesystem::path& file, std::filesystem::path& resolved, std::wstring& error) {
    resolved.clear();
    error.clear();
    const HANDLE handle = CreateFileW(file.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = L"日志文件暂不可用 · " + NativeError(GetLastError()) + L" · " + file.wstring(); return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle,&info)) {
        const DWORD code = GetLastError(); CloseHandle(handle);
        error = L"无法读取日志文件信息 · " + NativeError(code) + L" · " + file.wstring(); return false;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        CloseHandle(handle);
        error = L"日志文件暂不可用 · " + file.wstring(); return false;
    }
    // AppData can be redirected by the launching application's environment.
    // An external editor needs the actual file path, not our virtualized name.
    std::vector<wchar_t> buffer(32768,L'\0');
    const DWORD count = GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const DWORD code = count ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
    CloseHandle(handle);
    if (!count || count >= buffer.size()) {
        error = L"无法定位日志的实际路径 · " + NativeError(code) + L" · " + file.wstring(); return false;
    }
    std::wstring value(buffer.data(),count);
    // Ordinary DOS/UNC paths work with more editors. Keep the extended prefix
    // for long paths rather than truncating the file name.
    if (value.starts_with(L"\\\\?\\UNC\\") && value.size()-6 < MAX_PATH) value = L"\\\\" + value.substr(8);
    else if (value.starts_with(L"\\\\?\\") && value.size() > 6 && value[5] == L':' && value[6] == L'\\' &&
        value.size()-4 < MAX_PATH) value.erase(0,4);
    resolved = std::filesystem::path(value);
    return true;
}
bool OpenDiagnosticLog(HWND owner, const std::filesystem::path& file, std::wstring& error) {
    std::filesystem::path resolved;
    if (!ResolveDiagnosticLogPath(file,resolved,error)) return false;
    std::vector<wchar_t> system(32768,L'\0');
    const UINT length = GetSystemDirectoryW(system.data(),static_cast<UINT>(system.size()));
    if (!length || length >= system.size()) { error = L"无法定位系统记事本 · 日志位置：" + resolved.wstring(); return false; }
    const auto notepad = std::filesystem::path(std::wstring(system.data(),length)) / L"notepad.exe";
    const auto parameters = L"\"" + resolved.wstring() + L"\"";
    const auto directory = resolved.parent_path().wstring();
    // Modern Notepad may delegate to a packaged app. Initialize STA for Shell
    // activation and use the system launcher instead of an App Paths alias.
    const HRESULT apartment = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        error = L"记事本启动初始化失败 · " + NativeError(apartment) + L" · 日志位置：" + resolved.wstring(); return false;
    }
    SHELLEXECUTEINFOW execute{}; execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    execute.hwnd = owner; execute.lpVerb = L"open"; execute.lpFile = notepad.c_str();
    execute.lpParameters = parameters.c_str(); execute.lpDirectory = directory.c_str(); execute.nShow = SW_SHOWNORMAL;
    const bool opened = ShellExecuteExW(&execute) != FALSE;
    const DWORD code = opened ? ERROR_SUCCESS : GetLastError();
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (!opened) error = L"无法启动系统记事本 · " + NativeError(code) + L" · 日志位置：" + resolved.wstring();
    return opened;
}
Logger::Logger(const std::filesystem::path& directory) : file_(directory / L"switcher.log") {}
void Logger::Write(const std::wstring& text) {
    std::lock_guard lock(mutex_);
    std::error_code error;
    if (std::filesystem::exists(file_, error) && std::filesystem::file_size(file_, error) >= 256 * 1024) {
        auto previous = file_.parent_path() / L"switcher.previous.log";
        std::filesystem::remove(previous, error);
        std::filesystem::rename(file_, previous, error);
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::ostringstream line;
    line << std::setfill('0') << std::setw(4) << now.wYear << '-' << std::setw(2) << now.wMonth << '-'
         << std::setw(2) << now.wDay << ' ' << std::setw(2) << now.wHour << ':' << std::setw(2) << now.wMinute
         << ':' << std::setw(2) << now.wSecond << ' ' << Utf8(text) << '\n';
    std::ofstream stream(file_, std::ios::binary | std::ios::app);
    stream << line.str();
    OutputDebugStringW((text + L"\n").c_str());
}
bool StartupInitialized(const std::filesystem::path& directory) {
    return GetPrivateProfileIntW(L"App", L"StartupInitialized", 0, (directory / L"settings.ini").c_str()) != 0;
}
bool MarkStartupInitialized(const std::filesystem::path& directory) {
    return WritePrivateProfileStringW(L"App", L"StartupInitialized", L"1", (directory / L"settings.ini").c_str()) != FALSE;
}
std::wstring ModeText(Mode mode) {
    return mode == Mode::automatic ? L"自动模式" : mode == Mode::confirmation ? L"确认模式" : L"手动模式";
}
const wchar_t* ModeName(Mode mode) {
    return mode == Mode::automatic ? L"auto" : mode == Mode::confirmation ? L"confirm" : L"manual";
}
std::optional<Mode> ParseMode(const std::wstring& name) {
    if (name == L"auto") return Mode::automatic;
    if (name == L"confirm") return Mode::confirmation;
    if (name == L"manual") return Mode::manual;
    return std::nullopt;
}
Mode LoadMode(const std::filesystem::path& directory) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"App", L"Mode", L"auto", value, static_cast<DWORD>(std::size(value)), (directory / L"settings.ini").c_str());
    return ParseMode(value).value_or(Mode::automatic);
}
bool SaveMode(const std::filesystem::path& directory, Mode mode) {
    return WritePrivateProfileStringW(L"App", L"Mode", ModeName(mode), (directory / L"settings.ini").c_str()) != FALSE;
}
std::optional<int> ParseRefreshTarget(const std::wstring& value) {
    if (value == L"auto") return 0;
    if (value.empty()) return std::nullopt;
    int hz = 0;
    for (const wchar_t c : value) {
        if (c < L'0' || c > L'9' || hz > (std::numeric_limits<int>::max()-(c-L'0'))/10) return std::nullopt;
        hz = hz*10 + (c-L'0');
    }
    return hz > 1 ? std::optional<int>{hz} : std::nullopt;
}
RefreshTargets LoadRefreshTargets(const std::filesystem::path& directory, bool* invalid) {
    RefreshTargets targets;
    if (invalid) *invalid = false;
    const auto read = [&](const wchar_t* key) {
        wchar_t value[64]{};
        GetPrivateProfileStringW(L"RefreshRate",key,L"auto",value,static_cast<DWORD>(std::size(value)),(directory/L"settings.ini").c_str());
        const auto parsed = ParseRefreshTarget(value);
        if (!parsed && invalid) *invalid = true;
        return parsed.value_or(0);
    };
    targets.ac = read(L"AC"); targets.battery = read(L"Battery");
    return targets;
}
bool SaveRefreshTarget(const std::filesystem::path& directory, PowerSource source, int hz) {
    if (source == PowerSource::unknown || (hz != 0 && hz <= 1)) return false;
    const std::wstring value = hz ? std::to_wstring(hz) : L"auto";
    return WritePrivateProfileStringW(L"RefreshRate",source == PowerSource::ac ? L"AC" : L"Battery",value.c_str(),
        (directory/L"settings.ini").c_str()) != FALSE;
}
std::wstring CurrentRateText(const Screen& screen) {
    const auto format = [](double hz) {
        std::wostringstream text;
        text << std::fixed << std::setprecision(std::abs(hz-std::round(hz)) < 0.01 ? 0 : 2) << hz << L"Hz";
        return text.str();
    };
    if (screen.virtual_mode_supported && std::isfinite(screen.desktop_hz) && screen.desktop_hz > 1 &&
        std::isfinite(screen.physical_hz) && screen.physical_hz > screen.desktop_hz+0.5 &&
        !RateMatches(screen.nominal_hz,screen.physical_hz,screen.nominal_hz))
        return format(screen.desktop_hz)+L"（信号 "+format(screen.physical_hz)+L"）";
    if (std::isfinite(screen.physical_hz) && screen.physical_hz > 1 && std::abs(screen.physical_hz-screen.nominal_hz) >= 0.01) {
        std::wostringstream text; text << std::fixed << std::setprecision(2) << screen.physical_hz << L"Hz"; return text.str();
    }
    return std::to_wstring(screen.nominal_hz)+L"Hz";
}
}
