#include "localization.hpp"
#include <atomic>
#include <span>
#include <vector>

namespace rrs {
namespace {
struct Translation { const wchar_t* chinese; const wchar_t* english; };
constexpr Translation kTexts[] = {
#define RRS_TEXT(id, chinese, english) {chinese,english},
#include "strings.inc"
#undef RRS_TEXT
};
static_assert(std::size(kTexts) == static_cast<size_t>(Text::count));
// The application initializes this from settings before creating any UI.
std::atomic<Language> language{Language::chinese};
std::wstring Format(std::wstring_view pattern, std::span<const std::wstring> arguments) {
    std::wstring result;
    for (size_t i = 0; i < pattern.size();) {
        if (i+2 < pattern.size() && pattern[i] == L'{' && pattern[i+1] >= L'0' && pattern[i+1] <= L'9' && pattern[i+2] == L'}') {
            const auto index = static_cast<size_t>(pattern[i+1]-L'0');
            if (index < arguments.size()) { result += arguments[index]; i += 3; continue; }
        }
        result += pattern[i++];
    }
    return result;
}
}
Language ResolveLanguage(Language preference, LANGID windows_language) {
    if (preference == Language::chinese || preference == Language::english) return preference;
    return PRIMARYLANGID(windows_language) == LANG_CHINESE ? Language::chinese : Language::english;
}
Language CurrentLanguage() { return language.load(std::memory_order_relaxed); }
void SetUiLanguage(Language preference) { language.store(ResolveLanguage(preference,GetUserDefaultUILanguage()),std::memory_order_relaxed); }
const wchar_t* LanguageName(Language value) {
    return value == Language::chinese ? L"zh-CN" : value == Language::english ? L"en" : L"system";
}
std::optional<Language> ParseLanguage(std::wstring_view value) {
    if (value == L"system") return Language::system;
    if (value == L"zh-CN") return Language::chinese;
    if (value == L"en") return Language::english;
    return std::nullopt;
}
const wchar_t* TextFor(Text id, Language value) {
    const auto index = static_cast<size_t>(id);
    if (index >= std::size(kTexts)) return L"";
    return value == Language::english ? kTexts[index].english : kTexts[index].chinese;
}
std::wstring Tr(Text id) { return TextFor(id,CurrentLanguage()); }
std::wstring Tr(Text id, std::initializer_list<std::wstring> arguments) {
    return Format(TextFor(id,CurrentLanguage()),std::span(arguments.begin(),arguments.size()));
}
LocalizedText& LocalizedText::operator+=(const LocalizedText& other) {
    for (size_t i = 0; i < values_.size(); ++i) values_[i] += other.values_[i];
    return *this;
}
LocalizedText Localize(Text id) { return {TextFor(id,Language::chinese),TextFor(id,Language::english)}; }
LocalizedText Localize(Text id, std::initializer_list<LocalizedText> arguments) {
    const auto format = [&](Language value) {
        std::vector<std::wstring> translated;
        for (const auto& argument : arguments) translated.push_back(argument.Get(value));
        return Format(TextFor(id,value),translated);
    };
    return {format(Language::chinese),format(Language::english)};
}
}
