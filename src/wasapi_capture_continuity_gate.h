#pragma once

#include "wasapi_capture_timeline.h"

#include <cstdint>
#include <type_traits>

namespace sar_driver {

enum class CaptureContinuityFailure : std::uint32_t {
    none = 0,
    discontinuity = 1U << 0,
    silent_packet = 1U << 1,
    timestamp_error = 1U << 2,
    qpc_regression = 1U << 3,
    position_gap = 1U << 4,
    position_overlap = 1U << 5,
    insufficient_capture_frames = 1U << 6,
    insufficient_render_frames = 1U << 7,
};

constexpr CaptureContinuityFailure operator|(CaptureContinuityFailure left,
                                             CaptureContinuityFailure right) noexcept {
    using Value = std::underlying_type_t<CaptureContinuityFailure>;
    return static_cast<CaptureContinuityFailure>(static_cast<Value>(left) |
                                                  static_cast<Value>(right));
}

constexpr bool HasCaptureContinuityFailure(CaptureContinuityFailure failures,
                                           CaptureContinuityFailure failure) noexcept {
    using Value = std::underlying_type_t<CaptureContinuityFailure>;
    return (static_cast<Value>(failures) & static_cast<Value>(failure)) != 0;
}

// Spectral content can survive a dropped interval; strict continuity also
// requires clean device-position and timestamp evidence for the measured span.
constexpr CaptureContinuityFailure CaptureContinuityFailures(
    const CaptureTimelineStats& timeline, std::uint64_t captured_frames,
    std::uint64_t sent_frames, std::uint64_t required_frames) noexcept {
    auto failures = CaptureContinuityFailure::none;
    if (timeline.discontinuity_packets != 0) {
        failures = failures | CaptureContinuityFailure::discontinuity;
    }
    if (timeline.silent_packets != 0) {
        failures = failures | CaptureContinuityFailure::silent_packet;
    }
    if (timeline.timestamp_error_packets != 0) {
        failures = failures | CaptureContinuityFailure::timestamp_error;
    }
    if (timeline.qpc_regressions != 0) {
        failures = failures | CaptureContinuityFailure::qpc_regression;
    }
    if (timeline.position_gap_packets != 0) {
        failures = failures | CaptureContinuityFailure::position_gap;
    }
    if (timeline.position_overlap_packets != 0) {
        failures = failures | CaptureContinuityFailure::position_overlap;
    }
    if (captured_frames < required_frames) {
        failures = failures | CaptureContinuityFailure::insufficient_capture_frames;
    }
    if (sent_frames < required_frames) {
        failures = failures | CaptureContinuityFailure::insufficient_render_frames;
    }
    return failures;
}

constexpr bool CaptureContinuityPassed(const CaptureTimelineStats& timeline,
                                      std::uint64_t captured_frames,
                                      std::uint64_t sent_frames,
                                      std::uint64_t required_frames) noexcept {
    return CaptureContinuityFailures(timeline, captured_frames, sent_frames,
                                     required_frames) == CaptureContinuityFailure::none;
}

}  // namespace sar_driver
