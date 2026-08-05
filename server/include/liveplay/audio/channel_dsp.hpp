// ============================================================================
// liveplay/audio/channel_dsp.hpp
// ----------------------------------------------------------------------------
// The channel strip's fixed processing chain, in console order:
//
//     HPF -> LPF -> EQ (4 bands) -> gate -> compressor
//
// Fixed, not a plugin rack. These five are known blocks that every strip has,
// exactly as a console channel does, so they are laid out as fields rather
// than dispatched through an insert interface. User-orderable plugins arrive
// later as a separate list alongside this — building a virtual-dispatch
// framework for five blocks that are always present and always in this order
// would be scaffolding around a thing that does not move.
//
// Where it runs
// -------------
// On the bus accumulators, after the items have summed into them and BEFORE
// the fader, the PFL tap and the mute. So:
//
//     items -> [this] -> PFL tap -> fader/mute -> pan send -> master
//
// It therefore runs even while a bus is muted. That is required, because PFL
// is pre-mute and post-processing, and it is also what stops the compressor's
// envelope from lurching when a bus is unmuted mid-show.
//
// Threading
// ---------
// Parameters are set on the CONTROL thread, which turns them into filter
// coefficients (std::sin and std::cos have no business in an audio callback)
// and publishes them into one of two slots, flipping an atomic index. The
// render thread reads that index once per block. No locks, no allocation, and
// a reader can never see half of a coefficient set.
//
// Coefficients are then ramped toward the published target over a few blocks
// rather than snapped to it. Jumping straight to new coefficients is the
// "audio pops when a user drags a slider" problem the notes in _DSP_DOCS warn
// about; ramping per block (not per sample) costs five lerps per section and
// removes it.
// ============================================================================
#pragma once

#include "liveplay/audio/biquad.hpp"
#include "liveplay/audio/types.hpp"

#include <array>
#include <atomic>
#include <cstddef>

namespace liveplay::audio {

// How many EQ bands a strip has. Matches the four the channel view lays out
// (LF / LMF / HMF / HF).
inline constexpr std::size_t kEqBands = 4;

// Every filter section on one strip, in chain order. One set of coefficients
// is shared by both lanes; the per-lane STATE is what must stay separate.
struct StripCoeffs {
    BiquadCoeffs hpf;
    BiquadCoeffs lpf;
    std::array<BiquadCoeffs, kEqBands> eq;
};

// What the operator set. Plain values, owned by the control thread.
struct FilterParams {
    bool  enabled = false;
    float freq_hz = 80.0f;
    float q       = 0.70710678f;
};

struct EqBandParams {
    bool  enabled = false;
    float freq_hz = 1000.0f;
    float gain_db = 0.0f;
    float q       = 1.0f;
};

struct StripDspParams {
    FilterParams hpf{false, 80.0f,    0.70710678f};
    FilterParams lpf{false, 18000.0f, 0.70710678f};
    std::array<EqBandParams, kEqBands> eq{{
        {false, 100.0f,   0.0f, 0.7f},
        {false, 500.0f,   0.0f, 1.0f},
        {false, 2500.0f,  0.0f, 1.0f},
        {false, 10000.0f, 0.0f, 0.7f},
    }};
};

class ChannelDsp {
public:
    void configure(SampleRate sample_rate) noexcept {
        sample_rate_ = sample_rate;
        // Publish a flat chain so the very first block has something coherent
        // to read even if no parameters are ever set.
        set_params(StripDspParams{});
        // ...and land on it immediately rather than ramping up from nothing.
        active_ = slot(published_.load(std::memory_order_acquire));
        for (auto& lane : state_) lane.reset();
    }

