#pragma once

#include "wasapi_capture_timeline.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace sar_driver {

struct CaptureContinuityPacket {
    std::uint32_t warmup_frames = 0;
    std::uint32_t measured_frames = 0;
};

// Removes the startup warmup interval before accumulating strict continuity
// evidence. Packet timestamps are advanced to the first measured frame when a
// packet straddles the warmup boundary.
class WasapiCaptureContinuityTracker {
public:
    WasapiCaptureContinuityTracker(std::uint32_t sample_rate,
                                   std::uint64_t warmup_frames) noexcept
        : sample_rate_(sample_rate), warmup_frames_remaining_(warmup_frames) {}

    CaptureContinuityPacket Observe(std::uint64_t device_position,
                                    std::uint64_t qpc_position_100ns,
                                    std::uint32_t frames, bool discontinuity,
                                    bool silent, bool timestamp_error) noexcept {
        CaptureContinuityPacket packet;
        packet.warmup_frames = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(warmup_frames_remaining_, frames));
        warmup_frames_remaining_ -= packet.warmup_frames;
        packet.measured_frames = frames - packet.warmup_frames;
        if (packet.measured_frames == 0) return packet;

        AddSaturated(measured_frames_, packet.measured_frames);
        const bool invalid_position = device_position >
            std::numeric_limits<std::uint64_t>::max() - packet.warmup_frames;
        const auto measured_position = invalid_position
            ? std::numeric_limits<std::uint64_t>::max()
            : device_position + packet.warmup_frames;
        const auto qpc_offset = sample_rate_ == 0 ? 0 :
            static_cast<std::uint64_t>(packet.warmup_frames) *
                WasapiCaptureTimeline::kQpcUnitsPerSecond / sample_rate_;
        const auto measured_qpc = qpc_position_100ns >
                std::numeric_limits<std::uint64_t>::max() - qpc_offset
            ? std::numeric_limits<std::uint64_t>::max()
            : qpc_position_100ns + qpc_offset;
        timeline_.Observe(measured_position, measured_qpc, packet.measured_frames,
                          sample_rate_, discontinuity, silent,
                          invalid_position || sample_rate_ == 0 || timestamp_error);
        return packet;
    }

    const CaptureTimelineStats& stats() const noexcept { return timeline_.stats(); }
    std::uint64_t measured_frames() const noexcept { return measured_frames_; }
    std::uint64_t warmup_frames_remaining() const noexcept {
        return warmup_frames_remaining_;
    }

private:
    static void AddSaturated(std::uint64_t& target, std::uint64_t amount) noexcept {
        target = amount > std::numeric_limits<std::uint64_t>::max() - target
                     ? std::numeric_limits<std::uint64_t>::max()
                     : target + amount;
    }

    std::uint32_t sample_rate_;
    std::uint64_t warmup_frames_remaining_;
    std::uint64_t measured_frames_ = 0;
    WasapiCaptureTimeline timeline_;
};

}  // namespace sar_driver
