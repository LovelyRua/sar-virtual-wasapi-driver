#include "src/wasapi_capture_continuity_gate.h"
#include "src/wasapi_capture_continuity_tracker.h"
#include "src/capture_signal_window_assembler.h"
#include "src/signal_level_monitor.h"
#include "src/signal_window_queue.h"

#include <cstdint>

int main() {
    sar_driver::CaptureTimelineStats clean{};
    if (!sar_driver::CaptureContinuityPassed(clean, 96000, 96000, 96000)) return 1;
    sar_driver::SignalLevelMonitor saggedLevel;
    saggedLevel.Observe({0.04, 0.04}, 1);
    saggedLevel.Observe({0.04, 0.001}, 2);
    if (saggedLevel.WithinSpread(1.0) ||
        !sar_driver::CaptureContinuityPassed(clean, 96000, 96000, 96000)) return 28;
    sar_driver::SignalLevelMonitor stableLevel;
    stableLevel.Observe({0.04, 0.04}, 1);
    stableLevel.Observe({0.04, 0.04}, 2);
    auto gapWithStableLevel = clean;
    gapWithStableLevel.position_gap_packets = 1;
    gapWithStableLevel.position_gap_frames = 48;
    if (!stableLevel.WithinSpread(1.0) ||
        sar_driver::CaptureContinuityPassed(
            gapWithStableLevel, 96000, 96000, 96000)) return 29;
    // Both scheduling arms must judge the same measured stream identically.
    const auto cleanArmA = sar_driver::CaptureContinuityFailures(clean, 96000, 96000, 96000);
    const auto cleanArmB = sar_driver::CaptureContinuityFailures(clean, 96000, 96000, 96000);
    if (cleanArmA != cleanArmB || cleanArmA != sar_driver::CaptureContinuityFailure::none) return 20;
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
    const auto gapArmA = sar_driver::CaptureContinuityFailures(gap, 96000, 96000, 96000);
    const auto gapArmB = sar_driver::CaptureContinuityFailures(gap, 96000, 96000, 96000);
    if (gapArmA != gapArmB) return 21;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(gap, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::position_gap)) return 19;
    auto vmSizedGap = clean;
    vmSizedGap.position_gap_packets = 1;
    vmSizedGap.position_gap_frames = 384;
    vmSizedGap.maximum_position_gap_frames = 384;
    const auto vmGapFailures = sar_driver::CaptureContinuityFailures(
        vmSizedGap, 96000, 96000, 96000);
    if (sar_driver::CaptureContinuityPassed(vmSizedGap, 96000, 96000, 96000) ||
        !sar_driver::HasCaptureContinuityFailure(
            vmGapFailures, sar_driver::CaptureContinuityFailure::position_gap) ||
        sar_driver::HasCaptureContinuityFailure(
            vmGapFailures, sar_driver::CaptureContinuityFailure::position_overlap)) {
        return 27;
    }
    auto overlap = clean;
    overlap.position_overlap_packets = 1;
    overlap.position_overlap_frames = 48;
    if (sar_driver::CaptureContinuityPassed(overlap, 96000, 96000, 96000)) return 8;
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(overlap, 96000, 96000, 96000),
            sar_driver::CaptureContinuityFailure::position_overlap)) return 20;

    auto combined = clean;
    combined.discontinuity_packets = 1;
    combined.silent_packets = 1;
    combined.position_gap_packets = 1;
    combined.position_gap_frames = 480;
    const auto combinedFailures = sar_driver::CaptureContinuityFailures(
        combined, 96000, 96000, 96000);
    if (!sar_driver::HasCaptureContinuityFailure(
            combinedFailures, sar_driver::CaptureContinuityFailure::discontinuity) ||
        !sar_driver::HasCaptureContinuityFailure(
            combinedFailures, sar_driver::CaptureContinuityFailure::silent_packet) ||
        !sar_driver::HasCaptureContinuityFailure(
            combinedFailures, sar_driver::CaptureContinuityFailure::position_gap) ||
        sar_driver::HasCaptureContinuityFailure(
            combinedFailures, sar_driver::CaptureContinuityFailure::position_overlap)) {
        return 21;
    }

    // QPC rate drift is diagnostic, but backward timestamps invalidate the
    // time evidence needed to prove a continuous measured span.
    auto clockDrift = clean;
    clockDrift.maximum_qpc_delta_error_100ns = 100000;
    if (!sar_driver::CaptureContinuityPassed(clockDrift, 96000, 96000, 96000)) return 9;

    // Segmenting signal analysis prevents stitched windows but must never
    // erase the independent strict timeline failure.
    using Queue = sar_driver::SignalWindowQueue<2, 2, 2>;
    using Assembler = sar_driver::CaptureSignalWindowAssembler<Queue>;
    sar_driver::WasapiCaptureContinuityTracker tracker(4, 0);
    Queue queue;
    Assembler assembler;
    const auto first = tracker.Observe(0, 1000000, 1, false, false, false);
    if (first.segment_break || assembler.AppendFrame(1.0f, 1.0f, queue)) return 22;
    const auto gapPacket = tracker.Observe(5, 2000000, 2, false, false, false);
    if (!gapPacket.segment_break || !assembler.DiscardPartialSegment()) return 23;
    if (assembler.AppendFrame(2.0f, 2.0f, queue) ||
        !assembler.AppendFrame(2.0f, 2.0f, queue)) return 24;
    bool cleanNewSegment = false;
    if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
            cleanNewSegment = count == 4 && samples[0] == 2.0f && samples[2] == 2.0f;
        }) || !cleanNewSegment) return 25;
    const auto segmentFailures = sar_driver::CaptureContinuityFailures(
        tracker.stats(), tracker.measured_frames(), tracker.measured_frames(), 3);
    if (!sar_driver::HasCaptureContinuityFailure(
            segmentFailures, sar_driver::CaptureContinuityFailure::position_gap) ||
        sar_driver::CaptureContinuityPassed(
            tracker.stats(), tracker.measured_frames(), tracker.measured_frames(), 3)) {
        return 26;
    }
    return 0;
}
