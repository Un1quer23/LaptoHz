#pragma once
#include <windows.h>
#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rrs {
enum class Language { system, chinese, english };
enum class Text {
#define RRS_TEXT(id, chinese, english) id,
#include "strings.inc"
#undef RRS_TEXT
    count
};
Language ResolveLanguage(Language preference, LANGID windows_language);
Language CurrentLanguage();
void SetUiLanguage(Language preference);
const wchar_t* LanguageName(Language language);
std::optional<Language> ParseLanguage(std::wstring_view value);
const wchar_t* TextFor(Text id, Language language);
std::wstring Tr(Text id);
std::wstring Tr(Text id, std::initializer_list<std::wstring> arguments);

// Keep both translations in asynchronous results. Changing the UI language
// must not cancel a display operation or leave an old confirmation in Chinese.
class LocalizedText {
public:
    LocalizedText() = default;
    LocalizedText(const wchar_t* text) : LocalizedText(std::wstring(text)) {}
    LocalizedText(std::wstring text) : values_{text,std::move(text)} {}
    LocalizedText(std::wstring chinese, std::wstring english) : values_{std::move(chinese),std::move(english)} {}
    const std::wstring& Get(Language language) const { return values_[language == Language::english ? 1 : 0]; }
    const std::wstring& Get() const { return Get(CurrentLanguage()); }
    bool empty() const { return values_[0].empty() && values_[1].empty(); }
    void clear() { values_[0].clear(); values_[1].clear(); }
    LocalizedText& operator+=(const LocalizedText& other);
    friend LocalizedText operator+(LocalizedText left, const LocalizedText& right) { left += right; return left; }
private:
    std::array<std::wstring,2> values_;
};
LocalizedText Localize(Text id);
LocalizedText Localize(Text id, std::initializer_list<LocalizedText> arguments);
}
