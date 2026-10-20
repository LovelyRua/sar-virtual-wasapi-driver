#pragma once

#include "signal_level_coverage_policy.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace sar_driver {

struct SignalLevelStats {
    static constexpr std::size_t kNoWindow =
        std::numeric_limits<std::size_t>::max();

    std::array<double, 2> minimum_power{{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()}};
    std::array<double, 2> maximum_power{};
    std::array<std::size_t, 2> minimum_window{{kNoWindow, kNoWindow}};
    std::array<std::size_t, 2> maximum_window{{kNoWindow, kNoWindow}};
    std::size_t observed_windows = 0;
    std::size_t valid_windows = 0;
    std::size_t invalid_windows = 0;

    [[nodiscard]] static double power_dbfs(double power) noexcept {
        if (!std::isfinite(power) || power <= 0.0) {
            return -std::numeric_limits<double>::infinity();
        }
        return 10.0 * std::log10(power);
    }

    [[nodiscard]] double spread_db(unsigned channel) const noexcept {
        if (channel >= minimum_power.size() || valid_windows == 0 ||
            minimum_power[channel] <= 0.0 || maximum_power[channel] <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        return power_dbfs(maximum_power[channel]) -
               power_dbfs(minimum_power[channel]);
    }

    [[nodiscard]] double minimum_dbfs(unsigned channel) const noexcept {
        if (channel >= minimum_power.size() || valid_windows == 0 ||
            minimum_power[channel] <= 0.0) {
            return -std::numeric_limits<double>::infinity();
        }
        return power_dbfs(minimum_power[channel]);
    }

    [[nodiscard]] double maximum_dbfs(unsigned channel) const noexcept {
        if (channel >= maximum_power.size() || valid_windows == 0 ||
            maximum_power[channel] <= 0.0) {
            return -std::numeric_limits<double>::infinity();
        }
        return power_dbfs(maximum_power[channel]);
    }
};

class SignalLevelMonitor {
public:
    void Observe(const std::array<double, 2>& channel_power) noexcept {
        const auto next_window = stats_.observed_windows == SignalLevelStats::kNoWindow
            ? SignalLevelStats::kNoWindow
            : stats_.observed_windows + 1;
        Observe(channel_power, next_window);
    }

    void Observe(const std::array<double, 2>& channel_power,
                 std::size_t window_index) noexcept {
        if (stats_.observed_windows != SignalLevelStats::kNoWindow) {
            ++stats_.observed_windows;
        }
        for (const double power : channel_power) {
            if (!std::isfinite(power) || power <= 0.0) {
                if (stats_.invalid_windows != SignalLevelStats::kNoWindow) {
                    ++stats_.invalid_windows;
                }
                return;
            }
        }
        for (std::size_t channel = 0; channel < channel_power.size(); ++channel) {
            if (channel_power[channel] < stats_.minimum_power[channel]) {
                stats_.minimum_power[channel] = channel_power[channel];
                stats_.minimum_window[channel] = window_index;
            }
            if (channel_power[channel] > stats_.maximum_power[channel]) {
                stats_.maximum_power[channel] = channel_power[channel];
                stats_.maximum_window[channel] = window_index;
            }
        }
        if (stats_.valid_windows != SignalLevelStats::kNoWindow) {
            ++stats_.valid_windows;
        }
    }

    [[nodiscard]] const SignalLevelStats& stats() const noexcept { return stats_; }

    [[nodiscard]] bool WithinSpread(
        double maximum_spread_db,
        std::size_t minimum_valid_windows = 1) const noexcept {
        return std::isfinite(maximum_spread_db) && maximum_spread_db > 0.0 &&
               LevelWindowCoveragePassed(stats_.valid_windows,
                                         minimum_valid_windows) &&
               stats_.invalid_windows == 0 &&
               stats_.spread_db(0) <= maximum_spread_db &&
               stats_.spread_db(1) <= maximum_spread_db;
    }

private:
    SignalLevelStats stats_{};
};

}  // namespace sar_driver
