#pragma once

#include <cstddef>
#include <limits>

namespace sar_driver {

// One second is warmup and one second is the trailing partial-window allowance.
constexpr unsigned kProbeUnmeasuredSeconds = 2;
constexpr std::size_t kLevelWindowsPerSecond = 10;

constexpr bool LevelWindowCoveragePassed(
    std::size_t observed_windows,
    std::size_t required_windows) noexcept {
    return required_windows != 0 && observed_windows >= required_windows;
}

constexpr std::size_t MinimumLevelWindowsForDuration(unsigned seconds) noexcept {
    if (seconds <= kProbeUnmeasuredSeconds) return 0;
    const auto measured_seconds =
        static_cast<std::size_t>(seconds - kProbeUnmeasuredSeconds);
    return measured_seconds >
            std::numeric_limits<std::size_t>::max() / kLevelWindowsPerSecond
        ? std::numeric_limits<std::size_t>::max()
        : measured_seconds * kLevelWindowsPerSecond;
}

}  // namespace sar_driver
