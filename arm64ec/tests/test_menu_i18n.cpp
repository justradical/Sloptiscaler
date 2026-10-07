// Behaviour tests for the native menu translation layer.
//
// These tests exercise the planned production header
// `OptiScaler/menu/menu_zh_cn.inc` behind `OptiScaler/menu/menu_i18n.h`
// (namespace `OptiI18n`). That header is added by the translation change;
// until it exists this file fails to compile with one clear #error naming the
// missing path, which is the intended (red) failure.
//
// The suite is standalone and portable: standard library only, C++20, no
// Windows headers, no ImGui. It never touches the filesystem, the global
// OptiScaler state or the game; it only drives the public string API.
//
// Build (any modern compiler):
//   c++ -std=c++20 -O2 -Wall -Wextra -o test_menu_i18n test_menu_i18n.cpp
// With the arm64ec tree, add: -IOptiScaler  (so <menu/menu_i18n.h> resolves)
//
// Run: ./test_menu_i18n   (exit code 0 = all checks passed)
//
// ---------------------------------------------------------------------------
// Required public API (menu/menu_i18n.h), namespace OptiI18n:
//
//   void            SetLanguage(std::string_view language);
//   std::string_view Language();                 // "en" | "zh-CN"
//   const char*     Format(const char* english); // translated or original
//   std::string     TranslateRange(const char* begin,
//                                  const char* end = nullptr,
//                                  bool hideAfterDoubleHash = false);
//
// Contract:
//   * SetLanguage normalises only "zh-CN"/"schinese" (case-insensitive) to
//     Chinese; every other value, including the default, is English.
//   * Language() reports the normalised current tag.
//   * Format() returns a *stable* pointer: the translated UTF-8 format when the
//     English source is a dictionary key, otherwise the very pointer passed in.
//   * TranslateRange() renders a bounded view; an ImGui "##id" suffix is kept
//     verbatim unless hideAfterDoubleHash is set, and untranslated text is
//     returned byte-for-byte (whitespace included).
//
// The dictionary (menu_zh_cn.inc) must contain at least these entries, all
// Simplified Chinese, all preserving their placeholders exactly:
//
//   "Reset"                    -> "\xE9\x87\x8D\xE7\xBD\xAE"           (重置)
//   "Close"                    -> "\xE5\x85\xB3\xE9\x97\xAD"           (关闭)
//   "FPS: %6.1f, %7.2f ms"     -> a Chinese form keeping "%6.1f"/"%7.2f"
//                                 in order (e.g. "FPS: %6.1f, %7.2f 毫秒")
//
// The game-quirk descriptions emitted by the special-handling paths
// (stringQuirks in dllmain.cpp) are user-visible too, so the same dictionary
// must also map them, keeping every ASCII token (FSR2, DX11, Vulkan, XeFG) and
// the spaces around it:
//
//   "Use FSR2 DX11 inputs"                -> "使用 FSR2 DX11 输入"
//   "Use FSR2 Vulkan inputs"              -> "使用 FSR2 Vulkan 输入"
//   "Force Borderless when using XeFG"    -> "使用 XeFG 时强制无边框窗口"
//   "Disable Resize Skip"                 -> "禁用跳过尺寸调整"
//
// Nothing here is implemented yet and no test-only API is used.

#include <clocale>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<menu/menu_i18n.h>)
#include <menu/menu_i18n.h>
#elif __has_include("../../OptiScaler/menu/menu_i18n.h")
#include "../../OptiScaler/menu/menu_i18n.h"
#elif __has_include("../OptiScaler/menu/menu_i18n.h")
#include "../OptiScaler/menu/menu_i18n.h"
#elif __has_include("menu_i18n.h")
#include "menu_i18n.h"
#else
#error "planned header OptiScaler/menu/menu_i18n.h is missing; add it (namespace OptiI18n, dictionary in menu_zh_cn.inc) so this behavioural suite can build"
#endif

// ---------------------------------------------------------------------------
// tiny check framework
// ---------------------------------------------------------------------------

namespace {

int g_checks = 0;
int g_failures = 0;

void report_failure(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, what.c_str());
}

} // namespace

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond))                                                           \
            report_failure(__FILE__, __LINE__, #cond);                         \
    } while (0)

#define CHECK_STR(actual, expected)                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        std::string _a = (actual);                                             \
        std::string _e = (expected);                                           \
        if (_a != _e) {                                                        \
            std::ostringstream _os;                                            \
            _os << #actual " == " #expected " (got \"" << _a << "\" want \""   \
                << _e << "\")";                                                \
            report_failure(__FILE__, __LINE__, _os.str());                     \
        }                                                                      \
    } while (0)

