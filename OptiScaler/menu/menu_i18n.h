#pragma once
// Portable English/Chinese display translation for the OptiScaler native menu.
//
// Deliberately dependency-free: standard library only, no Windows headers, no
// ImGui, no locale/OS APIs, no threads. The same code backs the standalone
// behaviour suite (core/arm64ec/tests/test_menu_i18n.cpp) and the vendored
// ImGui display hooks in include/imgui/imgui.cpp and imgui_widgets.cpp.
//
// Contract:
//   * English is the source of truth and the fallback. Every lookup miss, every
//     non-Chinese language and every untouched string is returned byte-for-byte
//     (whitespace included), never trimmed, prettified or guessed.
//   * Translation is a DISPLAY concern only. Callers must never feed translated
//     text back into label hashing, widget ids, item input labels, persistence
//     or clipboard keys.
//   * No dictionary entry is copied per frame: the lookup table is built once.
//
// The dictionary is a generated table of {English, Chinese} UTF-8 literals and
// is included behind this header.

#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace OptiI18n
{
namespace Detail
{

struct Entry
{
    const char* english;
    const char* chinese;
};

// The generated dictionary. menu_zh_cn.inc expands to a comma-separated list of
// `{ "English", "Chinese" },` rows and is included exactly once, here, so the
// table lives only in this inline function's static storage.
inline const Entry* Entries(std::size_t& count)
{
    static constexpr Entry kEntries[] = {
#include "menu_zh_cn.inc"
// Supplement: visible native notification strings not covered by the generated
// main-menu catalog, authored by hand but built into the same table/keyspace.
#include "menu_notifications_zh_cn.inc"
    };
    count = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

// Process-wide lookup map built once on first use, shared by every translation
// unit because the enclosing function is inline. Keys/values point at immutable
// string literals for the lifetime of the process.
inline const std::unordered_map<std::string_view, std::string_view>& Dictionary()
{
    static const std::unordered_map<std::string_view, std::string_view> dictionary = [] {
        std::size_t count = 0;
        const Entry* entries = Entries(count);
        std::unordered_map<std::string_view, std::string_view> map;
        map.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            map.emplace(std::string_view(entries[i].english), std::string_view(entries[i].chinese));
        return map;
    }();
    return dictionary;
}

// Process-wide language flag. The render/menu thread stores it while native
// notification callers can read it from arbitrary game worker threads, so it is
// a relaxed atomic: no torn reads, no locks and no new threads. This is an
// internal detail; the public API is unchanged.
inline std::atomic<bool>& ChineseFlag()
{
    static std::atomic<bool> chinese{ false };
    return chinese;
}

// ASCII case-insensitive comparison of a runtime language tag against a literal.
inline bool EqualsIgnoreCaseAscii(std::string_view value, const char* literal)
{
    std::size_t i = 0;
    for (; i < value.size() && literal[i] != '\0'; ++i)
    {
        char a = value[i];
        char b = literal[i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return i == value.size() && literal[i] == '\0';
}

} // namespace Detail

// Normalise a language tag. Only the two Chinese spellings select Chinese;
// everything else, including the default and an empty tag, selects English.
inline void SetLanguage(std::string_view language)
{
    Detail::ChineseFlag().store(Detail::EqualsIgnoreCaseAscii(language, "zh-CN") ||
                                    Detail::EqualsIgnoreCaseAscii(language, "schinese"),
                                std::memory_order_relaxed);
}

// The normalised current tag: "en" or "zh-CN".
inline std::string_view Language()
{
    return Detail::ChineseFlag().load(std::memory_order_relaxed) ? std::string_view("zh-CN")
                                                                 : std::string_view("en");
}

// Translate a complete English string. Returns a pointer into the immutable
// dictionary for a known key while Chinese is active, otherwise the exact input
// pointer. The returned pointer is stable for the process lifetime and must not
// be freed; the caller's buffer is never modified.
inline const char* Format(const char* english)
{
    if (english == nullptr || !Detail::ChineseFlag().load(std::memory_order_relaxed))
        return english;
    const auto& dictionary = Detail::Dictionary();
    const auto it = dictionary.find(std::string_view(english));
    return it == dictionary.end() ? english : it->second.data();
}

// Translate a bounded display range and return an owned string.
//
//   * A trailing ImGui "##id" is the visible/hidden split: the visible part
//     before the first "##" is translated, and the original "##suffix" is kept
//     verbatim unless hideAfterDoubleHash is set (then only the visible part
//     remains).
//   * Untranslated text, including a whole untranslated label, is returned
//     byte-for-byte; a bounded range never reads past `end`.
inline std::string TranslateRange(const char* begin, const char* end = nullptr, bool hideAfterDoubleHash = false)
{
    if (begin == nullptr)
        return std::string();
    if (end == nullptr)
        end = begin + std::strlen(begin);
    if (end <= begin)
        return std::string();

    const char* hash = nullptr;
    for (const char* p = begin; p + 1 < end; ++p)
    {
        if (p[0] == '#' && p[1] == '#')
        {
            hash = p;
            break;
        }
    }
    const char* visible_end = hash != nullptr ? hash : end;

    const char* translated = nullptr;
    if (Detail::ChineseFlag().load(std::memory_order_relaxed))
    {
        const auto& dictionary = Detail::Dictionary();
        const auto it = dictionary.find(std::string_view(begin, static_cast<std::size_t>(visible_end - begin)));
        if (it != dictionary.end())
            translated = it->second.data();
    }

    if (hash == nullptr)
    {
        if (translated != nullptr)
            return std::string(translated);
        return std::string(begin, static_cast<std::size_t>(end - begin));
    }

    if (hideAfterDoubleHash)
    {
        if (translated != nullptr)
            return std::string(translated);
        return std::string(begin, static_cast<std::size_t>(visible_end - begin));
    }

    if (translated != nullptr)
        return std::string(translated) + std::string(hash, static_cast<std::size_t>(end - hash));
    return std::string(begin, static_cast<std::size_t>(end - begin));
}

} // namespace OptiI18n
