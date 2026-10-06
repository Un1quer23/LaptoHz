#include "startup.hpp"
#include "support.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace rrs {
namespace {
// The former Run value is read and removed only for legacy startup migration.
constexpr wchar_t kName[] = L"RefreshRateSwitcher";
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool Ready() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
struct Release { template<class T> void operator()(T* object) const { if (object) object->Release(); } };
template<class T> using ComPtr = std::unique_ptr<T,Release>;
HRESULT ShellLink(ComPtr<IShellLinkW>& link, ComPtr<IPersistFile>& file) {
    IShellLinkW* raw = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&raw));
    if (FAILED(result)) return result;
    link.reset(raw);
    IPersistFile* persist = nullptr;
    result = link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persist));
    if (SUCCEEDED(result)) file.reset(persist);
    return result;
}
bool SamePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    const auto left = first.lexically_normal().wstring(), right = second.lexically_normal().wstring();
    return CompareStringOrdinal(left.c_str(),-1,right.c_str(),-1,TRUE) == CSTR_EQUAL;
}
std::wstring ReadRun(const std::wstring& key) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER,key.c_str(),kName,RRF_RT_REG_SZ,nullptr,nullptr,&bytes) != ERROR_SUCCESS || bytes > 65536) return {};
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1,L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER,key.c_str(),kName,RRF_RT_REG_SZ,nullptr,value.data(),&bytes) != ERROR_SUCCESS) return {};
    return value.data();
}
bool Missing(LONG result) { return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND; }
}