#define CHECK_PTR_EQ(a, b)                                                     \
    do {                                                                       \
        ++g_checks;                                                            \
        const void* _a = (a);                                                  \
        const void* _b = (b);                                                  \
        if (_a != _b) {                                                        \
            std::ostringstream _os;                                            \
            _os << #a " == " #b " (different pointers)";                       \
            report_failure(__FILE__, __LINE__, _os.str());                     \
        }                                                                      \
    } while (0)

namespace {

// ---------------------------------------------------------------------------
// expected dictionary values (UTF-8, spelled as escapes so the source file
// itself never depends on the compiler's execution charset)
// ---------------------------------------------------------------------------

// "Reset" and "Close" are the real native-menu keys ("Save"/"Cancel" do not
// exist in the OptiScaler menu); both English labels are 5 bytes long.
constexpr const char* kResetChinese = "\xE9\x87\x8D\xE7\xBD\xAE";  // 重置
constexpr const char* kCloseChinese = "\xE5\x85\xB3\xE9\x97\xAD";  // 关闭

// A real menu format string that is expected to be translated, used to prove
// the placeholder sequence survives translation.
constexpr const char* kFormatSource = "FPS: %6.1f, %7.2f ms";

// Game-quirk descriptions surfaced by the special-handling paths (the
// stringQuirks strings in dllmain.cpp). They are shown to the user through the
// normal ImGui display hooks, so they must translate like any other label. Each
// Chinese form keeps the embedded ASCII token (FSR2/DX11/Vulkan/XeFG) and the
// spaces around it exactly; the source is authoritative and must not be
// trimmed. Spelled as escapes so this file never depends on the compiler's
// execution charset.
constexpr const char* kQuirkFsr2Dx11Zh =           // 使用 FSR2 DX11 输入
    "\xE4\xBD\xBF\xE7\x94\xA8 FSR2 DX11 \xE8\xBE\x93\xE5\x85\xA5";
constexpr const char* kQuirkFsr2VulkanZh =         // 使用 FSR2 Vulkan 输入
    "\xE4\xBD\xBF\xE7\x94\xA8 FSR2 Vulkan \xE8\xBE\x93\xE5\x85\xA5";
constexpr const char* kQuirkXefgBorderlessZh =     // 使用 XeFG 时强制无边框窗口
    "\xE4\xBD\xBF\xE7\x94\xA8 XeFG "
    "\xE6\x97\xB6\xE5\xBC\xBA\xE5\x88\xB6\xE6\x97\xA0\xE8\xBE\xB9"
    "\xE6\xA1\x86\xE7\xAA\x97\xE5\x8F\xA3";
constexpr const char* kQuirkDisableResizeSkipZh =  // 禁用跳过尺寸调整
    "\xE7\xA6\x81\xE7\x94\xA8\xE8\xB7\xB3\xE8\xBF\x87"
    "\xE5\xB0\xBA\xE5\xAF\xB8\xE8\xB0\x83\xE6\x95\xB4";

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Every printf conversion in order, so a reordered or retyped placeholder is
// detected rather than silently accepted.
std::vector<std::string> printf_specs(const char* s) {
    std::vector<std::string> out;
    for (const char* p = s; *p != '\0';) {
        if (*p != '%') {
            ++p;
            continue;
        }
        std::string spec = "%";
        ++p;
        if (*p == '%') {
            spec += '%';
            ++p;
            out.push_back(spec);
            continue;
        }
        while (*p != '\0' && std::strchr("-+ #0", *p) != nullptr)
            spec += *p++;
        while (*p != '\0' && std::isdigit(static_cast<unsigned char>(*p)))
            spec += *p++;
        if (*p == '.') {
            spec += *p++;
            while (*p != '\0' && std::isdigit(static_cast<unsigned char>(*p)))
                spec += *p++;
        }
        while (*p != '\0' && std::strchr("hljztL", *p) != nullptr)
            spec += *p++;
        if (*p != '\0')
            spec += *p++;
        out.push_back(spec);
    }
    return out;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0)
            out += " ";
        out += parts[i];
    }
    return out;
}

// Every std::format replacement field ("{}", "{:>4}", ...) in order. An escaped
// "{{"/"}}" literal brace is not a replacement field and is skipped.
std::vector<std::string> brace_specs(const char* s) {
    std::vector<std::string> out;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p != '{') {
            continue;
        }
        if (p[1] == '{') { // "{{" literal
            ++p;
            continue;
        }
        const char* begin = p;
        while (*p != '\0' && *p != '}')
            ++p;
        if (*p == '\0')
            break;
        out.emplace_back(begin, static_cast<std::size_t>(p - begin + 1));
    }
    return out;
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

