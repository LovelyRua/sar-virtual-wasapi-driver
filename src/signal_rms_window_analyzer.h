#pragma once

#include "signal_level_monitor.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sar_driver {

// Accumulates short RMS windows without allocating or waiting on the caller.
template <std::size_t FramesPerWindow>
class SignalRmsWindowAnalyzer {
    static_assert(FramesPerWindow > 0, "RMS windows must contain frames");

public:
    static constexpr std::size_t kChannels = 2;

    bool AppendFrame(float left, float right) noexcept {
        const std::array<float, kChannels> samples{{left, right}};
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            if (!std::isfinite(samples[channel])) {
                invalid_channels_[channel] = true;
                continue;
            }
            const double sample = samples[channel];
            sum_squares_[channel] += sample * sample;
        }

        ++pending_frames_;
        if (pending_frames_ != FramesPerWindow) return false;
        CompleteWindow();
        return true;
    }

    bool DiscardPartialSegment() noexcept {
        if (pending_frames_ == 0) return false;
        AddSaturated(discarded_segment_frames_, pending_frames_);
        ResetWindow();
        return true;
    }

    [[nodiscard]] const SignalLevelStats& stats() const noexcept {
        return monitor_.stats();
    }

    [[nodiscard]] bool WithinSpread(
        double maximum_spread_db,
        std::size_t minimum_valid_windows = 1) const noexcept {
        return monitor_.WithinSpread(maximum_spread_db, minimum_valid_windows);
    }

    [[nodiscard]] std::size_t pending_frames() const noexcept {
        return pending_frames_;
    }

    [[nodiscard]] std::uint64_t completed_windows() const noexcept {
        return completed_windows_;
    }

    [[nodiscard]] std::uint64_t discarded_segment_frames() const noexcept {
        return discarded_segment_frames_;
    }

private:
    void CompleteWindow() noexcept {
        std::array<double, kChannels> mean_power{};
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            mean_power[channel] = invalid_channels_[channel]
                ? std::numeric_limits<double>::quiet_NaN()
                : sum_squares_[channel] / static_cast<double>(FramesPerWindow);
        }
        if (completed_windows_ != std::numeric_limits<std::uint64_t>::max()) {
            ++completed_windows_;
        }
        monitor_.Observe(mean_power, static_cast<std::size_t>(completed_windows_));
        ResetWindow();
    }

    void ResetWindow() noexcept {
        sum_squares_ = {};
        invalid_channels_ = {};
        pending_frames_ = 0;
    }

    static void AddSaturated(std::uint64_t& target, std::size_t amount) noexcept {
        const auto value = static_cast<std::uint64_t>(amount);
        target = value > std::numeric_limits<std::uint64_t>::max() - target
            ? std::numeric_limits<std::uint64_t>::max()
            : target + value;
    }

    std::array<double, kChannels> sum_squares_{};
    std::array<bool, kChannels> invalid_channels_{};
    std::size_t pending_frames_ = 0;
    std::uint64_t completed_windows_ = 0;
    std::uint64_t discarded_segment_frames_ = 0;
    SignalLevelMonitor monitor_;
};

}  // namespace sar_driver
