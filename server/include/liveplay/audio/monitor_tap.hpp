// ============================================================================
// liveplay/audio/monitor_tap.hpp
// ----------------------------------------------------------------------------
// PFL — pre-fade listen — expressed as sends into the Monitor strip.
//
// A PFL'd bus adds one edge into Monitor and changes nothing else: the house
// mix is untouched, several buses can be tapped at once, and the tap is taken
// before the strip's fader and before its mute, because hearing a channel with
// its fader down is the entire diagnostic use. See BUS_ARCHITECTURE.md §2.4.
//
// The tap list is built on the control thread, where a strip's width is known,
// so the render thread only ever adds one buffer into another at a gain.
//
// This lives in its own header, apart from the engine, so the placement rule
// can be exercised without opening an audio device — every other line of the
// render loop needs a device to run at all, and this is the line with a
// decision in it.
// ============================================================================
#pragma once

#include "liveplay/audio/mixer_channel.hpp"
#include "liveplay/audio/types.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace liveplay::audio {

// One PFL'd strip's contribution to the Monitor strip.
//
// Deliberately NOT the general bus→bus edge. Monitor is a single, known
// destination that nothing may feed onward, so it needs no processing order
// and no cycle check; those arrive with bus→bus routing.
struct MonitorTap {
    std::shared_ptr<MixerChannel> source;
    ChannelIndex                  src_lane = 0;
    ChannelIndex                  dst_lane = 0;
    float                         gain     = 1.0f;
};

// Fold every tap into the monitor strip's lane buffers, in place.
//
// `lane_buffers` is the render thread's accumulator array, addressed
// [strip_index * kMixerLanes + lane]; `mixer_index` maps a strip id to its
// index for this block. Taps whose source is no longer live are skipped, as is
// any tap that resolves to the monitor itself — that would be a strip feeding
// its own accumulator, which accumulates rather than routes.
inline void mix_monitor_taps(
        std::size_t monitor_index,
        const std::vector<MonitorTap>& taps,
        const std::unordered_map<std::string, std::size_t>& mixer_index,
        std::vector<std::vector<Sample>>& lane_buffers,
        std::size_t block) noexcept {
    const std::size_t mon_base = monitor_index * kMixerLanes;
    if (mon_base + kMixerLanes > lane_buffers.size()) return;

    for (const auto& tap : taps) {
        if (!tap.source) continue;
        if (tap.src_lane >= kMixerLanes || tap.dst_lane >= kMixerLanes) continue;
        const auto sit = mixer_index.find(tap.source->id().value);
        if (sit == mixer_index.end() || sit->second == monitor_index) continue;
        const std::size_t src_idx = sit->second * kMixerLanes + tap.src_lane;
        if (src_idx >= lane_buffers.size()) continue;

        const Sample* src = lane_buffers[src_idx].data();
        Sample*       dst = lane_buffers[mon_base + tap.dst_lane].data();
        const std::size_t n = std::min({block, lane_buffers[src_idx].size(),
                                        lane_buffers[mon_base + tap.dst_lane].size()});
        for (std::size_t s = 0; s < n; ++s) dst[s] += src[s] * tap.gain;
    }
}

// The taps a strip contributes when PFL is up. Stereo goes lane-for-lane at
// unity; mono lives on lane 0 alone and would arrive hard left if tapped the
// same way, so it is centred by the same -3 dB the pan law uses at centre.
// PFL is pre-pan, as on a desk: where a bus sits in the house image does not
// move where it sits in the operator's headphones.
inline void append_monitor_taps(std::vector<MonitorTap>& out,
                                std::shared_ptr<MixerChannel> strip) {
    if (!strip) return;
    if (strip->width() >= kMixerLanes) {
        out.push_back({strip, 0, 0, 1.0f});
        out.push_back({strip, 1, 1, 1.0f});
    } else {
        const float g = db_to_linear_precise(kDefaultDownmixDb);
        out.push_back({strip, 0, 0, g});
        out.push_back({strip, 0, 1, g});
    }
}

} // namespace liveplay::audio
