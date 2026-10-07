// Direct overlay-control bridge (production header).
//
// The in-game core owns this bridge. It polls the plugin's mailbox on disk,
// turns a valid request into a DesiredAction for the caller (the game's render
// loop) to apply, and publishes the *actual* UI state the caller reports back.
// Acknowledgement (ready=1) is produced only on a later poll once the reported
// actual state already matches the accepted request; the first read of a
// pending request never acknowledges. Standard library only: no Windows, no
// ImGui, no key simulation, no threads, no sockets. File errors are never
// thrown into rendering; an unavailable mailbox simply yields no action and an
// unacknowledged state.
//
// See work/test-api.md for the full mailbox contract.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace OptiControl {

inline constexpr int         kProtocolVersion = 1;
inline constexpr int         kPollIntervalMs  = 250;  // request read rate
inline constexpr int         kHeartbeatMs     = 1000; // state publish rate
inline constexpr std::size_t kMaxRequestBytes = 4096;
// Opaque session/request tokens are lowercase hex of a bounded length. 16..64
// is the accepted grammar; production hands out 32-character tokens.
inline constexpr std::size_t kMinTokenLen     = 16;
inline constexpr std::size_t kMaxTokenLen     = 64;

inline constexpr char kRequestFile[] = "overlay.request";
inline constexpr char kStateFile[]   = "overlay.state";

enum class Target { Menu, Fps, Language };

struct DesiredAction {
    Target      target  = Target::Menu;
    bool        enabled = false; // Menu / Fps
    std::string language;        // "en" / "zh-CN" for Language
};

struct UiState {
    bool        menuVisible = false;
    bool        fpsVisible  = false;
    std::string language    = "en";
};

namespace detail {

// A unique, sibling temporary suffix. Single-threaded use only (no threads in
// this task): a monotonic counter is combined with the steady clock so two
// writes in the same call never collide.
inline std::string unique_suffix() {
    static std::uint64_t counter = 0;
    ++counter;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::uint64_t mix = now ^ (counter * 0x9E3779B97F4A7C15ull);
    char buf[17];
    for (int i = 15; i >= 0; --i) {
        const int nib = static_cast<int>(mix & 0xF);
        buf[i] = static_cast<char>(nib < 10 ? ('0' + nib) : ('a' + (nib - 10)));
        mix >>= 4;
    }
    buf[16] = '\0';
    return std::string("tmp") + buf;
}

// Non-empty lowercase hexadecimal of a bounded length: the only token grammar
// accepted for session and request ids.
inline bool is_lower_hex(const std::string& s, std::size_t minLen, std::size_t maxLen) {
    if (s.size() < minLen || s.size() > maxLen)
        return false;
    for (const char c : s) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower)
            return false;
    }
    return true;
}

// True only when `p` is known to be a symbolic link. A missing or unreadable
// path is reported as "not a symlink" so the caller's normal missing-file
// handling applies; this never throws.
inline bool is_symlink_at(const std::filesystem::path& p) {
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(p, ec);
    return !ec && std::filesystem::is_symlink(st);
}

// A directory the bridge may operate in: it exists, is a directory, and is not
// itself a symbolic link. Checked without throwing.
inline bool safe_directory(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(dir, ec);
    if (ec)
        return false;
    return std::filesystem::is_directory(st) && !std::filesystem::is_symlink(st);
}

// Reads at most `maxBytes` from `p`. The read itself is bounded to
// maxBytes + 1 bytes so an oversized file is rejected even when its reported
// size lied or it grew after any prior check; a symlink is never followed. All
// errors are surfaced as `false`, never thrown.
inline bool read_bounded(const std::filesystem::path& p, std::size_t maxBytes,
                         std::string& out) {
    out.clear();
    if (is_symlink_at(p))
        return false; // never follow a symlink out of the supplied directory

    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false; // missing / unreadable => treated as absent

    const std::size_t limit = maxBytes + 1; // one byte past the cap detects oversize
    char buf[4096];
    while (out.size() < limit) {
        const std::size_t want =
            std::min<std::size_t>(sizeof(buf), limit - out.size());
        in.read(buf, static_cast<std::streamsize>(want));
        const std::streamsize got = in.gcount();
        if (got <= 0)
            break;
        out.append(buf, static_cast<std::size_t>(got));
        if (static_cast<std::size_t>(got) < want)
            break; // end of file reached
    }
    return out.size() <= maxBytes;
}

