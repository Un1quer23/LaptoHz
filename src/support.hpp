#pragma once
#include "policy.hpp"
#include "localization.hpp"
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <string>

namespace rrs {
inline constexpr wchar_t kAppName[] = L"LaptoHz";
// Keep the host class stable so controls and the singleton work across the rename.
inline constexpr wchar_t kWindowClass[] = L"RefreshRateSwitcher.Host.v1";
inline constexpr wchar_t kVersion[] = L"0.4.0";
std::filesystem::path DataDirectory();
std::filesystem::path ExecutablePath();
std::wstring NativeError(LONG code);
LocalizedText NativeErrorMessage(LONG code);
std::wstring PowerText(PowerSource power);
std::wstring ReasonText(Reason reason);
LocalizedText ReasonMessage(Reason reason);
PowerSource ReadPower();
std::string Utf8(const std::wstring& text);
std::string JsonString(const std::wstring& text);
bool WriteText(const std::filesystem::path& file, const std::string& text);
bool ResolveDiagnosticLogPath(const std::filesystem::path& file, std::filesystem::path& resolved, std::wstring& error);
bool OpenDiagnosticLog(HWND owner, const std::filesystem::path& file, std::wstring& error);
class Logger {
public:
    explicit Logger(const std::filesystem::path& directory);
    void Write(const std::wstring& text);
    std::filesystem::path File() const { return file_; }
private:
    std::filesystem::path file_;
    std::mutex mutex_;
};
bool StartupRegistered();
bool SetStartup(bool enabled, std::wstring& error);
bool StartupInitialized(const std::filesystem::path& directory);
bool MarkStartupInitialized(const std::filesystem::path& directory);
std::wstring ModeText(Mode mode);
const wchar_t* ModeName(Mode mode);
std::optional<Mode> ParseMode(const std::wstring& name);
Mode LoadMode(const std::filesystem::path& directory);
bool SaveMode(const std::filesystem::path& directory, Mode mode);
Language LoadLanguage(const std::filesystem::path& directory);
bool SaveLanguage(const std::filesystem::path& directory, Language language);
std::optional<int> ParseRefreshTarget(const std::wstring& value);
RefreshTargets LoadRefreshTargets(const std::filesystem::path& directory, bool* invalid = nullptr);
bool SaveRefreshTarget(const std::filesystem::path& directory, PowerSource source, int hz);
std::wstring CurrentRateText(const Screen& screen);
}
