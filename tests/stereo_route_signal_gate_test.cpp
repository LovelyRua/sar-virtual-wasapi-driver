#include "src/stereo_route_signal_gate.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

using sar_driver::StereoRouteSignalExitCode;
static_assert(std::is_same_v<decltype(StereoRouteSignalExitCode(0, 0, 0)), unsigned>);

bool test_stereo_and_single_channel() {
    return StereoRouteSignalExitCode(1e12, 1e12, 0) == 0u &&
           StereoRouteSignalExitCode(4e12, 9e12, 1e10) == 0u &&
           StereoRouteSignalExitCode(1e12, 0, 0) == 4u &&
           StereoRouteSignalExitCode(0, 1e12, 0) == 4u &&
           StereoRouteSignalExitCode(0, 0, 0) == 3u &&
           StereoRouteSignalExitCode(1e11, 1e11, 0) == 3u;
}

bool test_wrong_channel_and_mono_leakage() {
    return StereoRouteSignalExitCode(0, 0, 2e12) == 3u &&
           StereoRouteSignalExitCode(1e12, 1e12, 2e12) == 3u &&
           StereoRouteSignalExitCode(1e12, 0, 1e12) == 3u &&
           StereoRouteSignalExitCode(0, 1e12, 1e12) == 3u;
}

bool test_channel_and_overall_thresholds() {
    const double below = std::nextafter(1e12, 0.0);
    const double above = std::nextafter(1e12, std::numeric_limits<double>::infinity());
    return StereoRouteSignalExitCode(below, 1e12, 0) == 4u &&
           StereoRouteSignalExitCode(1e12, below, 0) == 4u &&
           StereoRouteSignalExitCode(above, above, 0) == 0u &&
           StereoRouteSignalExitCode(5e11, 5e11, 0) == 4u &&
           StereoRouteSignalExitCode(5e11, std::nextafter(5e11, 0.0), 0) == 3u;
}

bool test_crosstalk_threshold() {
    const double allowed = 1e12 / 100.0 + 1e12 / 100.0;
    return StereoRouteSignalExitCode(1e12, 1e12, allowed) == 0u &&
           StereoRouteSignalExitCode(1e12, 1e12, std::nextafter(allowed, 0.0)) == 0u &&
           StereoRouteSignalExitCode(1e12, 1e12,
               std::nextafter(allowed, std::numeric_limits<double>::infinity())) == 3u;
}

bool test_invalid_values_fail_closed() {
    const double invalid[] = {-1.0, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()};
    for (const double value : invalid) {
        if (StereoRouteSignalExitCode(value, 1e12, 0) != 3u ||
            StereoRouteSignalExitCode(1e12, value, 0) != 3u ||
            StereoRouteSignalExitCode(1e12, 1e12, value) != 3u) return false;
    }
    return StereoRouteSignalExitCode(1e12, 1e12, -0.0) == 0u;
}

bool test_finite_extremes_do_not_overflow_comparisons() {
    const double maximum = std::numeric_limits<double>::max();
    const double allowed = maximum / 100.0 + maximum / 100.0;
    return StereoRouteSignalExitCode(maximum, maximum, allowed) == 0u &&
           StereoRouteSignalExitCode(maximum, maximum, maximum) == 3u &&
           StereoRouteSignalExitCode(maximum, maximum,
               std::nextafter(allowed, maximum)) == 3u &&
           StereoRouteSignalExitCode(maximum, 0, 0) == 4u &&
           StereoRouteSignalExitCode(0, maximum, 0) == 4u &&
           StereoRouteSignalExitCode(std::numeric_limits<double>::denorm_min(),
                                     std::numeric_limits<double>::denorm_min(), 0) == 3u;
}

} // namespace

int main() {
    if (!test_stereo_and_single_channel()) return 1;
    if (!test_wrong_channel_and_mono_leakage()) return 2;
    if (!test_channel_and_overall_thresholds()) return 3;
    if (!test_crosstalk_threshold()) return 4;
    if (!test_invalid_values_fail_closed()) return 5;
    if (!test_finite_extremes_do_not_overflow_comparisons()) return 6;
    std::cout << "Stereo route signal gate tests passed\n";
    return 0;
}