// Atomic replace: write a unique sibling temp, then rename it over dest. Only
// that temp is ever removed on failure; the pre-existing destination is never
// deleted and nothing outside `dest`'s own directory is touched. A symlinked
// destination is refused rather than replaced. The temp name is derived from
// dest's directory + filename, so it cannot escape the supplied directory.
inline bool atomic_write(const std::filesystem::path& dest, const std::string& content) {
    if (is_symlink_at(dest))
        return false; // never write through a symlink out of the directory

    std::filesystem::path tmp = dest;
    tmp += ".tmp" + unique_suffix();

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << content;
        out.flush();
        if (!out) {
            out.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }

    // std::filesystem::rename's error_code overload atomically replaces an
    // existing destination where the platform supports it. There is no
    // remove-and-retry fallback: on failure the previous state is kept intact
    // and only our own temp file is cleaned up.
    std::error_code ec;
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        std::error_code rmec;
        std::filesystem::remove(tmp, rmec); // our temp only
        return false;
    }
    return true;
}

struct ParsedRequest {
    std::string session;
    std::string nonce;
    DesiredAction action;
};

// Exactly five keys, each exactly once, no unknowns; a line without '=' or an
// empty key/value is malformed. Values are validated against the action.
inline bool parse_request(const std::string& body, ParsedRequest& out) {
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < body.size();) {
        const std::size_t nl = body.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(body.substr(start));
            break;
        }
        lines.push_back(body.substr(start, nl - start));
        start = nl + 1;
    }

    static const char* const kNames[5] = {"version", "session", "request", "action", "value"};
    std::string values[5];
    bool seen[5] = {false, false, false, false, false};

    for (std::string line : lines) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return false; // line without '='

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            return false; // line without '='

        const std::string key   = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key.empty() || value.empty())
            return false; // empty key/value

        int idx = -1;
        for (int i = 0; i < 5; ++i) {
            if (key == kNames[i]) {
                idx = i;
                break;
            }
        }
        if (idx < 0)
            return false;        // unknown key
        if (seen[idx])
            return false;        // duplicate key
        seen[idx]  = true;
        values[idx] = value;
    }

    for (int i = 0; i < 5; ++i) {
        if (!seen[i])
            return false;        // missing required key
    }

    if (values[0] != "1")
        return false;            // unsupported protocol version

    // Session and request ids are opaque 16..64 char lowercase hex tokens; any
    // other grammar (empty, non-hex, wrong length, uppercase) is rejected.
    if (!is_lower_hex(values[1], kMinTokenLen, kMaxTokenLen))
        return false;
    if (!is_lower_hex(values[2], kMinTokenLen, kMaxTokenLen))
        return false;

    const std::string& action = values[3];
    const std::string& value  = values[4];
    if (action == "menu" || action == "fps") {
        if (value != "0" && value != "1")
            return false;
        out.action.target  = (action == "menu") ? Target::Menu : Target::Fps;
        out.action.enabled = (value == "1");
    } else if (action == "language") {
        if (value != "en" && value != "zh-CN")
            return false;
        out.action.target   = Target::Language;
        out.action.enabled  = false;
        out.action.language = value;
    } else {
        return false;            // unknown action
    }

    out.session = values[1];
    out.nonce   = values[2];
    return true;
}

inline bool satisfied(const DesiredAction& a, const UiState& ui) {
    switch (a.target) {
        case Target::Menu:     return ui.menuVisible == a.enabled;
        case Target::Fps:      return ui.fpsVisible == a.enabled;
        case Target::Language: return ui.language == a.language;
    }
    return false;
}

} // namespace detail