    // ---- Control thread ---------------------------------------------------
    // Turn parameters into coefficients and publish them. A disabled block
    // becomes a passthrough section rather than being branched around, so the
    // render loop runs the same straight line either way.
    void set_params(const StripDspParams& p) {
        const double fs = static_cast<double>(sample_rate_);
        StripCoeffs c;
        c.hpf = p.hpf.enabled ? biquad_highpass(p.hpf.freq_hz, fs, p.hpf.q)
                              : biquad_passthrough();
        c.lpf = p.lpf.enabled ? biquad_lowpass(p.lpf.freq_hz, fs, p.lpf.q)
                              : biquad_passthrough();
        for (std::size_t i = 0; i < kEqBands; ++i) {
            const auto& b = p.eq[i];
            // A band sitting at 0 dB is a no-op; make it literally one so four
            // flat bands in circuit cannot colour the desk.
            c.eq[i] = (b.enabled && b.gain_db != 0.0f)
                          ? biquad_peaking(b.freq_hz, fs, b.gain_db, b.q)
                          : biquad_passthrough();
        }
        publish(c);
        any_active_.store(needs_processing(p), std::memory_order_release);
    }

    // Whether anything in the chain is doing something. Lets the render loop
    // skip a strip whose tone controls are all flat, which is most of them.
    bool active() const noexcept { return any_active_.load(std::memory_order_acquire); }

    // ---- Render thread ----------------------------------------------------
    // Advance the coefficient ramp by one block. Call once per block, before
    // process_block, so both lanes are filtered by the same coefficients.
    void advance_coeffs() noexcept {
        const StripCoeffs& target = slot(published_.load(std::memory_order_acquire));
        // ~20 ms to travel, at any block size, so a knob drag glides instead
        // of stepping. Small enough that nobody hears lag on it.
        constexpr float kRamp = 0.25f;
        lerp(active_.hpf, target.hpf, kRamp);
        lerp(active_.lpf, target.lpf, kRamp);
        for (std::size_t i = 0; i < kEqBands; ++i) lerp(active_.eq[i], target.eq[i], kRamp);
    }

    // Filter one lane's block in place. `lane` selects which lane's filter
    // memory to use — sharing history across lanes would smear the image.
    void process_block(ChannelIndex lane, Sample* buf, std::size_t frames) noexcept {
        if (lane >= kMixerLanes) return;
        auto& st = state_[lane];
        for (std::size_t s = 0; s < frames; ++s) {
            float x = buf[s];
            x = st.hpf.process(active_.hpf, x);
            x = st.lpf.process(active_.lpf, x);
            for (std::size_t b = 0; b < kEqBands; ++b) x = st.eq[b].process(active_.eq[b], x);
            buf[s] = x;
        }
    }

    // Drop every section's memory. For when a strip's signal source changes
    // underneath it and the old tail is no longer meaningful.
    void reset() noexcept { for (auto& lane : state_) lane.reset(); }

private:
    struct LaneState {
        BiquadState hpf;
        BiquadState lpf;
        std::array<BiquadState, kEqBands> eq;
        void reset() noexcept {
            hpf.reset();
            lpf.reset();
            for (auto& b : eq) b.reset();
        }
    };

    static bool needs_processing(const StripDspParams& p) noexcept {
        if (p.hpf.enabled || p.lpf.enabled) return true;
        for (const auto& b : p.eq) if (b.enabled && b.gain_db != 0.0f) return true;
        return false;
    }

    static void lerp(BiquadCoeffs& cur, const BiquadCoeffs& to, float t) noexcept {
        cur.b0 += (to.b0 - cur.b0) * t;
        cur.b1 += (to.b1 - cur.b1) * t;
        cur.b2 += (to.b2 - cur.b2) * t;
        cur.a1 += (to.a1 - cur.a1) * t;
        cur.a2 += (to.a2 - cur.a2) * t;
    }

    void publish(const StripCoeffs& c) {
        // Write the slot the render thread is not reading, then flip. The
        // store is release-ordered so the coefficients are visible before the
        // index that points at them.
        const unsigned next = 1u - published_.load(std::memory_order_relaxed);
        slots_[next] = c;
        published_.store(next, std::memory_order_release);
    }
    const StripCoeffs& slot(unsigned i) const noexcept { return slots_[i & 1u]; }

    SampleRate  sample_rate_ = kDefaultMixSampleRate;
    StripCoeffs slots_[2]{};
    std::atomic<unsigned> published_{0};
    std::atomic<bool>     any_active_{false};

    StripCoeffs                       active_{};   // render thread only
    std::array<LaneState, kMixerLanes> state_{};   // render thread only
};

} // namespace liveplay::audio
