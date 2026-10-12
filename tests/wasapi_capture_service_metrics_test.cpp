#include "src/wasapi_capture_service_metrics.h"
#include "src/wasapi_capture_continuity_gate.h"

#include <cstdint>
#include <limits>

int main() {
    sar_driver::WasapiCaptureServiceMetrics metrics;
    metrics.BeginPass();
    metrics.ObservePacket(1000, 1500, 1000);
    metrics.ObservePacket(1600, 3100, 1000);
    metrics.EndPass();
    metrics.BeginPass();
    metrics.EndPass();
    const auto& stats = metrics.stats();
    if (stats.service_passes != 2 || stats.packets != 2 ||
        stats.empty_service_passes != 1 || stats.maximum_packets_per_pass != 2 ||
        stats.total_timestamp_age_100ns != 2000 ||
        stats.maximum_timestamp_age_100ns != 1500 ||
        stats.packets_over_late_threshold != 1 ||
        stats.packets_with_valid_timestamp != 2) return 1;

    // Equality with one device period is not late; only strictly older
    // packets cross the diagnostic threshold.
    metrics.BeginPass();
    metrics.ObservePacket(4000, 5000, 1000);
    metrics.EndPass();
    if (metrics.stats().packets_over_late_threshold != 1 ||
        metrics.stats().maximum_timestamp_age_100ns != 1500 ||
        metrics.stats().total_timestamp_age_100ns != 3000) return 4;

    metrics.BeginPass();
    metrics.ObservePacket(5000, 4000, 1000);
    metrics.EndPass();
    if (metrics.stats().packets_with_future_timestamp != 1 ||
        metrics.stats().total_timestamp_age_100ns != 3000 ||
        metrics.stats().maximum_timestamp_age_100ns != 1500) return 2;

    metrics.BeginPass();
    metrics.ObservePacket(0, std::numeric_limits<std::uint64_t>::max(), 0);
    metrics.ObservePacket(0, std::numeric_limits<std::uint64_t>::max(), 0);
    metrics.EndPass();
    if (metrics.stats().total_timestamp_age_100ns !=
            std::numeric_limits<std::uint64_t>::max() ||
        metrics.stats().packets_over_late_threshold != 1 ||
        metrics.stats().packets_with_valid_timestamp != 5 ||
        metrics.stats().packets_with_future_timestamp != 1) return 3;

    // A later empty service pass must not erase prior latency or packet counts.
    metrics.BeginPass();
    metrics.ObservePacket(10000, 13000, 1000);
    metrics.ObservePacket(13000, 16000, 1000);
    metrics.ObservePacket(16000, 17000, 1000);
    metrics.EndPass();
    metrics.BeginPass();
    metrics.EndPass();
    if (metrics.stats().service_passes != 7 ||
        metrics.stats().empty_service_passes != 2 ||
        metrics.stats().maximum_packets_per_pass != 3 ||
        metrics.stats().packets != 9 ||
        metrics.stats().packets_over_late_threshold != 3 ||
        metrics.stats().maximum_timestamp_age_100ns !=
            std::numeric_limits<std::uint64_t>::max() ||
        metrics.stats().total_timestamp_age_100ns !=
            std::numeric_limits<std::uint64_t>::max()) return 5;

    // Late servicing is important diagnostic evidence, but it is not itself
    // a device-position gap. Keep that distinction in the strict gate.
    sar_driver::CaptureTimelineStats contiguous{};
    if (!sar_driver::CaptureContinuityPassed(contiguous, 48000, 48000, 48000)) {
        return 6;
    }
    sar_driver::WasapiCaptureServiceMetrics late;
    late.BeginPass();
    late.ObservePacket(1000, 5000, 1000);
    late.EndPass();
    if (late.stats().packets_over_late_threshold != 1 ||
        sar_driver::CaptureContinuityFailures(contiguous, 48000, 48000, 48000) !=
            sar_driver::CaptureContinuityFailure::none) return 9;
    sar_driver::WasapiCaptureServiceMetrics timely;
    timely.BeginPass();
    timely.ObservePacket(1000, 1100, 1000);
    timely.EndPass();
    sar_driver::CaptureTimelineStats silentCapture{};
    silentCapture.silent_packets = 1;
    if (timely.stats().packets_over_late_threshold != 0 ||
        timely.stats().packets_with_future_timestamp != 0 ||
        sar_driver::CaptureContinuityPassed(silentCapture, 48000, 48000, 48000)) {
        return 7;
    }
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(silentCapture, 48000, 48000, 48000),
            sar_driver::CaptureContinuityFailure::silent_packet)) return 10;
    sar_driver::CaptureTimelineStats regressedCapture{};
    regressedCapture.qpc_regressions = 1;
    if (timely.stats().packets_over_late_threshold != 0 ||
        sar_driver::CaptureContinuityPassed(regressedCapture, 48000, 48000, 48000)) {
        return 8;
    }
    if (!sar_driver::HasCaptureContinuityFailure(
            sar_driver::CaptureContinuityFailures(regressedCapture, 48000, 48000, 48000),
            sar_driver::CaptureContinuityFailure::qpc_regression)) return 11;
    return 0;
}