class Bridge {
public:
    // Creates `directory` if needed. `session` must be non-empty. A directory
    // that is itself a symbolic link is refused: the bridge then stays inert.
    explicit Bridge(std::filesystem::path directory, std::string session)
        : directory_(std::move(directory)), session_(std::move(session)) {
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec); // best effort
        requestPath_ = directory_ / kRequestFile;
        statePath_   = directory_ / kStateFile;
        usable_      = detail::safe_directory(directory_);
    }

    // Call once per frame. `elapsedMs` is milliseconds since the previous call,
    // `actual` is what the caller has really applied. Returns a desired action
    // to apply, or nullopt when there is nothing new.
    std::optional<DesiredAction> poll(int elapsedMs, const UiState& actual) {
        if (!usable_)
            return std::nullopt; // symlinked / unusable directory: stay inert

        if (elapsedMs < 0)
            elapsedMs = 0;
        accumulateMs_ += elapsedMs;
        sincePublishMs_ += elapsedMs;

        bool acceptedChanged = false;
        bool justAccepted    = false;
        std::optional<DesiredAction> result;

        if (accumulateMs_ >= kPollIntervalMs) {
            accumulateMs_ = 0;

            std::string body;
            if (detail::read_bounded(requestPath_, kMaxRequestBytes, body)) {
                detail::ParsedRequest req;
                if (detail::parse_request(body, req) && req.session == session_ &&
                    req.nonce != lastSeenNonce_) {
                    // A new, valid, session-matching request: hand it out once.
                    lastSeenNonce_  = req.nonce;
                    acceptedNonce_  = req.nonce;
                    acceptedAction_ = req.action;
                    hasAccepted_    = true;
                    acceptedChanged = true;
                    justAccepted    = true;
                    result          = req.action;
                }
            }
        }

        // A brand-new command is never acknowledged on the poll that hands it
        // out, even when the incoming actual state already equals the request:
        // readiness is only ever observed on a later poll.
        const bool actuallyReady =
            hasAccepted_ && !justAccepted && detail::satisfied(acceptedAction_, actual);

        // Publish whenever anything displayed changes, on any poll — not just
        // on heartbeat or readiness transitions.
        const bool changed = !published_ || acceptedChanged ||
                             actual.menuVisible != publishedMenu_ ||
                             actual.fpsVisible != publishedFps_ ||
                             actual.language != publishedLanguage_ ||
                             actuallyReady != publishedReady_;
        const bool needPublish = changed || (sincePublishMs_ >= kHeartbeatMs);

        if (needPublish) {
            const std::string text = build_state(actual, actuallyReady);
            if (detail::atomic_write(statePath_, text)) {
                published_         = true;
                publishedReady_    = actuallyReady;
                publishedMenu_     = actual.menuVisible;
                publishedFps_      = actual.fpsVisible;
                publishedLanguage_ = actual.language;
                sincePublishMs_    = 0;
            }
            // On failure the mailbox simply stays unavailable; the next poll
            // retries and nothing outside the directory is touched.
        }

        return result;
    }

    const std::string& session() const { return session_; }
    bool ready() const { return publishedReady_; } // last published state had ready=1

    static constexpr int poll_interval_ms() { return kPollIntervalMs; }
    static constexpr int heartbeat_ms() { return kHeartbeatMs; }
    static constexpr std::size_t max_request_bytes() { return kMaxRequestBytes; }

private:
    std::string build_state(const UiState& actual, bool ready) const {
        std::string text;
        text += "version=";
        text += std::to_string(kProtocolVersion);
        text += "\n";
        text += "session=";
        text += session_;
        text += "\n";
        text += "request=";
        text += hasAccepted_ ? acceptedNonce_ : std::string();
        text += "\n";
        text += "menu=";
        text += actual.menuVisible ? "1" : "0";
        text += "\n";
        text += "fps=";
        text += actual.fpsVisible ? "1" : "0";
        text += "\n";
        text += "language=";
        text += actual.language;
        text += "\n";
        text += "ready=";
        text += ready ? "1" : "0";
        text += "\n";
        return text;
    }

    std::filesystem::path directory_;
    std::filesystem::path requestPath_;
    std::filesystem::path statePath_;
    std::string           session_;

    int accumulateMs_   = 0; // no read until kPollIntervalMs of elapsed time
    int sincePublishMs_ = 0;

    std::string   lastSeenNonce_;   // last nonce handed out (never replayed)
    std::string   acceptedNonce_;   // most recently accepted request nonce
    DesiredAction acceptedAction_;
    bool          hasAccepted_ = false;

    bool published_         = false;
    bool publishedReady_    = false;
    bool publishedMenu_     = false;
    bool publishedFps_      = false;
    std::string publishedLanguage_ = "en";

    bool usable_ = true; // false when the directory is a symlink / unusable
};

} // namespace OptiControl
