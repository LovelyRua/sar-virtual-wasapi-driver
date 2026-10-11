#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace sar_driver {

struct SignalLevelStats {
    std::array<double, 2> minimum_power{{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()}};
    std::array<double, 2> maximum_power{};
    std::size_t valid_windows = 0;
    std::size_t invalid_windows = 0;

    [[nodiscard]] double spread_db(unsigned channel) const noexcept {
        if (channel >= minimum_power.size() || valid_windows == 0 ||
            minimum_power[channel] <= 0.0 || maximum_power[channel] <= 0.0) {
            return std::numeric_limits<double>::infinity();
        }
        return 10.0 * (std::log10(maximum_power[channel]) -
                       std::log10(minimum_power[channel]));
    }

    [[nodiscard]] double minimum_dbfs(unsigned channel) const noexcept {
        if (channel >= minimum_power.size() || valid_windows == 0 ||
            minimum_power[channel] <= 0.0) {
            return -std::numeric_limits<double>::infinity();
        }
        return 10.0 * std::log10(minimum_power[channel]);
    }

    [[nodiscard]] double maximum_dbfs(unsigned channel) const noexcept {
        if (channel >= maximum_power.size() || valid_windows == 0 ||
            maximum_power[channel] <= 0.0) {
            return -std::numeric_limits<double>::infinity();
        }
        return 10.0 * std::log10(maximum_power[channel]);
    }
};

class SignalLevelMonitor {
public:
    void Observe(const std::array<double, 2>& channel_power) noexcept {
        for (const double power : channel_power) {
            if (!std::isfinite(power) || power <= 0.0) {
                ++stats_.invalid_windows;
                return;
            }
        }
        for (std::size_t channel = 0; channel < channel_power.size(); ++channel) {
            if (channel_power[channel] < stats_.minimum_power[channel]) {
                stats_.minimum_power[channel] = channel_power[channel];
            }
            if (channel_power[channel] > stats_.maximum_power[channel]) {
                stats_.maximum_power[channel] = channel_power[channel];
            }
        }
        ++stats_.valid_windows;
    }

    [[nodiscard]] const SignalLevelStats& stats() const noexcept { return stats_; }

    [[nodiscard]] bool WithinSpread(double maximum_spread_db) const noexcept {
        return std::isfinite(maximum_spread_db) && maximum_spread_db > 0.0 &&
               stats_.valid_windows != 0 && stats_.invalid_windows == 0 &&
               stats_.spread_db(0) <= maximum_spread_db &&
               stats_.spread_db(1) <= maximum_spread_db;
    }

private:
    SignalLevelStats stats_{};
};

}  // namespace sar_driver