// The untouched default must be English, and it must survive a no-op set.
void test_default_language_is_english() {
    CHECK(OptiI18n::Language() == std::string_view("en"));
    OptiI18n::SetLanguage("en");
    CHECK(OptiI18n::Language() == std::string_view("en"));
}

// English is authoritative: with no dictionary hit the *same pointer* comes
// back, and English stays English even after switching to Chinese and back.
void test_english_preservation() {
    OptiI18n::SetLanguage("en");
    const char* plain = "This string is definitely not in the dictionary 42";
    CHECK_PTR_EQ(OptiI18n::Format(plain), plain);
    CHECK_STR(OptiI18n::Format(plain), plain);
    CHECK_STR(OptiI18n::Format("Reset"), "Reset");

    OptiI18n::SetLanguage("zh-CN");
    OptiI18n::SetLanguage("en");
    CHECK_STR(OptiI18n::Format("Reset"), "Reset");
    CHECK_STR(OptiI18n::TranslateRange("Close"), "Close");
}

// Case-insensitive normalisation of the two accepted Chinese spellings.
void test_language_normalisation() {
    OptiI18n::SetLanguage("zh-CN");
    CHECK(OptiI18n::Language() == std::string_view("zh-CN"));
    OptiI18n::SetLanguage("schinese");
    CHECK(OptiI18n::Language() == std::string_view("zh-CN"));
    OptiI18n::SetLanguage("SCHINESE");
    CHECK(OptiI18n::Language() == std::string_view("zh-CN"));
    OptiI18n::SetLanguage("ZH-CN");
    CHECK(OptiI18n::Language() == std::string_view("zh-CN"));
}

// Anything that is not one of the two Chinese spellings falls back to English.
void test_nonchinese_language_fallback() {
    OptiI18n::SetLanguage("fr");
    CHECK(OptiI18n::Language() == std::string_view("en"));
    CHECK_STR(OptiI18n::Format("Reset"), "Reset");

    OptiI18n::SetLanguage("en-US");
    CHECK(OptiI18n::Language() == std::string_view("en"));
    CHECK_STR(OptiI18n::Format("Close"), "Close");

    OptiI18n::SetLanguage("ja");
    CHECK(OptiI18n::Language() == std::string_view("en"));
    OptiI18n::SetLanguage("");
    CHECK(OptiI18n::Language() == std::string_view("en"));
}

// The known dictionary entries really are translated in Chinese.
void test_known_reset_close_translations() {
    OptiI18n::SetLanguage("zh-CN");
    CHECK_STR(OptiI18n::Format("Reset"), kResetChinese);
    CHECK_STR(OptiI18n::Format("Close"), kCloseChinese);
    CHECK_STR(OptiI18n::TranslateRange("Reset"), kResetChinese);

    // The returned pointer is stable across calls.
    CHECK_PTR_EQ(OptiI18n::Format("Reset"), OptiI18n::Format("Reset"));
}

// Switching at runtime must take effect immediately in both directions.
void test_runtime_switching() {
    OptiI18n::SetLanguage("en");
    CHECK_STR(OptiI18n::Format("Reset"), "Reset");

    OptiI18n::SetLanguage("zh-CN");
    CHECK_STR(OptiI18n::Format("Reset"), kResetChinese);

    OptiI18n::SetLanguage("en");
    CHECK_STR(OptiI18n::Format("Reset"), "Reset");

    OptiI18n::SetLanguage("zh-CN");
    CHECK_STR(OptiI18n::Format("Reset"), kResetChinese);
}

// Unknown text is returned byte-for-byte, never mangled or emptied.
void test_unknown_text_exact_fallback() {
    OptiI18n::SetLanguage("zh-CN");
    const char* src = "Not translated: \xE2\x9C\x93 12345";
    CHECK_PTR_EQ(OptiI18n::Format(src), src);
    CHECK_STR(OptiI18n::TranslateRange(src), src);
}

// Whitespace in the source is preserved; lookups are exact, not trimmed.
void test_source_whitespace_preserved() {
    OptiI18n::SetLanguage("zh-CN");
    CHECK_STR(OptiI18n::TranslateRange("  Reset  "), "  Reset  ");
    CHECK_STR(OptiI18n::TranslateRange("\tReset"), "\tReset");
    CHECK_STR(OptiI18n::Format(" Reset "), " Reset ");
    CHECK_STR(OptiI18n::TranslateRange("a  b\n c"), "a  b\n c");
}

