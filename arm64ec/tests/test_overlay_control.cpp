// Behaviour tests for the direct overlay-control bridge.
//
// These tests exercise the planned production header
// `OptiScaler/menu/overlay_control.h` (namespace `OptiControl`). That header is
// added by the control change; until it exists this file fails to compile with
// one clear #error naming the missing path, which is the intended failure.
//
// The suite is standalone and portable: standard library only, C++20/23, no
// Windows headers, no ImGui, no threads. Everything the tests need — temp dirs,
// atomic request writes, state readback — is real filesystem I/O driven from the
// single test thread, exactly the way the plugin and the in-game core would
// cooperate.
//
// Build (any modern compiler):
//   c++ -std=c++20 -O2 -Wall -Wextra -o test_overlay_control test_overlay_control.cpp
// With the arm64ec tree, add: -IOptiScaler  (so <menu/overlay_control.h> resolves)
//
// Run: ./test_overlay_control   (exit code 0 = all checks passed)

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#if __has_include(<menu/overlay_control.h>)
#include <menu/overlay_control.h>
#elif __has_include("../../OptiScaler/menu/overlay_control.h")
#include "../../OptiScaler/menu/overlay_control.h"
#elif __has_include("../OptiScaler/menu/overlay_control.h")
#include "../OptiScaler/menu/overlay_control.h"
#elif __has_include("overlay_control.h")
#include "overlay_control.h"
#else
#error "planned header OptiScaler/menu/overlay_control.h is missing; add it (namespace OptiControl) so this behavioural suite can build"
#endif

