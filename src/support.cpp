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
namespace {
std::wstring NativeErrorFor(LONG code, Language language) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code),
        language == Language::english ? MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US) : MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    // If Windows lacks this message translation, retain a readable error code
    // in the requested language instead of mixing UI languages.
    std::wstring text = length ? std::wstring(buffer, length) : Localize(Text::error_code,{std::to_wstring(code)}).Get(language);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text;
}
}
LocalizedText NativeErrorMessage(LONG code) { return {NativeErrorFor(code,Language::chinese),NativeErrorFor(code,Language::english)}; }
std::wstring NativeError(LONG code) { return NativeErrorMessage(code).Get(); }
PowerSource ReadPower() {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) return PowerSource::unknown;
    if (status.ACLineStatus == 1) return PowerSource::ac;
    if (status.ACLineStatus == 0) return PowerSource::battery;
    return PowerSource::unknown;
}
std::wstring PowerText(PowerSource power) {
    return Tr(power == PowerSource::ac ? Text::power_ac : power == PowerSource::battery ? Text::power_battery : Text::power_unknown);
}
LocalizedText ReasonMessage(Reason reason) {
    switch (reason) {
    case Reason::none: return {};
    case Reason::paused: return Localize(Text::reason_paused);
    case Reason::unknown_power: return Localize(Text::reason_power);
    case Reason::no_screen: return Localize(Text::reason_screen);
    case Reason::ambiguous_screen: return Localize(Text::reason_ambiguous);
    case Reason::remote_session: return Localize(Text::reason_remote);
    case Reason::query_failed: return Localize(Text::reason_query);
    case Reason::cloned_source: return Localize(Text::reason_clone);
    case Reason::dynamic_refresh: return Localize(Text::reason_drr);
    case Reason::no_valid_modes: return Localize(Text::reason_no_rates);
    case Reason::capabilities_changed: return Localize(Text::reason_capabilities);
    case Reason::unsupported_mode: return Localize(Text::reason_unsupported);
    }
    return {};
}
std::wstring ReasonText(Reason reason) { return ReasonMessage(reason).Get(); }
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
        error = Tr(Text::log_unavailable) + L" · " + NativeError(GetLastError()) + L" · " + file.wstring(); return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle,&info)) {
        const DWORD code = GetLastError(); CloseHandle(handle);
        error = Tr(Text::log_info_failed) + L" · " + NativeError(code) + L" · " + file.wstring(); return false;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        CloseHandle(handle);
        error = Tr(Text::log_unavailable) + L" · " + file.wstring(); return false;
    }
    // AppData can be redirected by the launching application's environment.
    // An external editor needs the actual file path, not our virtualized name.
    std::vector<wchar_t> buffer(32768,L'\0');
    const DWORD count = GetFinalPathNameByHandleW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const DWORD code = count ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
    CloseHandle(handle);
    if (!count || count >= buffer.size()) {
        error = Tr(Text::log_path_failed) + L" · " + NativeError(code) + L" · " + file.wstring(); return false;
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
    if (!length || length >= system.size()) { error = Tr(Text::notepad_not_found,{resolved.wstring()}); return false; }
    const auto notepad = std::filesystem::path(std::wstring(system.data(),length)) / L"notepad.exe";
    const auto parameters = L"\"" + resolved.wstring() + L"\"";
    const auto directory = resolved.parent_path().wstring();
    // Modern Notepad may delegate to a packaged app. Initialize STA for Shell
    // activation and use the system launcher instead of an App Paths alias.
    const HRESULT apartment = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        error = Tr(Text::notepad_init_failed,{NativeError(apartment),resolved.wstring()}); return false;
    }
    SHELLEXECUTEINFOW execute{}; execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    execute.hwnd = owner; execute.lpVerb = L"open"; execute.lpFile = notepad.c_str();
    execute.lpParameters = parameters.c_str(); execute.lpDirectory = directory.c_str(); execute.nShow = SW_SHOWNORMAL;
    const bool opened = ShellExecuteExW(&execute) != FALSE;
    const DWORD code = opened ? ERROR_SUCCESS : GetLastError();
    if (SUCCEEDED(apartment)) CoUninitialize();
    if (!opened) error = Tr(Text::notepad_failed,{NativeError(code),resolved.wstring()});
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
    return Tr(mode == Mode::automatic ? Text::mode_auto : mode == Mode::confirmation ? Text::mode_confirm : Text::mode_manual);
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
Language LoadLanguage(const std::filesystem::path& directory) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"App",L"Language",L"system",value,static_cast<DWORD>(std::size(value)),(directory/L"settings.ini").c_str());
    return ParseLanguage(value).value_or(Language::system);
}
bool SaveLanguage(const std::filesystem::path& directory, Language language) {
    if (language != Language::system && language != Language::chinese && language != Language::english) return false;
    return WritePrivateProfileStringW(L"App",L"Language",LanguageName(language),(directory/L"settings.ini").c_str()) != FALSE;
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
        return Tr(Text::signal_rate,{format(screen.desktop_hz),format(screen.physical_hz)});
    if (std::isfinite(screen.physical_hz) && screen.physical_hz > 1 && std::abs(screen.physical_hz-screen.nominal_hz) >= 0.01) {
        std::wostringstream text; text << std::fixed << std::setprecision(2) << screen.physical_hz << L"Hz"; return text.str();
    }
    return std::to_wstring(screen.nominal_hz)+L"Hz";
}
}
