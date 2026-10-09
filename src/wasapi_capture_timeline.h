#pragma once

#include <cstdint>
#include <limits>

namespace sar_driver {

struct CaptureTimelineStats {
    std::uint64_t packets = 0;
    std::uint64_t discontinuity_packets = 0;
    std::uint64_t silent_packets = 0;
    std::uint64_t timestamp_error_packets = 0;
    std::uint64_t position_gap_packets = 0;
    std::uint64_t position_gap_frames = 0;
    std::uint64_t position_overlap_packets = 0;
    std::uint64_t position_overlap_frames = 0;
    std::uint64_t qpc_regressions = 0;
    std::uint64_t maximum_qpc_delta_error_100ns = 0;
};

class WasapiCaptureTimeline {
public:
    static constexpr std::uint64_t kQpcUnitsPerSecond = 10000000;

    void Observe(std::uint64_t device_position, std::uint64_t qpc_position_100ns,
                 std::uint32_t frames, std::uint32_t sample_rate,
                 bool discontinuity, bool silent, bool timestamp_error) {
        ++stats_.packets;
        if (discontinuity) ++stats_.discontinuity_packets;
        if (silent) ++stats_.silent_packets;
        if (timestamp_error) ++stats_.timestamp_error_packets;

        if (have_previous_ && !timestamp_error && sample_rate != 0) {
            const std::uint64_t expected = SaturatingAdd(previous_position_, previous_frames_);
            if (device_position > expected) {
                ++stats_.position_gap_packets;
                AddSaturated(stats_.position_gap_frames, device_position - expected);
            } else if (device_position < expected) {
                ++stats_.position_overlap_packets;
                AddSaturated(stats_.position_overlap_frames, expected - device_position);
            }

            if (qpc_position_100ns < previous_qpc_) {
                ++stats_.qpc_regressions;
            } else if (device_position >= previous_position_) {
                const std::uint64_t position_delta = device_position - previous_position_;
                const std::uint64_t whole_seconds = position_delta / sample_rate;
                const std::uint64_t remainder_frames = position_delta % sample_rate;
                const std::uint64_t expected_qpc =
                    whole_seconds > std::numeric_limits<std::uint64_t>::max() /
                                        kQpcUnitsPerSecond
                        ? std::numeric_limits<std::uint64_t>::max()
                        : SaturatingAdd(whole_seconds * kQpcUnitsPerSecond,
                                        remainder_frames * kQpcUnitsPerSecond /
                                            sample_rate);
                const std::uint64_t actual_qpc = qpc_position_100ns - previous_qpc_;
                const std::uint64_t error = actual_qpc > expected_qpc
                                                ? actual_qpc - expected_qpc
                                                : expected_qpc - actual_qpc;
                if (error > stats_.maximum_qpc_delta_error_100ns) {
                    stats_.maximum_qpc_delta_error_100ns = error;
                }
            }
        }

        if (timestamp_error || sample_rate == 0 ||
            device_position > std::numeric_limits<std::uint64_t>::max() - frames) {
            have_previous_ = false;
            return;
        }
        previous_position_ = device_position;
        previous_qpc_ = qpc_position_100ns;
        previous_frames_ = frames;
        have_previous_ = true;
    }

    const CaptureTimelineStats& stats() const { return stats_; }

private:
    static std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) {
        return right > std::numeric_limits<std::uint64_t>::max() - left
                   ? std::numeric_limits<std::uint64_t>::max()
                   : left + right;
    }

    static void AddSaturated(std::uint64_t& target, std::uint64_t amount) {
        target = SaturatingAdd(target, amount);
    }

    CaptureTimelineStats stats_{};
    std::uint64_t previous_position_ = 0;
    std::uint64_t previous_qpc_ = 0;
    std::uint32_t previous_frames_ = 0;
    bool have_previous_ = false;
};

}  // namespace sar_driver