namespace fs = std::filesystem;

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

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond))                                                           \
            report_failure(__FILE__, __LINE__, #cond);                         \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++g_checks;                                                            \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (!(_a == _b)) {                                                     \
            std::ostringstream _os;                                            \
            _os << #a " == " #b " (" << _a << " vs " << _b << ")";             \
            report_failure(__FILE__, __LINE__, _os.str());                     \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

std::string unique_suffix() {
    static std::mt19937_64 rng(
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream os;
    os << std::hex << dist(rng);
    return os.str();
}

// A fresh directory under the system temp dir, removed on destruction.
class TempDir {
public:
    explicit TempDir(const char* tag) {
        path_ = fs::temp_directory_path() /
                ("opti-control-test-" + std::string(tag) + "-" + unique_suffix());
        std::error_code ec;
        fs::create_directories(path_, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec); // best effort; test-local scratch only
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }
    fs::path file(const std::string& name) const { return path_ / name; }

private:
    fs::path path_;
};

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_text(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

// Atomic replace the way the plugin does: write a sibling temp file, then move
// it over the destination (removing an existing destination first on
// platforms where rename() refuses to overwrite).
void atomic_replace(const fs::path& dest, const std::string& content) {
    fs::path tmp = dest;
    tmp += ".tmp" + unique_suffix();
    write_text(tmp, content);

    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(dest, ec);
        ec.clear();
        fs::rename(tmp, dest, ec);
    }
    if (ec)
        fs::remove(tmp, ec);
}

std::map<std::string, std::string> parse_kv(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        out.emplace(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

std::string make_request(const std::string& session, const std::string& request,
                         const std::string& action, const std::string& value) {
    return "version=1\n"
           "session=" + session + "\n"
           "request=" + request + "\n"
           "action=" + action + "\n"
           "value=" + value + "\n";
}

// Emulate the plugin writing a command.
void write_request(const fs::path& dir, const std::string& session,
                   const std::string& request, const std::string& action,
                   const std::string& value) {
    atomic_replace(dir / "overlay.request", make_request(session, request, action, value));
}

std::map<std::string, std::string> read_state(const fs::path& dir) {
    return parse_kv(read_text(dir / "overlay.state"));
}

// Stable hex session/request nonces used across the suite.
const std::string kSessionA = "0123456789abcdef0123456789abcdef";
const std::string kSessionB = "fedcba9876543210fedcba9876543210";

std::string nonce(int n) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i)
        s.push_back(kHex[(n * 7 + i * 13 + 5) & 0xF]);
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

static void test_reads_menu_show_request() {
    TempDir dir("menu-show");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui; // menu hidden to begin with
    write_request(dir.path(), kSessionA, nonce(1), "menu", "1");

    auto action = bridge.poll(250, ui);
    CHECK(action.has_value());
    if (action) {
        CHECK(action->target == OptiControl::Target::Menu);
        CHECK_EQ(action->enabled, true);
    }

    // Caller applies it and reports back: no further command, but the state
    // file must now acknowledge it.
    ui.menuVisible = true;
    auto second = bridge.poll(250, ui);
    CHECK(!second.has_value());

    auto st = read_state(dir.path());
    CHECK_EQ(st["version"], std::string("1"));
    CHECK_EQ(st["session"], kSessionA);
    CHECK_EQ(st["request"], nonce(1));
    CHECK_EQ(st["menu"], std::string("1"));
    CHECK_EQ(st["ready"], std::string("1"));
}

static void test_false_value_disables_menu() {
    TempDir dir("menu-hide");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    ui.menuVisible = true;
    write_request(dir.path(), kSessionA, nonce(2), "menu", "0");

    auto action = bridge.poll(250, ui);
    CHECK(action.has_value());
    if (action) {
        CHECK(action->target == OptiControl::Target::Menu);
        CHECK_EQ(action->enabled, false);
    }

    ui.menuVisible = false;
    CHECK(!bridge.poll(250, ui).has_value());
    auto st = read_state(dir.path());
    CHECK_EQ(st["menu"], std::string("0"));
    CHECK_EQ(st["ready"], std::string("1"));
}

static void test_fps_action() {
    TempDir dir("fps");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(dir.path(), kSessionA, nonce(3), "fps", "1");

    auto action = bridge.poll(250, ui);
    CHECK(action.has_value());
    if (action) {
        CHECK(action->target == OptiControl::Target::Fps);
        CHECK_EQ(action->enabled, true);
    }

    ui.fpsVisible = true;
    CHECK(!bridge.poll(250, ui).has_value());
    auto st = read_state(dir.path());
    CHECK_EQ(st["fps"], std::string("1"));
    CHECK_EQ(st["ready"], std::string("1"));
}

static void test_language_does_not_touch_menu_or_fps() {
    TempDir dir("locale");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui; // menu and fps both hidden
    write_request(dir.path(), kSessionA, nonce(4), "language", "zh-CN");

    auto action = bridge.poll(250, ui);
    CHECK(action.has_value());
    if (action) {
        CHECK(action->target == OptiControl::Target::Language);
        CHECK_EQ(action->language, std::string("zh-CN"));
        CHECK_EQ(action->enabled, false);
    }

    ui.language = "zh-CN";
    CHECK(!bridge.poll(250, ui).has_value());

    auto st = read_state(dir.path());
    CHECK_EQ(st["language"], std::string("zh-CN"));
    // A locale switch must never implicitly reveal the menu or the FPS overlay.
    CHECK_EQ(st["menu"], std::string("0"));
    CHECK_EQ(st["fps"], std::string("0"));
    CHECK_EQ(st["ready"], std::string("1"));
}

static void test_unchanged_command_is_not_replayed() {
    TempDir dir("no-replay");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(dir.path(), kSessionA, nonce(5), "menu", "1");

    CHECK(bridge.poll(250, ui).has_value());
    ui.menuVisible = true;

    // The request file still holds the same nonce; every later poll is a no-op.
    for (int i = 0; i < 4; ++i)
        CHECK(!bridge.poll(250, ui).has_value());
}

static void test_wrong_session_is_ignored() {
    TempDir dir("wrong-session");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(dir.path(), kSessionA, nonce(6), "menu", "1");
    // Overwrite with a command tagged for a different launch.
    write_request(dir.path(), kSessionB, nonce(7), "menu", "1");

    CHECK(!bridge.poll(250, ui).has_value());
    auto st = read_state(dir.path());
    // Nothing may be acknowledged for a foreign session.
    if (st.count("ready"))
        CHECK_EQ(st["ready"], std::string("0"));
    if (st.count("request"))
        CHECK(st["request"] != nonce(7));
}

static void test_restart_prevents_replay() {
    TempDir dir("restart");
    {
        OptiControl::Bridge first(dir.path(), kSessionA);
        OptiControl::UiState ui;
        write_request(dir.path(), kSessionA, nonce(8), "menu", "1");
        CHECK(first.poll(250, ui).has_value());
    }
    // A new launch, new session. The old request file is still on disk.
    OptiControl::Bridge second(dir.path(), kSessionB);
    OptiControl::UiState ui;
    CHECK(!second.poll(250, ui).has_value());
}

static void test_unknown_field_rejected() {
    TempDir dir("unknown-field");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    std::string body = make_request(kSessionA, nonce(9), "menu", "1");
    body += "surprise=yes\n";
    atomic_replace(dir.path() / "overlay.request", body);

    OptiControl::UiState ui;
    CHECK(!bridge.poll(250, ui).has_value());
}

static void test_duplicate_field_rejected() {
    TempDir dir("duplicate-field");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    std::string body = "version=1\n"
                       "session=" + kSessionA + "\n"
                       "request=" + nonce(10) + "\n"
                       "action=menu\n"
                       "action=fps\n"
                       "value=1\n";
    atomic_replace(dir.path() / "overlay.request", body);

    OptiControl::UiState ui;
    CHECK(!bridge.poll(250, ui).has_value());
}

static void test_oversized_request_rejected() {
    TempDir dir("oversize");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    // A structurally valid request padded well past the 4096-byte cap.
    std::string body = make_request(kSessionA, nonce(11), "menu", "1");
    body.append(6000, '\n');
    CHECK(body.size() > 4096);
    atomic_replace(dir.path() / "overlay.request", body);

    OptiControl::UiState ui;
    CHECK(!bridge.poll(250, ui).has_value());
}

static void test_malformed_requests_rejected() {
    const std::vector<std::string> bad = {
        // missing version
        "session=" + kSessionA + "\nrequest=" + nonce(12) + "\naction=menu\nvalue=1\n",
        // bad protocol version
        "version=2\nsession=" + kSessionA + "\nrequest=" + nonce(13) + "\naction=menu\nvalue=1\n",
        // unknown action
        "version=1\nsession=" + kSessionA + "\nrequest=" + nonce(14) + "\naction=volume\nvalue=1\n",
        // invalid value for menu
        "version=1\nsession=" + kSessionA + "\nrequest=" + nonce(15) + "\naction=menu\nvalue=2\n",
        // invalid language
        "version=1\nsession=" + kSessionA + "\nrequest=" + nonce(16) + "\naction=language\nvalue=fr\n",
        // line without '='
        "version=1\nsession=" + kSessionA + "\nrequest=" + nonce(17) + "\nactionmenu\nvalue=1\n",
        // not a request at all
        "hello world\n",
    };

    for (std::size_t i = 0; i < bad.size(); ++i) {
        TempDir dir("malformed");
        OptiControl::Bridge bridge(dir.path(), kSessionA);
        atomic_replace(dir.path() / "overlay.request", bad[i]);
        OptiControl::UiState ui;
        CHECK(!bridge.poll(250, ui).has_value());
    }
}

static void test_ack_requires_actual_state() {
    TempDir dir("ack");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui; // menu still hidden
    write_request(dir.path(), kSessionA, nonce(18), "menu", "1");
    CHECK(bridge.poll(250, ui).has_value());

    // Caller has not applied it yet: state must not claim ready.
    CHECK(!bridge.poll(250, ui).has_value());
    auto pending = read_state(dir.path());
    CHECK_EQ(pending["menu"], std::string("0"));
    CHECK_EQ(pending["ready"], std::string("0"));

    // Now the caller really flipped it.
    ui.menuVisible = true;
    CHECK(!bridge.poll(250, ui).has_value());
    auto acked = read_state(dir.path());
    CHECK_EQ(acked["menu"], std::string("1"));
    CHECK_EQ(acked["ready"], std::string("1"));
}

static void test_poll_rate_limits_reads() {
    TempDir dir("rate");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(dir.path(), kSessionA, nonce(19), "menu", "1");

    // Under the 250 ms poll interval the bridge must not even look yet.
    CHECK(!bridge.poll(100, ui).has_value());
    CHECK(!bridge.poll(100, ui).has_value());
    // Crossing the interval finally yields the action.
    auto action = bridge.poll(100, ui);
    CHECK(action.has_value());
}

static void test_distinct_directories_are_isolated() {
    TempDir a("dir-a");
    TempDir b("dir-b");
    OptiControl::Bridge bridge_a(a.path(), kSessionA);
    OptiControl::Bridge bridge_b(b.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(a.path(), kSessionA, nonce(20), "menu", "1");

    CHECK(!bridge_b.poll(250, ui).has_value());
    CHECK(bridge_a.poll(250, ui).has_value());
}

static void test_state_file_shape_and_no_temp_leftovers() {
    TempDir dir("state-shape");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    write_request(dir.path(), kSessionA, nonce(21), "language", "zh-CN");
    CHECK(bridge.poll(250, ui).has_value());
    ui.language = "zh-CN";
    bridge.poll(250, ui);

    auto st = read_state(dir.path());
    for (const char* key : {"version", "session", "request", "menu", "fps", "language", "ready"})
        CHECK_EQ(st.count(key), std::size_t(1)); // each key exactly once

    // Only the two protocol files may exist; no temp files may survive.
    for (const auto& entry : fs::directory_iterator(dir.path())) {
        std::string name = entry.path().filename().string();
        CHECK(name == "overlay.request" || name == "overlay.state");
    }
}

static void test_missing_request_is_quiet() {
    TempDir dir("no-request");
    OptiControl::Bridge bridge(dir.path(), kSessionA);
    OptiControl::UiState ui;
    // Nothing written at all: polling must be a harmless no-op.
    CHECK(!bridge.poll(250, ui).has_value());
    CHECK(!bridge.poll(1000, ui).has_value());
}

// Regression: a failed rename must never delete the pre-existing destination.
// The old "remove and retry" fallback destroyed the destination; the fix keeps
// it and removes only the bridge's own temp file.
static void test_atomic_write_keeps_destination_on_rename_failure() {
    TempDir dir("atomic-fail");

    // Case 1: destination is an EMPTY directory. Removing it would succeed, so
    // the old burn-and-retry logic silently replaced it with a file. The fix
    // must leave the directory untouched.
    const fs::path destDir = dir.file("overlay.state");
    fs::create_directories(destDir);
    CHECK(!OptiControl::detail::atomic_write(destDir, "new-content"));
    CHECK(fs::exists(destDir));
    CHECK(fs::is_directory(destDir));

    // Case 2: destination directory carries a sentinel that must survive.
    const fs::path nested = dir.file("nested");
    fs::create_directories(nested);
    const fs::path sentinel = nested / "sentinel";
    write_text(sentinel, "keep-me");
    CHECK(!OptiControl::detail::atomic_write(nested, "new-content"));
    CHECK(fs::exists(sentinel));
    CHECK_EQ(read_text(sentinel), std::string("keep-me"));

    // No temp files may be left behind anywhere under the test directory.
    for (const auto& entry : fs::recursive_directory_iterator(dir.path()))
        CHECK(entry.path().filename().string().find(".tmp") == std::string::npos);
}

// Regression: a brand-new command must publish ready=0 even when the incoming
// actual state already equals the requested value. Only a later poll may
// acknowledge it.
static void test_noop_request_requires_subsequent_poll_to_ack() {
    TempDir dir("noop-ack");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui;
    ui.menuVisible = true; // already matches the incoming request
    write_request(dir.path(), kSessionA, nonce(50), "menu", "1");

    auto action = bridge.poll(250, ui);
    CHECK(action.has_value());
    if (action) {
        CHECK(action->target == OptiControl::Target::Menu);
        CHECK_EQ(action->enabled, true);
    }

    // First publication of a fresh command must not acknowledge, even though the
    // requested value already holds.
    auto first = read_state(dir.path());
    CHECK_EQ(first["request"], nonce(50));
    CHECK_EQ(first["menu"], std::string("1"));
    CHECK_EQ(first["ready"], std::string("0"));

    // Only a subsequent poll may acknowledge.
    CHECK(!bridge.poll(250, ui).has_value());
    auto second = read_state(dir.path());
    CHECK_EQ(second["ready"], std::string("1"));
}

// Regression: session/request tokens must be 16..64 char lowercase hex.
static void test_rejects_invalid_tokens() {
    const std::vector<std::string> bad = {
        "",                                  // empty
        "0123456789abcde",                   // 15 chars: too short
        std::string(65, 'a'),                // 65 chars: too long
        "0123456789ABCDEF0123456789ABCDEF",  // uppercase is not lowercase hex
        "0123456789abcdeg0123456789abcdeg",  // 'g' is not hex
        "ghijklmnopqrstuv",                  // non-hex letters
    };

    for (const auto& tok : bad) {
        TempDir dir("bad-nonce");
        OptiControl::Bridge bridge(dir.path(), kSessionA);
        atomic_replace(dir.file("overlay.request"), make_request(kSessionA, tok, "menu", "1"));
        OptiControl::UiState ui;
        CHECK(!bridge.poll(250, ui).has_value());
    }

    // An uppercase session that exactly matches the bridge's own session must
    // still be rejected purely on the token grammar.
    {
        TempDir dir("upper-session");
        const std::string upper = "0123456789ABCDEF0123456789ABCDEF";
        OptiControl::Bridge bridge(dir.path(), upper);
        atomic_replace(dir.file("overlay.request"),
                       make_request(upper, nonce(41), "menu", "1"));
        OptiControl::UiState ui;
        CHECK(!bridge.poll(250, ui).has_value());
    }

    // Boundary lengths are accepted.
    {
        const std::string t16 = "0123456789abcdef";
        TempDir dir("len16");
        OptiControl::Bridge bridge(dir.path(), t16);
        atomic_replace(dir.file("overlay.request"), make_request(t16, t16, "menu", "1"));
        OptiControl::UiState ui;
        CHECK(bridge.poll(250, ui).has_value());
    }
    {
        const std::string t64(64, 'a');
        TempDir dir("len64");
        OptiControl::Bridge bridge(dir.path(), t64);
        atomic_replace(dir.file("overlay.request"), make_request(t64, t64, "menu", "1"));
        OptiControl::UiState ui;
        CHECK(bridge.poll(250, ui).has_value());
    }
}

// Regression: never follow a symlinked request out of the supplied directory.
static void test_rejects_request_symlink() {
    TempDir dir("req-symlink");
    TempDir outside("req-symlink-out");
    write_text(outside.file("overlay.request"),
               make_request(kSessionA, nonce(30), "menu", "1"));

    std::error_code ec;
    fs::create_symlink(outside.file("overlay.request"), dir.file("overlay.request"), ec);
    if (ec)
        return; // platform disallows symlinks: nothing to assert

    OptiControl::Bridge bridge(dir.path(), kSessionA);
    OptiControl::UiState ui;
    for (int i = 0; i < 3; ++i)
        CHECK(!bridge.poll(250, ui).has_value());
}

// Regression: never write the state through a symlink to another location.
static void test_rejects_state_symlink() {
    TempDir dir("state-symlink");
    TempDir outside("state-symlink-out");
    const fs::path victim = outside.file("victim.state");
    write_text(victim, "ORIGINAL");

    std::error_code ec;
    fs::create_symlink(victim, dir.file("overlay.state"), ec);
    if (ec)
        return; // platform disallows symlinks: nothing to assert

    OptiControl::Bridge bridge(dir.path(), kSessionA);
    OptiControl::UiState ui;
    bridge.poll(250, ui);
    bridge.poll(1000, ui);

    // The outside victim must never be written through the link, and the link
    // itself must not be replaced by a regular file.
    CHECK_EQ(read_text(victim), std::string("ORIGINAL"));
    CHECK(fs::is_symlink(dir.file("overlay.state")));
}

// Regression: refuse a symlinked directory so nothing is read from or written
// into a directory the caller did not actually hand us.
static void test_rejects_symlink_directory() {
    TempDir outside("symlink-dir-out");
    write_text(outside.file("overlay.request"),
               make_request(kSessionA, nonce(31), "menu", "1"));

    TempDir parent("symlink-dir-parent");
    const fs::path linkdir = parent.file("bridge");
    std::error_code ec;
    fs::create_symlink(outside.path(), linkdir, ec);
    if (ec)
        return; // platform disallows symlinks: nothing to assert

    OptiControl::Bridge bridge(linkdir, kSessionA);
    OptiControl::UiState ui;
    for (int i = 0; i < 3; ++i)
        CHECK(!bridge.poll(250, ui).has_value());

    // Nothing may have been created inside the real (outside) directory.
    CHECK(!fs::exists(outside.file("overlay.state")));
}

// Regression: any change to the displayed actual state must be published on the
// poll where it is observed, not merely at the 1 s heartbeat.
static void test_actual_state_change_publishes_promptly() {
    TempDir dir("prompt-publish");
    OptiControl::Bridge bridge(dir.path(), kSessionA);

    OptiControl::UiState ui; // menu and fps hidden, language "en"
    bridge.poll(250, ui);
    CHECK_EQ(read_state(dir.path())["menu"], std::string("0"));

    ui.menuVisible = true;
    // Well below the 1 s heartbeat: the change must still reach disk promptly.
    bridge.poll(10, ui);
    CHECK_EQ(read_state(dir.path())["menu"], std::string("1"));

    ui.fpsVisible = true;
    ui.language  = "zh-CN";
    bridge.poll(10, ui);
    auto st = read_state(dir.path());
    CHECK_EQ(st["fps"], std::string("1"));
    CHECK_EQ(st["language"], std::string("zh-CN"));
}

// ---------------------------------------------------------------------------

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        {"reads a menu-show request and acknowledges it", test_reads_menu_show_request},
        {"menu value 0 disables the menu", test_false_value_disables_menu},
        {"fps value 1 shows the FPS overlay", test_fps_action},
        {"language change never touches menu or fps", test_language_does_not_touch_menu_or_fps},
        {"unchanged commands are not replayed", test_unchanged_command_is_not_replayed},
        {"wrong session is ignored", test_wrong_session_is_ignored},
        {"restart prevents replay of a stale command", test_restart_prevents_replay},
        {"unknown fields are rejected", test_unknown_field_rejected},
        {"duplicate fields are rejected", test_duplicate_field_rejected},
        {"oversized requests are rejected", test_oversized_request_rejected},
        {"malformed requests are rejected", test_malformed_requests_rejected},
        {"acknowledgement requires the actual applied state", test_ack_requires_actual_state},
        {"poll rate limits how often the request is read", test_poll_rate_limits_reads},
        {"distinct target directories are isolated", test_distinct_directories_are_isolated},
        {"state file has the documented shape with no temp leftovers",
         test_state_file_shape_and_no_temp_leftovers},
        {"missing request is a quiet no-op", test_missing_request_is_quiet},
        {"failed rename keeps the existing destination and drops only our temp",
         test_atomic_write_keeps_destination_on_rename_failure},
        {"a no-op command still needs a later poll to acknowledge",
         test_noop_request_requires_subsequent_poll_to_ack},
        {"session and request tokens must be 16..64 lowercase hex",
         test_rejects_invalid_tokens},
        {"a symlinked request is never followed", test_rejects_request_symlink},
        {"a symlinked state file is never written through", test_rejects_state_symlink},
        {"a symlinked directory is refused", test_rejects_symlink_directory},
        {"actual UI state changes are published promptly", test_actual_state_change_publishes_promptly},
    };

    std::printf("OptiControl bridge behaviour tests (%zu cases)\n", sizeof(cases) / sizeof(cases[0]));
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
