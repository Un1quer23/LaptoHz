#include "support.hpp"
#include "tray_menu.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace rrs;
namespace {
std::set<wchar_t> Placeholders(std::wstring_view text) {
    std::set<wchar_t> result;
    for (size_t i = 0; i+2 < text.size(); ++i)
        if (text[i] == L'{' && text[i+1] >= L'0' && text[i+1] <= L'9' && text[i+2] == L'}') result.insert(text[i+1]);
    return result;
}
std::wstring Label(HMENU menu, UINT id) { wchar_t text[1024]{}; GetMenuStringW(menu,id,text,1024,MF_BYCOMMAND); return text; }
bool HasChinese(std::wstring_view text) { for (auto c : text) if (c >= 0x4e00 && c <= 0x9fff) return true; return false; }
}
int main() {
    int assertions = 0;
    const auto check = [&](bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); };
    try {
        check(ResolveLanguage(Language::system,0x0804) == Language::chinese,"Chinese Windows uses Chinese.");
        check(ResolveLanguage(Language::system,0x1004) == Language::chinese,"Singapore Chinese also uses Chinese.");
        for (const auto language : {0x0409,0x0809,0x0407,0x0411,0x0000})
            check(ResolveLanguage(Language::system,static_cast<LANGID>(language)) == Language::english,"Other Windows UI languages fall back to English.");
        check(ResolveLanguage(Language::english,0x0804) == Language::english &&
            ResolveLanguage(Language::chinese,0x0409) == Language::chinese,"Explicit preferences override Windows.");
        check(!ParseLanguage(L"invalid") && ParseLanguage(L"system") == Language::system &&
            ParseLanguage(L"en") == Language::english && ParseLanguage(L"zh-CN") == Language::chinese,"Language values are validated.");
        for (int index = 0; index < static_cast<int>(Text::count); ++index) {
            const auto id = static_cast<Text>(index);
            const std::wstring chinese = TextFor(id,Language::chinese), english = TextFor(id,Language::english);
            check(!chinese.empty() && !english.empty(),"Every message has both translations.");
            check(Placeholders(chinese) == Placeholders(english),"Translations have matching positional arguments.");
            check(!HasChinese(english),"English messages contain no untranslated Chinese.");
        }
        auto delayed = Localize(Text::settings_warning,{Localize(Text::recovery_request_failed)});
        SetUiLanguage(Language::english);
        check(delayed.Get().find(L"Could not request") != std::wstring::npos && !HasChinese(delayed.Get()),"Stored asynchronous details render in English.");
        check(Tr(Text::target_set,{L"{1}",L"60Hz"}) == L"{1} set to 60Hz","Inserted data is not reinterpreted as a format placeholder.");
        check(PowerText(PowerSource::battery) == L"On battery" && ModeText(Mode::confirmation) == L"Confirmation mode","Status vocabulary is translated.");
        check(!HasChinese(NativeError(ERROR_ACCESS_DENIED)) && NativeError(0x7ffffffe).find(L"Error code") == 0,"Native errors and missing messages remain English.");
        Screen screen{L"inner",30,60,{30,60,240},false,false}; screen.virtual_mode_supported = true; screen.desktop_hz = 30;
        check(CurrentRateText(screen) == L"30Hz (signal 60Hz)","Desktop/signal differences are translated.");
        for (const auto reason : {Reason::paused,Reason::unknown_power,Reason::no_screen,Reason::ambiguous_screen,Reason::remote_session,
             Reason::query_failed,Reason::cloned_source,Reason::dynamic_refresh,Reason::unsupported_mode,Reason::no_valid_modes,Reason::capabilities_changed})
            check(!ReasonText(reason).empty() && !HasChinese(ReasonText(reason)),"Every pause/failure reason is translated.");
        TrayMenuState state; state.summary = L"60Hz · Plugged in · Manual mode"; state.mode = Mode::manual; state.language = Language::english;
        state.input = PolicyInput{PowerSource::ac,Availability::ready,screen};
        HMENU menu = CreateTrayMenu(state);
        check(menu != nullptr,"English tray menu is created.");
        check(Label(menu,kMenuAuto) == L"Automatic mode" && Label(menu,kMenuStartup) == L"Run at Windows sign-in","Menu actions use English.");
        check(Label(menu,kMenuAcTarget) == L"Plugged-in target: Highest available (240Hz)","Translated target labels keep the resolved rate.");
        check(Label(menu,kMenuLanguage) == L"Language" && Label(menu,kMenuLanguageChinese) == L"简体中文" &&
            Label(menu,kMenuLanguageEnglish) == L"English","Both native language names remain discoverable.");
        check((GetMenuState(menu,kMenuLanguageEnglish,MF_BYCOMMAND) & MF_CHECKED) != 0,"The explicit English preference is checked.");
        state.busy = true; UpdateTrayMenu(menu,state);
        check(Label(menu,kMenuSummary).find(L"Switching") != std::wstring::npos,"Live busy status uses English.");
        DestroyTrayMenu(menu);
        SetUiLanguage(Language::chinese);
        check(delayed.Get().find(L"恢复原显示状态") != std::wstring::npos,"Stored details can switch back to Chinese.");
        std::cout << "PASS: " << assertions << " localization assertions.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
