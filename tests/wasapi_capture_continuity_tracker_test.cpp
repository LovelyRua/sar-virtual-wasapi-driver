#include "src/wasapi_capture_continuity_tracker.h"
#include "src/wasapi_capture_continuity_gate.h"

namespace {

bool startup_packets_are_excluded_from_measured_continuity() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 480);
    const auto warmup = tracker.Observe(100, 1000000, 256, true, false, false);
    if (warmup.warmup_frames != 256 || warmup.measured_frames != 0 ||
        tracker.stats().packets != 0) return false;
    const auto remainder = tracker.Observe(356, 1053333, 224, true, false, false);
    if (remainder.warmup_frames != 224 || remainder.measured_frames != 0 ||
        tracker.warmup_frames_remaining() != 0 || tracker.stats().packets != 0) {
        return false;
    }
    const auto measured = tracker.Observe(580, 1100000, 480, false, false, false);
    return measured.warmup_frames == 0 && measured.measured_frames == 480 &&
           tracker.measured_frames() == 480 && tracker.stats().packets == 1 &&
           tracker.stats().discontinuity_packets == 0;
}

bool packet_crossing_warmup_advances_position_and_qpc() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 100);
    tracker.Observe(0, 1000000, 80, false, false, false);
    const auto packet = tracker.Observe(80, 1010000, 160, false, false, false);
    if (packet.warmup_frames != 20 || packet.measured_frames != 140 ||
        tracker.measured_frames() != 140 || tracker.stats().packets != 1) return false;
    tracker.Observe(240, 1043332, 140, false, false, false);
    return tracker.stats().position_gap_packets == 0 &&
           tracker.stats().position_overlap_packets == 0 &&
           tracker.stats().maximum_qpc_delta_error_100ns < 2;
}

bool only_post_warmup_position_errors_fail_measured_span() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 64);
    tracker.Observe(1000, 1000000, 64, false, false, false);
    tracker.Observe(1064, 1013333, 480, false, false, false);
    tracker.Observe(1700, 1113333, 480, false, false, false);
    const auto failures = sar_driver::CaptureContinuityFailures(
        tracker.stats(), tracker.measured_frames(), tracker.measured_frames(), 960);
    return tracker.stats().position_gap_packets == 1 &&
           tracker.stats().position_gap_frames == 156 &&
           tracker.stats().maximum_position_gap_frames == 156 &&
           tracker.measured_frames() == 960 &&
           sar_driver::HasCaptureContinuityFailure(
               failures, sar_driver::CaptureContinuityFailure::position_gap);
}

bool post_warmup_timestamp_errors_remain_visible_after_recovery() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 0);
    tracker.Observe(0, 1000000, 480, false, false, false);
    tracker.Observe(480, 1100000, 480, false, false, true);
    tracker.Observe(960, 1200000, 480, false, false, false);
    return tracker.stats().timestamp_error_packets == 1 &&
           tracker.stats().position_gap_packets == 0 &&
           tracker.stats().packets == 3;
}

bool invalid_sample_rate_fails_closed() {
    sar_driver::WasapiCaptureContinuityTracker tracker(0, 0);
    tracker.Observe(0, 1000000, 480, false, false, false);
    return tracker.stats().timestamp_error_packets == 1 &&
           tracker.stats().packets == 1;
}

bool warmup_silence_is_excluded_but_measured_silence_is_retained() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 480);
    tracker.Observe(0, 1000000, 480, false, true, false);
    if (tracker.stats().silent_packets != 0 || tracker.stats().packets != 0) {
        return false;
    }
    tracker.Observe(480, 1100000, 480, false, true, false);
    return tracker.stats().silent_packets == 1 && tracker.stats().packets == 1 &&
           tracker.measured_frames() == 480;
}

bool measured_qpc_regression_is_retained_after_warmup() {
    sar_driver::WasapiCaptureContinuityTracker tracker(48000, 480);
    tracker.Observe(0, 1000000, 480, false, false, false);
    tracker.Observe(480, 1100000, 480, false, false, false);
    tracker.Observe(960, 1000000, 480, false, false, false);
    return tracker.stats().qpc_regressions == 1 &&
           tracker.stats().position_gap_packets == 0 &&
           tracker.measured_frames() == 960;
}

}  // namespace

int main() {
    return startup_packets_are_excluded_from_measured_continuity() &&
                   packet_crossing_warmup_advances_position_and_qpc() &&
                   only_post_warmup_position_errors_fail_measured_span() &&
                   post_warmup_timestamp_errors_remain_visible_after_recovery() &&
                   invalid_sample_rate_fails_closed() &&
                   warmup_silence_is_excluded_but_measured_silence_is_retained() &&
                   measured_qpc_regression_is_retained_after_warmup()
               ? 0
               : 1;
}
