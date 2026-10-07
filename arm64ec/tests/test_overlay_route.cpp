// Regression tests for the ARM64EC DXVK overlay route selector.
//
// These tests exercise the planned production header
// `OptiScaler/menu/overlay_route.h` (namespace `OptiOverlayRoute`, class
// `Dx11Fallback`). That header is added by the route change; until it exists
// this file fails to compile with one clear #error naming the missing path,
// which is the intended failure.
//
// Why the route exists: on ARM64EC the DX11 overlay is the only path that can
// draw under Wine + DXVK, but only while the Vulkan overlay path has not taken
// over the present chain. Choosing it wrongly either draws nothing (the overlay
// silently disappears) or makes a second component believe it owns the
// renderer. The selector is therefore a pure predicate over the runtime facts
// and owns nothing but its own boolean.
//
// Contract under test:
//   * a default-constructed route is not selected;
//   * TrySelect selects only when ALL SIX positive predicates hold (arm64ec,
//     wine, dxvk, dx11, willPresent, overlayEnabled) and NO Vulkan hooks are
//     installed;
//   * every false predicate rejects on its own, and a rejection leaves no
//     residue: a later fully valid probe still selects;
//   * present probes (willPresent == false) never select;
//   * installed Vulkan hooks reject the DX11 fallback;
//   * the guards are actually evaluated even after the route is selected, so a
//     later failing probe reports `false` -- but it must NOT revoke ownership:
//     selection is sticky until Reset(), so late device probes cannot steal the
//     renderer from the route that already won;
//   * Reset() clears the selection, and is harmless on a fresh route.
//
// The class performs no locking, no device access and no installation; callers
// serialize deliberate selection/installation under the already existing hook
// mutex.
//
// This suite is standalone and portable: standard library only, C++20, no
// Windows headers, no Vulkan, no ImGui, no threads.
//
// The English/Chinese menu, overlay-control and routing behaviour verified
// elsewhere (test_menu_i18n.cpp, test_overlay_control.cpp) is unaffected here.
//
// Build (any modern compiler):
//   c++ -std=c++20 -O2 -Wall -Wextra -o test_overlay_route test_overlay_route.cpp
// Run: ./test_overlay_route   (exit code 0 = all checks passed)

