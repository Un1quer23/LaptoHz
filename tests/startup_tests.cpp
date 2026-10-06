#include "startup.hpp"
#include "support.hpp"
#include <shobjidl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace rrs;
int main() {
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH,temporary)) return 1;
    const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto directory = std::filesystem::path(temporary) / (L"rrs-startup 测试 " + suffix);
    const std::wstring branch = L"Software\\LaptoHz.Tests\\" + suffix;
    const auto shortcut = directory / L"LaptoHz.lnk";
    const auto executable = ExecutablePath();
    std::filesystem::create_directory(directory);
    StartupRegistration registration(executable,shortcut,branch);
    int assertions = 0, result = 0;
    HANDLE locked = INVALID_HANDLE_VALUE;
    const auto check = [&](bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); };
    const auto write = [&](const std::wstring& key, const std::wstring& name, DWORD type, const void* data, DWORD bytes) {
        HKEY handle = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,(branch+L"\\"+key).c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot create the isolated registry branch.");
        const auto code = RegSetValueExW(handle,name.c_str(),0,type,static_cast<const BYTE*>(data),bytes);
        RegCloseKey(handle);
        if (code != ERROR_SUCCESS) throw std::runtime_error("Cannot write the isolated registry branch.");
    };
    const auto read_run = [&](const wchar_t* name = L"RefreshRateSwitcher") {
        wchar_t text[4096]{}; DWORD bytes = sizeof(text);
        const auto code = RegGetValueW(HKEY_CURRENT_USER,(branch+L"\\Run").c_str(),name,RRF_RT_REG_SZ,nullptr,text,&bytes);
        return code == ERROR_SUCCESS ? std::wstring(text) : std::wstring{};
    };
    const std::wstring command = L"\"" + executable.wstring() + L"\" --startup";
    const auto legacy = [&] { write(L"Run",L"RefreshRateSwitcher",REG_SZ,command.c_str(),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))); };
    const auto approval = [&](const wchar_t* group, const std::wstring& name, DWORD state) {
        const DWORD data[3] = {state,0,0};
        write(std::wstring(L"Explorer\\StartupApproved\\")+group,name,REG_BINARY,data,sizeof(data));
    };
    const auto clear_approval = [&](const wchar_t* group, const std::wstring& name) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,(branch+L"\\Explorer\\StartupApproved\\"+group).c_str(),0,KEY_SET_VALUE,&key) == ERROR_SUCCESS) {
            RegDeleteValueW(key,name.c_str()); RegCloseKey(key);
        }
    };
    try {
        std::wstring error; bool changed = true;
        check(!registration.Inspect().Enabled(),"Absent registration is off.");
        check(registration.Migrate(changed,error) && !changed,"A disabled legacy preference is not recreated.");
        check(!std::filesystem::exists(shortcut),"Migration with no Run value creates no shortcut.");
        const wchar_t other[] = L"unrelated startup";
        write(L"Run",L"OtherApplication",REG_SZ,other,sizeof(other));
        check(registration.Set(true,error),"Register a standard shell shortcut in a path with spaces and Unicode.");
        check(registration.Inspect().shortcut && registration.Inspect().Enabled(),"The saved shortcut is recognized as enabled.");
        check(read_run().empty(),"Enable uses one startup source.");
        check(read_run(L"OtherApplication") == other,"Unrelated Run values are preserved.");
        const HRESULT apartment = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        IShellLinkW* link = nullptr; IPersistFile* file = nullptr;
        check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))),"Windows can load the created shell link.");
        check(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&file))),"The shortcut has a persisted file.");
        check(SUCCEEDED(file->Load(shortcut.c_str(),STGM_READ)),"The saved link reloads independently.");
        wchar_t args[256]{}, target[4096]{}, working[4096]{};
        link->GetArguments(args,256); link->GetPath(target,4096,nullptr,SLGP_RAWPATH); link->GetWorkingDirectory(working,4096);
        check(std::wstring(args) == L"--startup","Shortcut arguments are separate from the executable path.");
        check(std::filesystem::path(target) == executable,"Windows reads the exact executable path, including spaces.");
        check(std::filesystem::path(working) == executable.parent_path(),"Shortcut working directory is fixed.");
        link->SetArguments(L"--diagnose"); file->Save(shortcut.c_str(),TRUE);
        file->Release(); link->Release(); if (SUCCEEDED(apartment)) CoUninitialize();
        check(!registration.Inspect().Enabled(),"A shortcut with wrong arguments does not falsely report startup enabled.");
        check(registration.Set(true,error),"An explicit enable repairs the shortcut.");
        check(registration.Set(false,error) && !registration.Inspect().Enabled(),"Disable removes the shortcut.");
        check(registration.Set(false,error),"Repeated disable is harmless.");
        legacy();
        check(registration.Inspect().legacy_run && registration.Inspect().Enabled(),"Existing matching Run registration is recognized before migration.");
        check(registration.Migrate(changed,error) && changed,"Enabled legacy startup migrates.");
        check(registration.Inspect().shortcut && read_run().empty(),"Migration installs the link and removes the old Run value.");
        check(registration.Migrate(changed,error) && !changed,"A completed migration does not recreate registrations.");
        check(registration.Set(false,error),"Disable after upgrade removes both startup sources.");
        check(registration.Migrate(changed,error) && !changed && !std::filesystem::exists(shortcut),"A disabled choice survives migration/restart.");
        legacy(); approval(L"Run",L"RefreshRateSwitcher",3);
        check(!registration.Inspect().Enabled() && registration.Inspect().blocked,"Windows-disabled Run startup is shown as off.");
        check(registration.Migrate(changed,error) && !changed && !std::filesystem::exists(shortcut),"Migration respects Windows disabling the old entry.");
        check(!registration.Set(true,error) && !error.empty(),"Explicit enable explains a Windows disable without bypassing it.");
        check(read_run() == command,"A refused migration preserves the legacy entry.");
        clear_approval(L"Run",L"RefreshRateSwitcher");
        approval(L"Run",L"RefreshRateSwitcher",6);
        check(registration.Inspect().Enabled(),"Known enabled Windows approval is recognized.");
        check(registration.Migrate(changed,error) && changed,"Known enabled approval permits migration.");
        clear_approval(L"Run",L"RefreshRateSwitcher");
        approval(L"StartupFolder",shortcut.filename().wstring(),7);
        check(!registration.Inspect().Enabled(),"Windows disabling the shortcut clears its enabled state.");
        check(!registration.Set(true,error),"A disabled shortcut is not silently re-enabled.");
        approval(L"StartupFolder",shortcut.filename().wstring(),1234);
        check(registration.Inspect().blocked && !registration.Set(true,error),"Unknown approval data cannot cause an automatic opt-in.");
        clear_approval(L"StartupFolder",shortcut.filename().wstring());
        approval(L"StartupFolder",shortcut.filename().wstring(),2);
        check(registration.Inspect().Enabled(),"Known enabled shortcut approval is recognized.");
        clear_approval(L"StartupFolder",shortcut.filename().wstring());
        legacy();
        locked = CreateFileW(shortcut.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(locked != INVALID_HANDLE_VALUE,"Lock the test shortcut to exercise save failure.");
        check(!registration.Set(true,error) && !error.empty(),"A failed atomic replacement reports failure.");
        check(read_run() == command,"Save failure retains the previous Run startup source.");
        CloseHandle(locked); locked = INVALID_HANDLE_VALUE;
        check(registration.Set(true,error),"Registration can be retried after the write failure.");
        check(registration.Set(false,error) && read_run(L"OtherApplication") == other,"Cleanup removes only this product's startup entries.");
        StartupRegistration missing(directory/L"missing.exe",shortcut,branch);
        check(!missing.Set(true,error) && !std::filesystem::exists(shortcut),"A missing executable is not registered.");
        const auto blocker = directory / L"blocked-parent"; std::ofstream(blocker) << "test";
        StartupRegistration unavailable(executable,blocker/L"test.lnk",branch);
        check(!unavailable.Set(true,error) && !error.empty(),"Startup-folder write errors are reported.");
        std::filesystem::remove(blocker);
        check(std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{}) == 0,"Failed writes leave no temporary startup files.");
        std::cout << "PASS: " << assertions << " isolated startup assertions.\n";
    } catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; result = 1; }
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    RegDeleteTreeW(HKEY_CURRENT_USER,branch.c_str());
    for (const auto& entry : std::filesystem::directory_iterator(directory)) std::filesystem::remove(entry.path());
    std::filesystem::remove(directory);
    return result;
}
