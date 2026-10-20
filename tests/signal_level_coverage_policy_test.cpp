#include "src/signal_level_coverage_policy.h"

#include <limits>

int main() {
    using sar_driver::MinimumLevelWindowsForDuration;
    using sar_driver::LevelWindowCoveragePassed;
    const auto maximum_seconds = std::numeric_limits<unsigned>::max();
    const auto measured_seconds = static_cast<std::size_t>(
        maximum_seconds - sar_driver::kProbeUnmeasuredSeconds);
    const auto maximum_window_count = std::numeric_limits<std::size_t>::max();
    const auto expected_maximum = measured_seconds >
            maximum_window_count / sar_driver::kLevelWindowsPerSecond
        ? maximum_window_count
        : measured_seconds * sar_driver::kLevelWindowsPerSecond;
    return MinimumLevelWindowsForDuration(0) == 0 &&
                   MinimumLevelWindowsForDuration(2) == 0 &&
                   MinimumLevelWindowsForDuration(5) == 30 &&
                   MinimumLevelWindowsForDuration(15) == 130 &&
                   MinimumLevelWindowsForDuration(60) == 580 &&
                   MinimumLevelWindowsForDuration(3600) == 35980 &&
                   MinimumLevelWindowsForDuration(maximum_seconds) ==
                       expected_maximum &&
                   !LevelWindowCoveragePassed(129, 130) &&
                   LevelWindowCoveragePassed(130, 130) &&
                   !LevelWindowCoveragePassed(1000, 0)
        ? 0
        : 1;
}
