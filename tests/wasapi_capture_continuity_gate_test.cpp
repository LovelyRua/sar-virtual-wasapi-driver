#include "src/wasapi_capture_continuity_gate.h"

#include <cstdint>

int main() {
    sar_driver::CaptureTimelineStats clean{};
    if (!sar_driver::CaptureContinuityPassed(clean, 96000, 96000, 96000)) return 1;
    if (!sar_driver::CaptureContinuityPassed(clean, 96001, 96000, 96000)) return 2;
    if (sar_driver::CaptureContinuityPassed(clean, 95999, 96000, 96000)) return 3;
    if (sar_driver::CaptureContinuityPassed(clean, 96000, 95999, 96000)) return 4;
    if (sar_driver::CaptureContinuityFailures(clean, 96000, 96000, 96000) !=
        sar_driver::CaptureContinuityFailure::none) return 12;

    const auto captureShortfall = sar_driver::CaptureContinuityFailures(clean, 1, 96000, 96000);
    if (!sar_driver::HasCaptureContinuityFailure(
            captureShortfall, sar_driver::CaptureContinuityFailure::insufficient_capture_frames) ||
        sar_driver::HasCaptureContinuityFailure(
            captureShortfall, sar_driver::CaptureContinuityFailure::insufficient_render_frames)) return 13;
    const auto renderShortfall = sar_driver::CaptureContinuityFailures(clean, 96000, 1, 96000);
    if (!sar_driver::HasCaptureContinuityFailure(
            renderShortfall, sar_driver::CaptureContinuityFailure::insufficient_render_frames) ||
        sar_driver::HasCaptureContinuityFailure(
            renderShortfall, sar_driver::CaptureContinuityFailure::insufficient_capture_frames)) return 14;

    auto discontinuity = clean;
    discontinuity.discontinuity_packets = 1;
    if (sar_driver::CaptureContinuityPassed(discontinuity, 96000, 96000, 96000)) return 5;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(discontinuity, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::discontinuity)) return 15;
    auto silent = clean;
    silent.silent_packets = 1;
    if (sar_driver::CaptureContinuityPassed(silent, 96000, 96000, 96000)) return 10;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(silent, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::silent_packet)) return 16;
    auto timestampError = clean;
    timestampError.timestamp_error_packets = 1;
    if (sar_driver::CaptureContinuityPassed(timestampError, 96000, 96000, 96000)) return 6;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(timestampError, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::timestamp_error)) return 17;
    auto qpcRegression = clean;
    qpcRegression.qpc_regressions = 1;
    if (sar_driver::CaptureContinuityPassed(qpcRegression, 96000, 96000, 96000)) return 11;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(qpcRegression, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::qpc_regression)) return 18;
    auto gap = clean;
    gap.position_gap_packets = 1;
    gap.position_gap_frames = 48;
    if (sar_driver::CaptureContinuityPassed(gap, 96000, 96000, 96000)) return 7;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(gap, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::position_gap)) return 19;
    auto overlap = clean;
    overlap.position_overlap_packets = 1;
    overlap.position_overlap_frames = 48;
    if (sar_driver::CaptureContinuityPassed(overlap, 96000, 96000, 96000)) return 8;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(overlap, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::position_overlap)) return 20;

    // QPC rate drift is diagnostic, but backward timestamps invalidate the
    // time evidence needed to prove a continuous measured span.
    auto clockDrift = clean;
    clockDrift.maximum_qpc_delta_error_100ns = 100000;
    if (!sar_driver::CaptureContinuityPassed(clockDrift, 96000, 96000, 96000)) return 9;
    return 0;
}