std::filesystem::path StartupShortcutPath() {
    PWSTR folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Startup,KF_FLAG_DEFAULT,nullptr,&folder);
    if (FAILED(result)) throw std::runtime_error("Cannot locate the current user's Startup folder.");
    std::filesystem::path path(folder); CoTaskMemFree(folder);
    return path / L"LaptoHz.lnk";
}
bool StartupRegistration::ShortcutMatches(const std::filesystem::path& path) const {
    Apartment apartment;
    if (!apartment.Ready()) return false;
    ComPtr<IShellLinkW> link; ComPtr<IPersistFile> file;
    if (FAILED(ShellLink(link,file)) || FAILED(file->Load(path.c_str(),STGM_READ))) return false;
    std::vector<wchar_t> target(32768,L'\0'), arguments(32768,L'\0');
    if (FAILED(link->GetPath(target.data(),static_cast<int>(target.size()),nullptr,SLGP_RAWPATH)) ||
        FAILED(link->GetArguments(arguments.data(),static_cast<int>(arguments.size())))) return false;
    std::error_code error;
    return SamePath(target.data(),executable_) && std::wstring(arguments.data()) == L"--startup" &&
        std::filesystem::is_regular_file(executable_,error);
}
bool StartupRegistration::ApprovalBlocked(const wchar_t* group, const std::wstring& value) const {
    const std::wstring key = registry_base_ + L"\\Explorer\\StartupApproved\\" + group;
    BYTE bytes[12]{}; DWORD size = sizeof(bytes);
    const LONG result = RegGetValueW(HKEY_CURRENT_USER,key.c_str(),value.c_str(),RRF_RT_REG_BINARY,nullptr,bytes,&size);
    if (Missing(result)) return false;
    // Explorer owns this data. Read it only, never clear a Windows disable
    // choice. Unknown states are conservative and cannot trigger migration.
    if (result != ERROR_SUCCESS || size != sizeof(bytes)) return true;
    const DWORD state = static_cast<DWORD>(bytes[0]) | (static_cast<DWORD>(bytes[1]) << 8) |
        (static_cast<DWORD>(bytes[2]) << 16) | (static_cast<DWORD>(bytes[3]) << 24);
    return state != 2 && state != 6;
}
StartupState StartupRegistration::Inspect() const {
    StartupState state;
    state.shortcut = ShortcutMatches(shortcut_);
    state.legacy_run = ReadRun(registry_base_ + L"\\Run") == L"\"" + executable_.wstring() + L"\" --startup";
    state.blocked = ApprovalBlocked(L"Run",kName) || ApprovalBlocked(L"StartupFolder",shortcut_.filename().wstring());
    return state;
}
bool StartupRegistration::RemoveLegacy(std::wstring& error) const {
    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,(registry_base_ + L"\\Run").c_str(),0,KEY_SET_VALUE,&key);
    if (result == ERROR_SUCCESS) { result = RegDeleteValueW(key,kName); RegCloseKey(key); }
    if (result == ERROR_SUCCESS || Missing(result)) return true;
    error = L"无法清理旧登录自启项 · " + NativeError(result); return false;
}
bool StartupRegistration::Set(bool enabled, std::wstring& error) const {
    error.clear();
    if (!enabled) {
        if (!DeleteFileW(shortcut_.c_str()) && !Missing(GetLastError())) {
            error = L"无法删除登录自启快捷方式 · " + NativeError(GetLastError()); return false;
        }
        return RemoveLegacy(error);
    }
    if (ApprovalBlocked(L"Run",kName) || ApprovalBlocked(L"StartupFolder",shortcut_.filename().wstring())) {
        error = L"Windows 已禁用或无法确认此启动项；请在“设置 → 应用 → 启动”中启用 LaptoHz 后重试";
        return false;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(executable_,filesystem_error)) { error = L"程序文件不存在，请从固定目录重新运行"; return false; }
    std::filesystem::create_directories(shortcut_.parent_path(),filesystem_error);
    if (filesystem_error) { error = L"无法写入启动文件夹 · " + NativeError(filesystem_error.value()); return false; }
    Apartment apartment;
    ComPtr<IShellLinkW> link; ComPtr<IPersistFile> file;
    HRESULT result = apartment.Ready() ? ShellLink(link,file) : apartment.result;
    if (SUCCEEDED(result)) result = link->SetPath(executable_.c_str());
    if (SUCCEEDED(result)) result = link->SetArguments(L"--startup");
    if (SUCCEEDED(result)) result = link->SetWorkingDirectory(executable_.parent_path().c_str());
    if (SUCCEEDED(result)) result = link->SetDescription(L"LaptoHz 登录自启");
    if (SUCCEEDED(result)) result = link->SetShowCmd(SW_SHOWNORMAL);
    const auto temporary = shortcut_.parent_path() / (shortcut_.filename().wstring() + L"." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp");
    if (SUCCEEDED(result)) result = file->Save(temporary.c_str(),TRUE);
    if (FAILED(result)) { DeleteFileW(temporary.c_str()); error = L"无法创建登录自启快捷方式 · " + NativeError(result); return false; }
    if (!ShortcutMatches(temporary)) { DeleteFileW(temporary.c_str()); error = L"登录自启快捷方式校验失败"; return false; }
    if (!MoveFileExW(temporary.c_str(),shortcut_.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError(); DeleteFileW(temporary.c_str());
        error = L"无法保存登录自启快捷方式 · " + NativeError(code); return false;
    }
    return RemoveLegacy(error);
}
bool StartupRegistration::Migrate(bool& changed, std::wstring& error) const {
    changed = false; error.clear();
    const auto state = Inspect();
    if (!state.legacy_run || state.blocked) return true;
    if (!Set(true,error)) return false;
    changed = true; return true;
}
bool StartupRegistered() {
    try { return StartupRegistration(ExecutablePath(),StartupShortcutPath()).Inspect().Enabled(); }
    catch (...) { return false; }
}
bool SetStartup(bool enabled, std::wstring& error) {
    try { return StartupRegistration(ExecutablePath(),StartupShortcutPath()).Set(enabled,error); }
    catch (const std::exception& exception) { const std::string detail = exception.what(); error = L"无法访问启动文件夹 · " + std::wstring(detail.begin(),detail.end()); return false; }
}
bool MigrateStartup(bool& changed, std::wstring& error) {
    try { return StartupRegistration(ExecutablePath(),StartupShortcutPath()).Migrate(changed,error); }
    catch (const std::exception& exception) { const std::string detail = exception.what(); changed = false; error = L"无法迁移登录自启 · " + std::wstring(detail.begin(),detail.end()); return false; }
}
}