// The range and its optional end pointer bound exactly what is translated.
void test_bounded_ranges() {
    OptiI18n::SetLanguage("zh-CN");

    // "Reset" is 5 bytes, so the visible range before "##" is [label, label+5).
    const char* label = "Reset##button";
    CHECK_STR(OptiI18n::TranslateRange(label, label + 5), kResetChinese);

    std::string mixed = "xx Reset yy";
    CHECK_STR(OptiI18n::TranslateRange(mixed.data() + 3, mixed.data() + 8),
              kResetChinese);

    // A partial word is not a dictionary key: the substring comes back intact.
    CHECK_STR(OptiI18n::TranslateRange(label, label + 2), "Re");

    // end == nullptr means "up to the NUL terminator".
    CHECK_STR(OptiI18n::TranslateRange("Reset"), kResetChinese);
}

// "##id" is an ImGui id: keep it when rendering normally, drop it when asked.
void test_rendered_label_double_hash() {
    OptiI18n::SetLanguage("zh-CN");

    // Not hidden: the id survives untouched, only the visible part changes.
    CHECK_STR(OptiI18n::TranslateRange("Reset##button"),
              std::string(kResetChinese) + "##button");

    // Hidden: the result is the visible part alone.
    CHECK_STR(OptiI18n::TranslateRange("Reset##button", nullptr, true),
              kResetChinese);

    // Untranslated text keeps its id too, and only loses it when hidden.
    CHECK_STR(OptiI18n::TranslateRange("Weird##identifier"),
              "Weird##identifier");
    CHECK_STR(OptiI18n::TranslateRange("Weird##identifier", nullptr, true),
              "Weird");
}

// A translated format must keep the exact placeholder sequence and still be
// usable in a real printf call.
void test_printf_placeholders_unchanged() {
    OptiI18n::SetLanguage("zh-CN");

    const char* translated = OptiI18n::Format(kFormatSource);
    CHECK(translated != nullptr);

    // It must actually be a translation, not the English source.
    CHECK(std::string(translated) != std::string(kFormatSource));

    // Order and count of printf conversions are unchanged.
    std::vector<std::string> want = printf_specs(kFormatSource);
    std::vector<std::string> got = printf_specs(translated);
    CHECK_STR(join(got), join(want));

    // And it really formats: the arguments still land in the string.
    char buffer[128];
    int n = std::snprintf(buffer, sizeof buffer, translated, 59.5, 118.0);
    CHECK(n > 0);
    std::string formatted(buffer);
    CHECK(formatted.find("59.5") != std::string::npos);
    CHECK(formatted.find("118.0") != std::string::npos);
}

// Native notification strings (supplement dictionary) translate as text, and a
// dynamically formatted one keeps the original braced placeholders in order so
// std::format still substitutes correctly; English remains the fallback.
void test_native_notification_translations() {
    // A static toast title/content is translated, so it differs from English.
    OptiI18n::SetLanguage("zh-CN");
    const char* static_en = "RTSS + XeFG detected";
    const char* static_zh = OptiI18n::Format(static_en);
    CHECK(static_zh != nullptr);
    CHECK(std::string(static_zh) != std::string(static_en));
    CHECK(!std::string(static_zh).empty());

    // A dynamically formatted notification keeps the exact {}-placeholder
    // sequence and newline shape of the English source.
    const char* dyn_en = "{} is not available.\nFalling back to {}.";
    const char* dyn_zh = OptiI18n::Format(dyn_en);
    CHECK(dyn_zh != nullptr);
    CHECK(std::string(dyn_zh) != std::string(dyn_en));
    std::vector<std::string> dyn_want = brace_specs(dyn_en);
    std::vector<std::string> dyn_got = brace_specs(dyn_zh);
    CHECK(dyn_want.size() == 2);
    CHECK_STR(join(dyn_got), join(dyn_want));
    CHECK(std::string(dyn_zh).find('\n') != std::string::npos);

    // English is still the source of truth: switching back returns the very
    // pointers that were passed in, untranslated and byte-for-byte.
    OptiI18n::SetLanguage("en");
    CHECK_PTR_EQ(OptiI18n::Format(static_en), static_en);
    CHECK_PTR_EQ(OptiI18n::Format(dyn_en), dyn_en);
    CHECK_STR(OptiI18n::Format(static_en), static_en);
}

