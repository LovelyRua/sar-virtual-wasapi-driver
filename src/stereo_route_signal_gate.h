#pragma once

#include <cmath>

namespace sar_driver {

// Values match the bridge probe's established process exit codes.
inline unsigned StereoRouteSignalExitCode(double first_channel_power,
                                          double second_channel_power,
                                          double cross_channel_power) noexcept {
    constexpr double minimum_power = 1e12;
    if (!std::isfinite(first_channel_power) || first_channel_power < 0.0 ||
        !std::isfinite(second_channel_power) || second_channel_power < 0.0 ||
        !std::isfinite(cross_channel_power) || cross_channel_power < 0.0) {
        return 3u;
    }

    // Compare without adding large powers or multiplying leakage into overflow.
    if ((first_channel_power < minimum_power &&
         second_channel_power < minimum_power - first_channel_power) ||
        cross_channel_power > first_channel_power / 100.0 +
                                  second_channel_power / 100.0) {
        return 3u;
    }
    if (first_channel_power < minimum_power || second_channel_power < minimum_power) {
        return 4u;
    }
    return 0u;
}

} // namespace sar_driver