#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<menu/overlay_route.h>)
#include <menu/overlay_route.h>
#elif __has_include("../../OptiScaler/menu/overlay_route.h")
#include "../../OptiScaler/menu/overlay_route.h"
#elif __has_include("../OptiScaler/menu/overlay_route.h")
#include "../OptiScaler/menu/overlay_route.h"
#elif __has_include("overlay_route.h")
#include "overlay_route.h"
#else
#error "planned header OptiScaler/menu/overlay_route.h is missing; add it (namespace OptiOverlayRoute, class Dx11Fallback) so this behavioural suite can build"
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

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(cond))                                                                               \
            report_failure(__FILE__, __LINE__, #cond);                                             \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                       \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(cond))                                                                               \
            report_failure(__FILE__, __LINE__, (msg));                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        ++g_checks;                                                                                \
        auto _a = (a);                                                                             \
        auto _b = (b);                                                                             \
        if (!(_a == _b)) {                                                                         \
            std::ostringstream _os;                                                                \
            _os << #a " == " #b " (" << _a << " vs " << _b << ")";                                 \
            report_failure(__FILE__, __LINE__, _os.str());                                         \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// API shape: pins the documented signature so a drifting header fails here
// ---------------------------------------------------------------------------

using Route = OptiOverlayRoute::Dx11Fallback;

static_assert(std::is_default_constructible_v<Route>,
              "OptiOverlayRoute::Dx11Fallback must be default constructible (initial state: "
              "not selected)");

static_assert(
    std::is_same_v<decltype(std::declval<Route&>().TrySelect(true, true, true, true, true, true,
                                                              false)),
                   bool>,
    "OptiOverlayRoute::Dx11Fallback::TrySelect must take seven bools "
    "(arm64ec, wine, dxvk, dx11, willPresent, overlayEnabled, vulkanHooksInstalled) and return bool");

static_assert(std::is_same_v<decltype(std::declval<const Route&>().IsSelected()), bool>,
              "OptiOverlayRoute::Dx11Fallback::IsSelected must be a const bool member function");

static_assert(std::is_same_v<decltype(std::declval<Route&>().Reset()), void>,
              "OptiOverlayRoute::Dx11Fallback::Reset must be a void member function");

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// The runtime facts the route is selected from. The defaults describe the exact
// situation the DX11 fallback exists for: ARM64EC, under Wine, DXVK presenting
// through the DX11 fallback, with a live present probe and the overlay enabled,
// and with no Vulkan overlay hooks installed.
struct Probe {
    bool arm64ec = true;
    bool wine = true;
    bool dxvk = true;
    bool dx11 = true;
    bool willPresent = true;
    bool overlayEnabled = true;
    bool vulkanHooksInstalled = false;
};

bool attempt(Route& route, const Probe& p) {
    return route.TrySelect(p.arm64ec, p.wine, p.dxvk, p.dx11, p.willPresent, p.overlayEnabled,
                           p.vulkanHooksInstalled);
}

// The contract, stated independently of the implementation: all six positive
// predicates must hold and no Vulkan hooks may be installed.
bool should_select(const Probe& p) {
    return p.arm64ec && p.wine && p.dxvk && p.dx11 && p.willPresent && p.overlayEnabled &&
           !p.vulkanHooksInstalled;
}

std::string describe(const Probe& p) {
    std::ostringstream os;
    os << "probe{arm64ec=" << p.arm64ec << " wine=" << p.wine << " dxvk=" << p.dxvk
       << " dx11=" << p.dx11 << " willPresent=" << p.willPresent
       << " overlayEnabled=" << p.overlayEnabled
       << " vulkanHooksInstalled=" << p.vulkanHooksInstalled << "}";
    return os.str();
}

// A probe built from the seven bits of `bits`, so the whole 128-entry truth
// table can be walked.
Probe probe_from_bits(int bits) {
    Probe p;
    p.arm64ec = (bits & 1) != 0;
    p.wine = (bits & 2) != 0;
    p.dxvk = (bits & 4) != 0;
    p.dx11 = (bits & 8) != 0;
    p.willPresent = (bits & 16) != 0;
    p.overlayEnabled = (bits & 32) != 0;
    p.vulkanHooksInstalled = (bits & 64) != 0;
    return p;
}

struct FailingPredicate {
    const char* name;
    void (*flip)(Probe&);
};

// Every single-predicate failure the route must reject, each named after the
// runtime fact it stands for.
std::vector<FailingPredicate> single_failures() {
    return {
        {"not running as ARM64EC",
         [](Probe& p) { p.arm64ec = false; }},
        {"not running under Wine",
         [](Probe& p) { p.wine = false; }},
        {"game renderer is not DXVK",
         [](Probe& p) { p.dxvk = false; }},
        {"DXVK is not presenting through the DX11 fallback",
         [](Probe& p) { p.dx11 = false; }},
        {"present probe says the DX11 presenter will not present",
         [](Probe& p) { p.willPresent = false; }},
        {"overlay disabled by configuration",
         [](Probe& p) { p.overlayEnabled = false; }},
        {"Vulkan overlay hooks already installed",
         [](Probe& p) { p.vulkanHooksInstalled = true; }},
    };
}

// The six positive predicates: failing any of them after a successful selection
// must report `false` while the selection itself survives.
std::vector<FailingPredicate> positive_predicate_failures() {
    std::vector<FailingPredicate> out = single_failures();
    out.pop_back(); // drop the vulkanHooksInstalled entry
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

static void test_fresh_route_is_not_selected() {
    Route route;
    CHECK(!route.IsSelected());

    // Reset on an untouched route is a harmless no-op, not a state change.
    route.Reset();
    CHECK(!route.IsSelected());
}

static void test_rejects_every_single_predicate_failure() {
    for (const auto& failure : single_failures()) {
        Probe p;
        failure.flip(p);
        const std::string what = std::string(failure.name) + " -> " + describe(p);

        Route route;
        CHECK_MSG(attempt(route, p) == false, what + ": TrySelect must return false");
        CHECK_MSG(route.IsSelected() == false, what + ": route must stay unselected");

        // A rejection leaves no residue: the very next fully valid probe wins.
        CHECK_MSG(attempt(route, Probe{}) == true, what + ": a valid probe after the rejection must select");
        CHECK_MSG(route.IsSelected() == true, what + ": route must be selected after the valid probe");
    }
}

static void test_selection_requires_the_full_truth_table() {
    // Exhaustive: 2^7 combinations of the six positive predicates plus the
    // installed-Vulkan-hooks guard. A fresh route per combination, so no
    // combination can inherit state from another.
    for (int bits = 0; bits < 128; ++bits) {
        const Probe p = probe_from_bits(bits);
        const bool want = should_select(p);
        const std::string what = describe(p);

        Route route;
        const bool selected = attempt(route, p);
        CHECK_MSG(selected == want,
                  what + ": TrySelect returned " + (selected ? "true" : "false") +
                      " but the contract expects " + (want ? "true" : "false"));
        CHECK_MSG(route.IsSelected() == want,
                  what + ": IsSelected() is " + (route.IsSelected() ? "true" : "false") +
                      " after TrySelect returned " + (selected ? "true" : "false"));

        // Rejections must not poison the route for a later valid probe.
        if (!want) {
            Route retry;
            attempt(retry, p);
            CHECK_MSG(attempt(retry, Probe{}) == true,
                      what + ": a later valid probe must still select after this rejection");
            CHECK_MSG(retry.IsSelected() == true,
                      what + ": route selected by the later valid probe must report selected");
        }
    }
}

static void test_present_probe_never_selects() {
    // The present probe is a test, not a decision: when it reports that the DX11
    // presenter will not present, the route must never be claimed, however many
    // times the probe runs.
    Route route;
    Probe p;
    p.willPresent = false;

    for (int i = 0; i < 4; ++i) {
        CHECK_MSG(attempt(route, p) == false,
                  "present probe (willPresent=false) must never select the DX11 fallback");
        CHECK_MSG(route.IsSelected() == false,
                  "present probe (willPresent=false) must leave the route unselected");
    }

    // Only a probe that actually sees a present selects.
    p.willPresent = true;
    CHECK(attempt(route, p) == true);
    CHECK(route.IsSelected());
}

static void test_installed_vulkan_hooks_reject_the_fallback() {
    // The Vulkan overlay path already owns the present chain: the DX11 fallback
    // must stand down even though every other predicate holds.
    Probe p;
    p.vulkanHooksInstalled = true;

    Route route;
    CHECK(!attempt(route, p));
    CHECK(!route.IsSelected());

    // Repeated probes stay rejected.
    for (int i = 0; i < 3; ++i) {
        CHECK(!attempt(route, p));
        CHECK(!route.IsSelected());
    }
}

static void test_repeated_valid_probes_stay_selected() {
    Route route;
    const Probe valid;

    for (int i = 0; i < 5; ++i) {
        CHECK_MSG(attempt(route, valid) == true,
                  "a valid probe must keep reporting true on every call");
        CHECK_MSG(route.IsSelected() == true,
                  "a valid probe must keep the route selected on every call");
    }
}

static void test_selection_is_sticky_until_reset() {
    Route route;
    CHECK(attempt(route, Probe{}));
    CHECK(route.IsSelected());

    // Guards are still evaluated once selected: a later probe that fails a
    // platform / game / present / overlay predicate reports false ...
    for (const auto& failure : positive_predicate_failures()) {
        Probe p;
        failure.flip(p);
        const std::string what = std::string(failure.name) + " after selection: " + describe(p);

        CHECK_MSG(attempt(route, p) == false, what + ": TrySelect must return false");
        // ... but it must not steal the renderer: ownership survives.
        CHECK_MSG(route.IsSelected() == true,
                  what + ": the previously selected route must retain ownership");
    }

    // A later valid probe still re-confirms the same route.
    CHECK(attempt(route, Probe{}) == true);
    CHECK(route.IsSelected());

    // Only Reset() relinquishes it.
    route.Reset();
    CHECK_MSG(route.IsSelected() == false, "Reset() must clear the selection");

    // After Reset a failing probe cannot select, and a valid probe can again.
    Probe bad;
    bad.dxvk = false;
    CHECK(!attempt(route, bad));
    CHECK(!route.IsSelected());
    CHECK(attempt(route, Probe{}));
    CHECK(route.IsSelected());

    // Reset is idempotent.
    route.Reset();
    route.Reset();
    CHECK(!route.IsSelected());
}

// ---------------------------------------------------------------------------

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        {"a fresh route is not selected and Reset is a no-op", test_fresh_route_is_not_selected},
        {"each false predicate rejects on its own without residue",
         test_rejects_every_single_predicate_failure},
        {"selection matches the full truth table of the seven guards",
         test_selection_requires_the_full_truth_table},
        {"present probes never select", test_present_probe_never_selects},
        {"installed Vulkan hooks reject the DX11 fallback",
         test_installed_vulkan_hooks_reject_the_fallback},
        {"repeated valid probes stay selected", test_repeated_valid_probes_stay_selected},
        {"selection is sticky until Reset even when later guards fail",
         test_selection_is_sticky_until_reset},
    };

    std::printf("OptiOverlayRoute::Dx11Fallback route tests (%zu cases)\n",
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

    // A suite that ran no checks at all is a failure, not a pass.
    if (g_checks == 0)
        report_failure(__FILE__, __LINE__, "no checks were executed");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    std::printf("RESULT: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
