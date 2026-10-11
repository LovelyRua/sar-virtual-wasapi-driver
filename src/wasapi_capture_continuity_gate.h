#pragma once

#include "wasapi_capture_timeline.h"

#include <cstdint>

namespace sar_driver {

// Spectral content can survive a dropped interval; strict continuity also
// requires clean device-position and timestamp evidence for the measured span.
inline bool CaptureContinuityPassed(const CaptureTimelineStats& timeline,
                                    std::uint64_t captured_frames,
                                    std::uint64_t sent_frames,
                                    std::uint64_t required_frames) noexcept {
    return timeline.discontinuity_packets == 0 &&
           timeline.timestamp_error_packets == 0 &&
           timeline.position_gap_packets == 0 &&
           timeline.position_overlap_packets == 0 &&
           captured_frames >= required_frames && sent_frames >= required_frames;
}

}  // namespace sar_driver