// The game-quirk descriptions raised by the special-handling paths are real,
// user-visible labels (they appear under an active quirk through the normal
// display hooks), so in Chinese they must read as Chinese while keeping every
// embedded ASCII token and the spaces around it.
void test_game_quirk_descriptions_translate() {
    OptiI18n::SetLanguage("zh-CN");

    CHECK_STR(OptiI18n::Format("Use FSR2 DX11 inputs"), kQuirkFsr2Dx11Zh);
    CHECK_STR(OptiI18n::Format("Use FSR2 Vulkan inputs"), kQuirkFsr2VulkanZh);
    CHECK_STR(OptiI18n::Format("Force Borderless when using XeFG"),
              kQuirkXefgBorderlessZh);
    CHECK_STR(OptiI18n::Format("Disable Resize Skip"),
              kQuirkDisableResizeSkipZh);

    // The same source through the range API yields the same Chinese text.
    CHECK_STR(OptiI18n::TranslateRange("Use FSR2 DX11 inputs"),
              kQuirkFsr2Dx11Zh);

    // The translated pointers are stable across calls.
    CHECK_PTR_EQ(OptiI18n::Format("Use FSR2 DX11 inputs"),
                 OptiI18n::Format("Use FSR2 DX11 inputs"));
}

// The very same four sources must come back byte-for-byte, at the identical
// pointer, for English and for every language that is not Chinese: the
// dictionary is Chinese-only, so no other tag may alter these strings.
void test_game_quirk_descriptions_fall_back_verbatim() {
    const char* sources[] = {
        "Use FSR2 DX11 inputs",
        "Use FSR2 Vulkan inputs",
        "Force Borderless when using XeFG",
        "Disable Resize Skip",
    };
    const char* languages[] = {"en", "fr", "ja"};

    for (const char* lang : languages) {
        OptiI18n::SetLanguage(lang);
        CHECK(OptiI18n::Language() == std::string_view("en"));
        for (const char* src : sources) {
            CHECK_PTR_EQ(OptiI18n::Format(src), src);
            CHECK_STR(OptiI18n::Format(src), src);
            CHECK_STR(OptiI18n::TranslateRange(src), src);
        }
    }
}

// Translating never mutates the caller's buffer and doesn't depend on the
// process locale.
void test_no_locale_effect_on_source_buffers() {
    OptiI18n::SetLanguage("zh-CN");

    // "Reset" is 5 bytes + NUL; give the raw buffer comfortable headroom.
    char buffer[32];
    std::strcpy(buffer, "Reset");
    CHECK_STR(OptiI18n::TranslateRange(buffer), kResetChinese);
    CHECK(std::strcmp(buffer, "Reset") == 0); // untouched

    char close_buffer[32];
    std::strcpy(close_buffer, "Close");
    CHECK_STR(OptiI18n::TranslateRange(close_buffer), kCloseChinese);
    CHECK(std::strcmp(close_buffer, "Close") == 0); // untouched

    char fmt_buffer[64];
    std::strcpy(fmt_buffer, kFormatSource);
    (void)OptiI18n::Format(fmt_buffer);
    CHECK(std::strcmp(fmt_buffer, kFormatSource) == 0);

    // Changing the C locale must not change the mapping.
    std::setlocale(LC_ALL, "C");
    CHECK_STR(OptiI18n::Format("Reset"), kResetChinese);
    std::setlocale(LC_ALL, "");
    CHECK_STR(OptiI18n::Format("Close"), kCloseChinese);
}

} // namespace

// ---------------------------------------------------------------------------

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        {"default language is english", test_default_language_is_english},
        {"english sources are preserved verbatim", test_english_preservation},
        {"language names normalise to zh-CN/en", test_language_normalisation},
        {"non-chinese languages fall back to english", test_nonchinese_language_fallback},
        {"Reset and Close have known translations", test_known_reset_close_translations},
        {"language can be switched at runtime", test_runtime_switching},
        {"unknown text falls back byte-for-byte", test_unknown_text_exact_fallback},
        {"source whitespace is preserved", test_source_whitespace_preserved},
        {"ranges respect the end pointer", test_bounded_ranges},
        {"rendered labels keep their ## id unless hidden", test_rendered_label_double_hash},
        {"printf placeholders survive translation", test_printf_placeholders_unchanged},
        {"native notification strings translate and keep placeholders", test_native_notification_translations},
        {"game-quirk descriptions translate to chinese", test_game_quirk_descriptions_translate},
        {"game-quirk descriptions fall back verbatim off chinese", test_game_quirk_descriptions_fall_back_verbatim},
        {"translation never mutates sources or the locale", test_no_locale_effect_on_source_buffers},
    };

    std::printf("OptiI18n menu translation behaviour tests (%zu cases)\n",
                sizeof(cases) / sizeof(cases[0]));
    for (const auto& c : cases) {
        int before = g_failures;
        std::printf("- %s\n", c.name);
        std::fflush(stdout);
        c.fn();
        if (g_failures == before)
            std::printf("  ok\n");
        else
            std::printf("  FAILED\n");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
