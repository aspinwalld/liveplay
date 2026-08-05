// ============================================================================
// biquad_test.cpp — the filter sections behind the HPF, the LPF and every EQ
// band. No framework: each check prints PASS/FAIL and the binary exits
// non-zero if anything failed.
//
//     cmake -DLIVEPLAY_BUILD_TESTS=ON .. && cmake --build . --target liveplay-biquad-tests
//     ./liveplay-biquad-tests
//
// The load-bearing test here is `test_measured_matches_analytic`: it pushes
// real sine waves through the real process() loop and compares the measured
// steady-state level against the analytic |H(e^jw)|. That is what catches a
// coefficient typo or a transposed-form slip, which a test that only checks
// the coefficient arithmetic against itself would sail straight past.
// ============================================================================
#include "liveplay/audio/biquad.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace liveplay::audio;

namespace {

int g_failures = 0;

void check_near(const char* name, double got, double expect, double tol) {
    const bool ok = std::fabs(got - expect) <= tol;
    std::printf("%-58s %s  (got %+9.4f, expect %+9.4f ±%.4f)\n",
                name, ok ? "PASS" : "FAIL", got, expect, tol);
    if (!ok) ++g_failures;
}
void check_true(const char* name, bool ok) {
    std::printf("%-58s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

constexpr double kFs = 48000.0;

// Drive a steady sine through the section and return its output level in dB.
// Half the run is discarded so the reading is steady state, not the section's
// start-up transient.
double measured_response_db(const BiquadCoeffs& c, double freq_hz) {
    BiquadState st;
    const int settle = 24000;
    const int measure = 24000;
    double peak = 0.0;
    for (int n = 0; n < settle + measure; ++n) {
        const float x = static_cast<float>(std::sin(2.0 * kPi * freq_hz * n / kFs));
        const float y = st.process(c, x);
        if (n >= settle) peak = std::max(peak, std::fabs(static_cast<double>(y)));
    }
    return peak <= 0.0 ? -200.0 : 20.0 * std::log10(peak);
}

// ---------------------------------------------------------------------------
void test_highpass() {
    const auto c = biquad_highpass(1000.0, kFs);
    // A Butterworth (Q = 1/sqrt2) corner sits at -3.01 dB, by definition.
    check_near("hpf: -3 dB at the corner",
               biquad_magnitude_db(c, 1000.0, kFs), -3.0103, 0.05);
    check_near("hpf: passes the passband at unity",
               biquad_magnitude_db(c, 12000.0, kFs), 0.0, 0.15);
    // Second order = 12 dB per octave. An octave down should be ~12 dB lower
    // than two octaves down is... measured as the slope between them.
    const double oneOct  = biquad_magnitude_db(c, 500.0, kFs);
    const double twoOct  = biquad_magnitude_db(c, 250.0, kFs);
    check_near("hpf: rolls off 12 dB/octave", oneOct - twoOct, 12.0, 0.7);
    check_true ("hpf: deep stopband is well down", biquad_magnitude_db(c, 20.0, kFs) < -60.0);
}

void test_lowpass() {
    const auto c = biquad_lowpass(1000.0, kFs);
    check_near("lpf: -3 dB at the corner",
               biquad_magnitude_db(c, 1000.0, kFs), -3.0103, 0.05);
    check_near("lpf: passes the passband at unity",
               biquad_magnitude_db(c, 50.0, kFs), 0.0, 0.15);
    const double oneOct = biquad_magnitude_db(c, 2000.0, kFs);
    const double twoOct = biquad_magnitude_db(c, 4000.0, kFs);
    check_near("lpf: rolls off 12 dB/octave", oneOct - twoOct, 12.0, 0.7);
}

void test_peaking() {
    // A bell must deliver exactly the gain asked for, at the frequency asked
    // for — this is the one an operator checks by ear against the number.
    for (float g : {-12.0f, -6.0f, 6.0f, 12.0f}) {
        const auto c = biquad_peaking(1000.0, kFs, g, 1.0f);
        char label[80];
        std::snprintf(label, sizeof label, "eq: bell hits %+.0f dB at centre", g);
        check_near(label, biquad_magnitude_db(c, 1000.0, kFs), g, 0.02);
    }
    // ...and must not disturb anything far from it.
    const auto c = biquad_peaking(1000.0, kFs, 12.0f, 2.0f);
    check_near("eq: bell is unity two decades below",
               biquad_magnitude_db(c, 10.0, kFs), 0.0, 0.1);
    check_near("eq: bell is unity well above",
               biquad_magnitude_db(c, 20000.0, kFs), 0.0, 0.3);
    // Q controls width: a higher Q must be narrower, i.e. closer to unity at a
    // fixed offset from centre.
    const auto wide   = biquad_peaking(1000.0, kFs, 12.0f, 0.5f);
    const auto narrow = biquad_peaking(1000.0, kFs, 12.0f, 4.0f);
    check_true("eq: higher Q is narrower",
               biquad_magnitude_db(narrow, 2000.0, kFs) <
               biquad_magnitude_db(wide,   2000.0, kFs));
    // A band set to 0 dB must be a genuine no-op, not "nearly" one — an EQ
    // with four flat bands in circuit has to be bit-transparent or every strip
    // quietly colours the desk.
    const auto flat = biquad_peaking(1000.0, kFs, 0.0f, 1.0f);
    check_near("eq: 0 dB band is flat at centre",
               biquad_magnitude_db(flat, 1000.0, kFs), 0.0, 1e-9);
    check_near("eq: 0 dB band is flat elsewhere",
               biquad_magnitude_db(flat, 60.0, kFs), 0.0, 1e-9);
}

void test_shelves() {
    const auto ls = biquad_lowshelf(200.0, kFs, 9.0f);
    check_near("shelf: low shelf reaches its gain at DC",
               biquad_magnitude_db(ls, 1.0, kFs), 9.0, 0.15);
    check_near("shelf: low shelf is unity up top",
               biquad_magnitude_db(ls, 18000.0, kFs), 0.0, 0.2);
    const auto hs = biquad_highshelf(4000.0, kFs, -9.0f);
    check_near("shelf: high shelf reaches its gain up top",
               biquad_magnitude_db(hs, 22000.0, kFs), -9.0, 0.2);
    check_near("shelf: high shelf is unity at DC",
               biquad_magnitude_db(hs, 1.0, kFs), 0.0, 0.15);
}

// ---------------------------------------------------------------------------
// The real check: does the running filter do what the maths says it does?
void test_measured_matches_analytic() {
    struct Case { const char* name; BiquadCoeffs c; double at; };
    const Case cases[] = {
        {"hpf 100 Hz measured at 100 Hz",  biquad_highpass(100.0, kFs),            100.0},
        {"hpf 100 Hz measured at 1 kHz",   biquad_highpass(100.0, kFs),           1000.0},
        {"hpf 100 Hz measured at 40 Hz",   biquad_highpass(100.0, kFs),             40.0},
        {"lpf 8 kHz measured at 8 kHz",    biquad_lowpass(8000.0, kFs),           8000.0},
        {"lpf 8 kHz measured at 16 kHz",   biquad_lowpass(8000.0, kFs),          16000.0},
        {"bell +6 dB measured at centre",  biquad_peaking(1000.0, kFs, 6.0f, 1.0f), 1000.0},
        {"bell -9 dB measured at centre",  biquad_peaking(3000.0, kFs, -9.0f, 2.0f), 3000.0},
        {"low shelf +6 measured at 50 Hz", biquad_lowshelf(300.0, kFs, 6.0f),       50.0},
    };
    for (const auto& k : cases) {
        const double analytic = biquad_magnitude_db(k.c, k.at, kFs);
        const double measured = measured_response_db(k.c, k.at);
        check_near(k.name, measured, analytic, 0.06);
    }
}

// ---------------------------------------------------------------------------
void test_denormal_flush() {
    // Ring the section with an impulse, then feed it pure silence. Without the
    // flush the state decays towards denormal range and stays there, and on
    // x86 every subsequent sample costs a microcode trap — a filter that was
    // free suddenly is not, on every idle bus at once.
    auto c = biquad_lowpass(80.0, kFs);
    BiquadState st;
    st.process(c, 1.0f);
    float last = 1.0f;
    for (int n = 0; n < 500000 && last != 0.0f; ++n) last = st.process(c, 0.0f);
    check_true("denormal: state reaches exactly zero on silence", last == 0.0f);

    // And the flush must not eat real signal on the way past.
    BiquadState st2;
    auto flat = biquad_peaking(1000.0, kFs, 0.0f, 1.0f);
    double worst = 0.0;
    for (int n = 0; n < 4800; ++n) {
        const float x = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * n / kFs));
        worst = std::max(worst, std::fabs(static_cast<double>(st2.process(flat, x) - x)));
    }
    check_true("denormal: flush does not disturb real signal", worst < 1e-6);
}

void test_nyquist_clamp() {
    // A 20 kHz knob on a 44.1 kHz project asks for a corner above Nyquist. The
    // cookbook formulas blow up there; the design functions clamp instead, so
    // the worst case is a filter parked as high as it can go rather than NaN
    // poisoning the bus for the rest of the show.
    for (double fs : {44100.0, 48000.0}) {
        for (double f : {19000.0, 22050.0, 24000.0, 96000.0}) {
            const auto lp = biquad_lowpass(f, fs);
            const auto hp = biquad_highpass(f, fs);
            const bool finite =
                std::isfinite(lp.b0) && std::isfinite(lp.a1) && std::isfinite(lp.a2) &&
                std::isfinite(hp.b0) && std::isfinite(hp.a1) && std::isfinite(hp.a2);
            if (!finite) {
                std::printf("nyquist clamp FAILED at fs=%.0f f=%.0f\n", fs, f);
                ++g_failures;
            }
        }
    }
    check_true("nyquist: coefficients stay finite above fs/2", true);

    // Zero and negative frequencies come from a typed-in value or a bad patch.
    const auto z = biquad_highpass(0.0, kFs);
    check_true("nyquist: DC request stays finite", std::isfinite(z.b0) && std::isfinite(z.a1));
}

void test_stability() {
    // A section is stable when its poles are inside the unit circle, which for
    // a normalised biquad is |a2| < 1 and |a1| < 1 + a2. Swept across the whole
    // usable range, because an unstable section does not misbehave quietly —
    // it runs away to full scale on a live output.
    int checked = 0;
    for (double f = 20.0; f <= 20000.0; f *= 1.15) {
        for (float q : {0.1f, 0.5f, 0.707f, 2.0f, 10.0f, 40.0f}) {
            for (float g : {-18.0f, 0.0f, 18.0f}) {
                const BiquadCoeffs set[] = {
                    biquad_highpass(f, kFs, q),
                    biquad_lowpass(f, kFs, q),
                    biquad_peaking(f, kFs, g, q),
                    biquad_lowshelf(f, kFs, g),
                    biquad_highshelf(f, kFs, g),
                };
                for (const auto& c : set) {
                    ++checked;
                    if (!(std::fabs(c.a2) < 1.0f &&
                          std::fabs(c.a1) < 1.0f + c.a2 + 1e-6f)) {
                        std::printf("unstable at f=%.1f q=%.2f g=%.1f "
                                    "(a1=%.6f a2=%.6f)\n", f, q, g, c.a1, c.a2);
                        ++g_failures;
                    }
                }
            }
        }
    }
    std::printf("%-58s %s  (%d sections)\n",
                "stability: poles inside the unit circle everywhere",
                g_failures == 0 ? "PASS" : "FAIL", checked);
}

} // namespace

int main() {
    std::printf("== biquad ==\n");
    test_highpass();
    test_lowpass();
    test_peaking();
    test_shelves();
    test_measured_matches_analytic();
    test_denormal_flush();
    test_nyquist_clamp();
    test_stability();

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
