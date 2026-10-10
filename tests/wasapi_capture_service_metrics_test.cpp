#include "src/wasapi_capture_service_metrics.h"

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
    return 0;
}
