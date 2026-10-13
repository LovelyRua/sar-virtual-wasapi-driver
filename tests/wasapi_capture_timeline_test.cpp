#include "src/wasapi_capture_timeline.h"
#include "src/wasapi_capture_continuity_gate.h"

#include <cstdint>
#include <initializer_list>
#include <limits>

namespace {

using sar_driver::WasapiCaptureTimeline;

bool test_contiguous_packets_have_no_frame_gaps() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(1000, 1000000, 480, 48000, false, false, false);
    timeline.Observe(1480, 1100000, 480, 48000, false, false, false);
    const auto& stats = timeline.stats();
    return stats.packets == 2 && stats.position_gap_packets == 0 &&
           stats.position_overlap_packets == 0 && stats.discontinuity_packets == 0 &&
           stats.maximum_qpc_delta_error_100ns == 0;
}

bool test_gap_and_overlap_are_measured_in_frames() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(100, 1000, 64, 48000, false, false, false);
    timeline.Observe(170, 2000, 64, 48000, true, false, false);
    timeline.Observe(220, 3000, 64, 48000, false, true, false);
    const auto& stats = timeline.stats();
    return stats.position_gap_packets == 1 && stats.position_gap_frames == 6 &&
           stats.maximum_position_gap_frames == 6 &&
           stats.position_overlap_packets == 1 && stats.position_overlap_frames == 14 &&
           stats.maximum_position_overlap_frames == 14 &&
           stats.discontinuity_packets == 1 && stats.silent_packets == 1;
}

bool test_timeline_gap_maps_to_a_specific_gate_reason() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 480, 48000, false, false, false);
    timeline.Observe(960, 200000, 480, 48000, false, false, false);
    const auto failures = sar_driver::CaptureContinuityFailures(
        timeline.stats(), 960, 960, 960);
    return sar_driver::HasCaptureContinuityFailure(
               failures, sar_driver::CaptureContinuityFailure::position_gap) &&
           !sar_driver::HasCaptureContinuityFailure(
               failures, sar_driver::CaptureContinuityFailure::position_overlap);
}

bool test_identical_ab_packets_produce_identical_timeline_evidence() {
    WasapiCaptureTimeline armA;
    WasapiCaptureTimeline armB;
    for (auto* timeline : {&armA, &armB}) {
        timeline->Observe(0, 1000000, 480, 48000, false, false, false);
        timeline->Observe(480, 1100000, 480, 48000, false, false, false);
        timeline->Observe(1008, 1210000, 480, 48000, false, false, false);
    }
    const auto& a = armA.stats();
    const auto& b = armB.stats();
    return a.packets == b.packets &&
           a.position_gap_packets == b.position_gap_packets &&
           a.position_gap_frames == b.position_gap_frames &&
           a.position_overlap_packets == b.position_overlap_packets &&
           a.discontinuity_packets == b.discontinuity_packets &&
           a.maximum_qpc_delta_error_100ns == b.maximum_qpc_delta_error_100ns;
}

bool test_timestamp_error_breaks_position_comparison_chain() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 100, 48000, false, false, false);
    timeline.Observe(1000, 1000, 100, 48000, true, false, true);
    timeline.Observe(2000, 2000, 100, 48000, false, false, false);
    return timeline.stats().timestamp_error_packets == 1 &&
           timeline.stats().position_gap_packets == 0 &&
           timeline.stats().discontinuity_packets == 1;
}

bool test_qpc_regression_and_zero_rate_do_not_overflow() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 100, 32, 48000, false, false, false);
    timeline.Observe(32, 99, 32, 48000, false, false, false);
    timeline.Observe(std::numeric_limits<std::uint64_t>::max() - 1,
                     200, 8, 0, false, false, false);
    return timeline.stats().qpc_regressions == 1 &&
           timeline.stats().packets == 3;
}

bool test_extreme_positions_saturate_without_wrapping() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(1, 0, 1, 48000, false, false, false);
    timeline.Observe(std::numeric_limits<std::uint64_t>::max() - 1,
                     std::numeric_limits<std::uint64_t>::max(),
                     8, 48000, false, false, false);
    return timeline.stats().position_gap_packets == 1 &&
           timeline.stats().position_gap_frames ==
               std::numeric_limits<std::uint64_t>::max() - 3 &&
           timeline.stats().maximum_position_gap_frames ==
               std::numeric_limits<std::uint64_t>::max() - 3 &&
           timeline.stats().packets == 2;
}

