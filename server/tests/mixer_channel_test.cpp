// ============================================================================
// mixer_channel_test.cpp — standalone assertions for MixerChannel gain/fades.
// ----------------------------------------------------------------------------
// No test framework: each check prints PASS/FAIL and the binary exits non-zero
// if anything failed. Build via the LIVEPLAY_BUILD_TESTS CMake option:
//     cmake -DLIVEPLAY_BUILD_TESTS=ON .. && cmake --build . --target liveplay-mixer-tests
//     ./liveplay-mixer-tests
//
// Covers the peek/advance split (#45): reading the gain must never move the
// fade envelope, so a caller that reads twice in a block (metering + gain
// application) can't run every fade at double speed.
//   1. Static gain            — set_gain_db reads back, repeatedly
//   2. Peek is pure           — N peeks leave the envelope where it was
//   3. Advance drives it      — the ramp completes in exactly the configured
//                               number of blocks, and only then
//   4. begin_fade is pure     — arming a fade from the control thread doesn't
//                               consume a block of the previous one
//   5. Zero-length fade       — applies immediately
// ============================================================================
#include "liveplay/audio/mixer_channel.hpp"

#include <cmath>
#include <cstdio>

using namespace liveplay::audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name, double got, double expect, double tol) {
    std::printf("%-56s %s  (got %+9.5f, expect %+9.5f ±%.5f)\n",
                name, ok ? "PASS" : "FAIL", got, expect, tol);
    if (!ok) ++g_failures;
}
void check_near(const char* name, double got, double expect, double tol) {
    check(std::fabs(got - expect) <= tol, name, got, expect, tol);
}
void check_true(const char* name, bool ok) {
    std::printf("%-56s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

constexpr SampleRate kFs    = 48'000;
constexpr FrameCount kBlock = 256;

// MixerChannel owns per-lane Meters (atomics), so it is neither copyable nor
// movable — construct it in place.
#define MAKE_CHANNEL(name)                                   \
    MixerChannel name{MixerChannelId{"test"}, "Test"};       \
    name.configure(kFs, kBlock)

// ---------------------------------------------------------------------------
void test_static_gain() {
    MAKE_CHANNEL(m);
    check_near("static: unity by default",   m.peek_gain_linear(), 1.0, 1e-6);
    m.set_gain_db(-6.0f);
    const float first = m.peek_gain_linear();
    check_near("static: -6 dB ≈ 0.5012",     first, 0.50119, 1e-4);
    check_near("static: repeat read stable", m.peek_gain_linear(), first, 0.0);
    m.advance_block();
    check_near("static: advance is a no-op without a fade",
                                             m.peek_gain_linear(), first, 0.0);
}

// ---------------------------------------------------------------------------
void test_peek_does_not_advance() {
    MAKE_CHANNEL(m);
    // 100 blocks' worth of fade to silence.
    const auto duration = std::chrono::milliseconds{
        static_cast<long long>(100.0 * kBlock * 1000.0 / kFs)};
    m.begin_fade(-120.0f, duration);

    check_near("peek: starts at unity", m.peek_gain_linear(), 1.0, 1e-6);
    for (int i = 0; i < 50; ++i) m.peek_gain_linear();
    check_near("peek: 50 reads left the envelope alone",
               m.peek_gain_linear(), 1.0, 1e-6);

    // Half the blocks → cosine ramp is at its midpoint (0.5 of the way down).
    for (int i = 0; i < 50; ++i) m.advance_block();
    check_near("peek: half-way through the fade", m.peek_gain_linear(), 0.5, 0.02);
    // Reading it a second time must give the same answer.
    check_near("peek: still half-way on re-read", m.peek_gain_linear(), 0.5, 0.02);
}

// ---------------------------------------------------------------------------
void test_advance_completes_on_schedule() {
    MAKE_CHANNEL(m);
    const int blocks = 40;
    m.begin_fade(-120.0f, std::chrono::milliseconds{
        static_cast<long long>(blocks * kBlock * 1000.0 / kFs)});

    for (int i = 0; i < blocks - 1; ++i) m.advance_block();
    check_true("advance: not finished one block early", m.peek_gain_linear() > 0.0f);
    m.advance_block();
    check_near("advance: silent exactly on schedule", m.peek_gain_linear(), 0.0, 1e-6);
    // Overshooting must not resurrect the fade.
    for (int i = 0; i < 10; ++i) m.advance_block();
    check_near("advance: stays at the target afterwards", m.peek_gain_linear(), 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
void test_begin_fade_does_not_consume_a_block() {
    MAKE_CHANNEL(m);
    const int blocks = 40;
    const auto duration = std::chrono::milliseconds{
        static_cast<long long>(blocks * kBlock * 1000.0 / kFs)};

    // Arm a fade, then immediately re-arm it (as a control-thread caller might).
    // The re-arm snapshots the current gain; it must not step the envelope.
    m.begin_fade(-120.0f, duration);
    m.begin_fade(-120.0f, duration);
    m.begin_fade(-120.0f, duration);
    check_near("begin_fade: re-arming kept the start gain",
               m.peek_gain_linear(), 1.0, 1e-6);

    for (int i = 0; i < blocks; ++i) m.advance_block();
    check_near("begin_fade: full duration still required",
               m.peek_gain_linear(), 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
void test_zero_length_fade() {
    MAKE_CHANNEL(m);
    m.begin_fade(-120.0f, std::chrono::milliseconds{0});
    check_near("zero fade: applied immediately", m.peek_gain_linear(), 0.0, 1e-6);
    m.advance_block();
    check_near("zero fade: stable after advance", m.peek_gain_linear(), 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Constant-power pan law (pan_gains_db). Two properties carry real weight and
// are easy to break silently:
//
//   * Constant power. l^2 + r^2 == 1 everywhere, so a source sweeping across
//     the image holds its perceived level instead of dipping in the middle.
//   * Centre agrees with the downmix constant. A centred mono source and a
//     stereo fold-down must land at the same level, which is the whole reason
//     kDefaultDownmixDb and this law are described as one law in the design.
//     If someone "rounds" one of them, this catches it.
void test_pan_law() {
    const auto lin = [](float db) { return std::pow(10.0, db / 20.0); };

    const auto centre = pan_gains_db(0.0f);
    check_near("pan: centre is symmetric",
               centre.left - centre.right, 0.0, 1e-6);
    check_near("pan: centre matches the downmix constant",
               centre.left, kDefaultDownmixDb, 0.02);

    const auto left = pan_gains_db(-1.0f);
    check_near("pan: hard left is unity on L", left.left, 0.0, 1e-4);
    check_true ("pan: hard left is silent on R", left.right <= kSilentGainDb + 1e-3f);

    const auto right = pan_gains_db(1.0f);
    check_near("pan: hard right is unity on R", right.right, 0.0, 1e-4);
    check_true ("pan: hard right is silent on L", right.left <= kSilentGainDb + 1e-3f);

    // Power stays constant across the sweep, which is what stops the image
    // from dipping as it crosses centre.
    double worst = 0.0;
    for (int i = -10; i <= 10; ++i) {
        const auto g = pan_gains_db(static_cast<float>(i) / 10.0f);
        const double power = lin(g.left) * lin(g.left) + lin(g.right) * lin(g.right);
        worst = std::max(worst, std::fabs(power - 1.0));
    }
    check_near("pan: constant power across the sweep", worst, 0.0, 1e-6);

    // Out-of-range input must clamp, not wrap into the far side of the arc.
    check_near("pan: clamps below -1", pan_gains_db(-4.0f).left, 0.0, 1e-4);
    check_near("pan: clamps above +1", pan_gains_db(4.0f).right, 0.0, 1e-4);
}

} // namespace

int main() {
    std::printf("== mixer channel ==\n");
    test_static_gain();
    test_peek_does_not_advance();
    test_advance_completes_on_schedule();
    test_begin_fade_does_not_consume_a_block();
    test_zero_length_fade();
    test_pan_law();

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
