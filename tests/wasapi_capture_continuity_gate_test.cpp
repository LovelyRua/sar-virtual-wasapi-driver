#include "src/wasapi_capture_continuity_gate.h"

#include <cstdint>

int main() {
    sar_driver::CaptureTimelineStats clean{};
    if (!sar_driver::CaptureContinuityPassed(clean, 96000, 96000, 96000)) return 1;
    if (!sar_driver::CaptureContinuityPassed(clean, 96001, 96000, 96000)) return 2;
    if (sar_driver::CaptureContinuityPassed(clean, 95999, 96000, 96000)) return 3;
    if (sar_driver::CaptureContinuityPassed(clean, 96000, 95999, 96000)) return 4;

    auto discontinuity = clean;
    discontinuity.discontinuity_packets = 1;
    if (sar_driver::CaptureContinuityPassed(discontinuity, 96000, 96000, 96000)) return 5;
    auto silent = clean;
    silent.silent_packets = 1;
    if (sar_driver::CaptureContinuityPassed(silent, 96000, 96000, 96000)) return 10;
    auto timestampError = clean;
    timestampError.timestamp_error_packets = 1;
    if (sar_driver::CaptureContinuityPassed(timestampError, 96000, 96000, 96000)) return 6;
    auto qpcRegression = clean;
    qpcRegression.qpc_regressions = 1;
    if (sar_driver::CaptureContinuityPassed(qpcRegression, 96000, 96000, 96000)) return 11;
    auto gap = clean;
    gap.position_gap_packets = 1;
    gap.position_gap_frames = 48;
    if (sar_driver::CaptureContinuityPassed(gap, 96000, 96000, 96000)) return 7;
    auto overlap = clean;
    overlap.position_overlap_packets = 1;
    overlap.position_overlap_frames = 48;
    if (sar_driver::CaptureContinuityPassed(overlap, 96000, 96000, 96000)) return 8;

    // QPC rate drift is diagnostic, but backward timestamps invalidate the
    // time evidence needed to prove a continuous measured span.
    auto clockDrift = clean;
    clockDrift.maximum_qpc_delta_error_100ns = 100000;
    if (!sar_driver::CaptureContinuityPassed(clockDrift, 96000, 96000, 96000)) return 9;
    return 0;
}