bool test_maximum_gap_reports_the_largest_single_event() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 10, 48000, false, false, false);
    timeline.Observe(12, 2500, 10, 48000, false, false, false);
    timeline.Observe(40, 8000, 10, 48000, false, false, false);
    const auto& stats = timeline.stats();
    return stats.position_gap_packets == 2 && stats.position_gap_frames == 20 &&
           stats.maximum_position_gap_frames == 18;
}

bool test_timestamp_error_restarts_with_a_clean_reference_packet() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 1000000, 480, 48000, false, false, false);
    timeline.Observe(9000, 2000000, 480, 48000, false, false, true);
    timeline.Observe(7000, 3000000, 480, 48000, false, false, false);
    timeline.Observe(7480, 3100000, 480, 48000, false, false, false);
    const auto& stats = timeline.stats();
    return stats.timestamp_error_packets == 1 && stats.position_gap_packets == 0 &&
           stats.position_overlap_packets == 0 && stats.qpc_regressions == 0 &&
           stats.packets == 4;
}

bool test_qpc_delta_error_tracks_clock_drift_without_frame_gap() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(100, 1000000, 480, 48000, false, false, false);
    timeline.Observe(580, 1110000, 480, 48000, false, false, false);
    return timeline.stats().position_gap_packets == 0 &&
           timeline.stats().maximum_qpc_delta_error_100ns == 10000;
}

bool test_qpc_delta_saturates_when_frame_delta_exceeds_clock_range() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 1, 1, false, false, false);
    timeline.Observe(std::numeric_limits<std::uint64_t>::max() / 2,
                     0, 1, 1, false, false, false);
    return timeline.stats().maximum_qpc_delta_error_100ns ==
               std::numeric_limits<std::uint64_t>::max() &&
           timeline.stats().position_gap_packets == 1 &&
           timeline.stats().packets == 2;
}

bool test_discontinuity_is_not_hidden_by_contiguous_followup_packets() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 480, 48000, false, false, false);
    timeline.Observe(480, 100000, 480, 48000, true, false, false);
    timeline.Observe(960, 200000, 480, 48000, false, false, false);
    const auto& stats = timeline.stats();
    return stats.discontinuity_packets == 1 && stats.position_gap_packets == 0 &&
           stats.position_overlap_packets == 0 && stats.packets == 3;
}

bool test_discontinuity_flag_is_independent_of_position_gap() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 1000000, 480, 48000, false, false, false);
    timeline.Observe(480, 1100000, 480, 48000, true, false, false);
    const auto& stats = timeline.stats();
    return stats.discontinuity_packets == 1 && stats.position_gap_packets == 0 &&
           stats.position_overlap_packets == 0 && stats.packets == 2;
}

bool test_silent_packets_are_counted_independently_of_discontinuities() {
    WasapiCaptureTimeline timeline;
    timeline.Observe(0, 0, 480, 48000, false, false, false);
    timeline.Observe(480, 100000, 480, 48000, false, true, false);
    timeline.Observe(960, 200000, 480, 48000, false, false, false);
    const auto& stats = timeline.stats();
    return stats.packets == 3 && stats.silent_packets == 1 &&
           stats.discontinuity_packets == 0 && stats.position_gap_packets == 0;
}

}  // namespace

int main() {
    return test_contiguous_packets_have_no_frame_gaps() &&
                   test_gap_and_overlap_are_measured_in_frames() &&
                   test_timeline_gap_maps_to_a_specific_gate_reason() &&
                   test_identical_ab_packets_produce_identical_timeline_evidence() &&
                   test_timestamp_error_breaks_position_comparison_chain() &&
                   test_qpc_regression_and_zero_rate_do_not_overflow() &&
                   test_extreme_positions_saturate_without_wrapping() &&
                   test_timestamp_error_restarts_with_a_clean_reference_packet() &&
                   test_qpc_delta_error_tracks_clock_drift_without_frame_gap() &&
                   test_qpc_delta_saturates_when_frame_delta_exceeds_clock_range() &&
                   test_discontinuity_is_not_hidden_by_contiguous_followup_packets() &&
                   test_discontinuity_flag_is_independent_of_position_gap() &&
                   test_silent_packets_are_counted_independently_of_discontinuities() &&
                   test_maximum_gap_reports_the_largest_single_event()
               ? 0
               : 1;
}
