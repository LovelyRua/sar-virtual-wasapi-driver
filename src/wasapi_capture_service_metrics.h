#pragma once

#include <cstdint>
#include <limits>

namespace sar_driver {

struct CaptureServiceStats {
    std::uint64_t service_passes = 0;
    std::uint64_t packets = 0;
    std::uint64_t empty_service_passes = 0;
    std::uint64_t packets_with_future_timestamp = 0;
    std::uint64_t packets_over_period = 0;
    std::uint64_t total_timestamp_age_100ns = 0;
    std::uint64_t maximum_timestamp_age_100ns = 0;
    std::uint32_t maximum_packets_per_pass = 0;
};

// Measures user-mode packet service latency without allocating or changing the
// capture path. All timestamps use WASAPI's 100 ns QPC time base.
class WasapiCaptureServiceMetrics {
public:
    void BeginPass() {
        ++stats_.service_passes;
        packets_this_pass_ = 0;
    }

    void ObservePacket(std::uint64_t packet_qpc_100ns,
                       std::uint64_t serviced_qpc_100ns,
                       std::uint64_t period_100ns) {
        ++stats_.packets;
        ++packets_this_pass_;
        if (packet_qpc_100ns > serviced_qpc_100ns) {
            ++stats_.packets_with_future_timestamp;
            return;
        }
        const std::uint64_t age = serviced_qpc_100ns - packet_qpc_100ns;
        AddSaturated(stats_.total_timestamp_age_100ns, age);
        if (age > stats_.maximum_timestamp_age_100ns) {
            stats_.maximum_timestamp_age_100ns = age;
        }
        if (period_100ns != 0 && age > period_100ns) {
            ++stats_.packets_over_period;
        }
    }

    void EndPass() {
        if (packets_this_pass_ == 0) {
            ++stats_.empty_service_passes;
        }
        if (packets_this_pass_ > stats_.maximum_packets_per_pass) {
            stats_.maximum_packets_per_pass = packets_this_pass_;
        }
    }

    const CaptureServiceStats& stats() const { return stats_; }

private:
    static void AddSaturated(std::uint64_t& target, std::uint64_t amount) {
        target = amount > std::numeric_limits<std::uint64_t>::max() - target
                     ? std::numeric_limits<std::uint64_t>::max()
                     : target + amount;
    }

    CaptureServiceStats stats_{};
    std::uint32_t packets_this_pass_ = 0;
};

}  // namespace sar_driver
