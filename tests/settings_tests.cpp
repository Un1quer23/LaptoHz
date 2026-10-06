#include "support.hpp"
#include <iostream>
#include <stdexcept>
using namespace rrs;
int main() {
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH,temporary)) return 1;
    const auto directory = std::filesystem::path(temporary)/(L"rrs-settings-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(directory);
    int assertions = 0;
    const auto check = [&](bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); };
    int result = 0;
    try {
        check(LoadMode(directory) == Mode::automatic,"Missing settings default to automatic.");
        check(MarkStartupInitialized(directory),"Create legacy startup marker.");
        check(LoadMode(directory) == Mode::automatic,"Legacy startup-only configuration upgrades to automatic.");
        for (const auto mode : {Mode::confirmation,Mode::manual,Mode::automatic}) {
            check(SaveMode(directory,mode),"Save mode.");
            check(LoadMode(directory) == mode,"Mode survives a reload.");
            check(StartupInitialized(directory),"Changing mode preserves the startup marker.");
        }
        check(WritePrivateProfileStringW(L"App",L"Mode",L"invalid",(directory/L"settings.ini").c_str()) != FALSE,"Write invalid setting.");
        check(LoadMode(directory) == Mode::automatic,"Invalid mode falls back to automatic.");
        check(!SaveMode(directory/L"missing-parent"/L"missing-child",Mode::manual),"A write failure is reported.");
        check(ParseMode(L"confirm") == Mode::confirmation && !ParseMode(L"240"),"CLI mode validation.");
        check(LoadRefreshTargets(directory) == RefreshTargets{},"Legacy settings use dynamic defaults.");
        check(SaveMode(directory,Mode::confirmation),"Save a mode before adding rate settings.");
        check(SaveRefreshTarget(directory,PowerSource::ac,165) && SaveRefreshTarget(directory,PowerSource::battery,48),"Persist arbitrary validated target choices.");
        check(LoadRefreshTargets(directory) == RefreshTargets{165,48},"Both targets survive a reload.");
        check(StartupInitialized(directory) && LoadMode(directory) == Mode::confirmation,"Rate choices preserve existing startup and mode fields.");
        check(SaveRefreshTarget(directory,PowerSource::ac,0) && LoadRefreshTargets(directory) == RefreshTargets{0,48},"Dynamic defaults are stored as auto, not the current maximum.");
        check(!SaveRefreshTarget(directory,PowerSource::unknown,60) && !SaveRefreshTarget(directory,PowerSource::ac,1),"Invalid power sources and driver-default frequencies are rejected.");
        for (const auto value : {L"",L"invalid",L"-60",L"60.0",L"60x",L"1",L"2147483648",L"99999999999999999999999999999999999999999999999999999999999999999999"}) {
            check(WritePrivateProfileStringW(L"RefreshRate",L"AC",value,(directory/L"settings.ini").c_str()) != FALSE,"Write malformed target.");
            bool invalid = false;
            check(LoadRefreshTargets(directory,&invalid) == RefreshTargets{0,48} && invalid,"Malformed values only reset the affected source.");
        }
        check(ParseRefreshTarget(L"120") == 120 && ParseRefreshTarget(L"auto") == 0 && ParseRefreshTarget(L"2147483647") == 2147483647,"Strict target parsing preserves all representable integer values.");
        check(!SaveRefreshTarget(directory/L"missing-parent",PowerSource::ac,144),"Target save failures are reported.");
        Screen low{L"inner",30,60,{30,48,60},false,false}; low.virtual_mode_supported = true; low.desktop_hz = 30;
        check(CurrentRateText(low) == L"30Hz（信号 60Hz）","Current-rate text explains lower desktop and higher physical signal.");
        low.nominal_hz = 48; low.desktop_hz = 48;
        check(CurrentRateText(low) == L"48Hz（信号 60Hz）","The current display text does not falsely claim a 48Hz physical signal.");
        low.nominal_hz = 60; low.desktop_hz = 59.94; low.physical_hz = 59.94;
        check(CurrentRateText(low) == L"59.94Hz","Fractional actual timing remains visible without a virtual-rate annotation.");
        low.nominal_hz = 59; low.desktop_hz = 59;
        check(CurrentRateText(low) == L"59.94Hz","Integer rounding of a fractional signal is not mislabeled as a separate virtual rate.");
        std::cout << "PASS: " << assertions << " settings assertions.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,(directory/L"settings.ini").c_str());
    std::filesystem::remove(directory/L"settings.ini"); std::filesystem::remove(directory);
    return result;
}
