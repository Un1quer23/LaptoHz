#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

namespace rrs {
struct StartupState {
    bool shortcut = false, legacy_run = false, blocked = false;
    bool Enabled() const { return (shortcut || legacy_run) && !blocked; }
};

// Tests provide a temporary shortcut and registry branch. Production uses the
// current user's Startup known folder and the Windows CurrentVersion branch.
class StartupRegistration {
public:
    StartupRegistration(std::filesystem::path executable, std::filesystem::path shortcut,
        std::wstring registry_base = L"Software\\Microsoft\\Windows\\CurrentVersion")
        : executable_(std::move(executable)), shortcut_(std::move(shortcut)), registry_base_(std::move(registry_base)) {}
    StartupState Inspect() const;
    bool Set(bool enabled, std::wstring& error) const;
    bool Migrate(bool& changed, std::wstring& error) const;
private:
    bool ShortcutMatches(const std::filesystem::path& file) const;
    bool ApprovalBlocked(const wchar_t* group, const std::wstring& value) const;
    bool RemoveLegacy(std::wstring& error) const;
    std::filesystem::path executable_, shortcut_;
    std::wstring registry_base_;
};

std::filesystem::path StartupShortcutPath();
bool MigrateStartup(bool& changed, std::wstring& error);
}
