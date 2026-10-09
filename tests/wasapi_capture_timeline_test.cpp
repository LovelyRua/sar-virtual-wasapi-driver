#include "src/wasapi_capture_timeline.h"

#include <cstdint>
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
           stats.position_overlap_packets == 1 && stats.position_overlap_frames == 14 &&
           stats.discontinuity_packets == 1 && stats.silent_packets == 1;
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

}  // namespace

int main() {
    return test_contiguous_packets_have_no_frame_gaps() &&
                   test_gap_and_overlap_are_measured_in_frames() &&
                   test_timestamp_error_breaks_position_comparison_chain() &&
                   test_qpc_regression_and_zero_rate_do_not_overflow()
               ? 0
               : 1;
}
