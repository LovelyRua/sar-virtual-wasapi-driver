#include "src/signal_level_coverage_policy.h"

#include <limits>

int main() {
    using sar_driver::MinimumLevelWindowsForDuration;
    return MinimumLevelWindowsForDuration(0) == 0 &&
                   MinimumLevelWindowsForDuration(2) == 0 &&
                   MinimumLevelWindowsForDuration(5) == 30 &&
                   MinimumLevelWindowsForDuration(15) == 130 &&
                   MinimumLevelWindowsForDuration(60) == 580 &&
                   MinimumLevelWindowsForDuration(3600) == 35980 &&
                   MinimumLevelWindowsForDuration(
                       std::numeric_limits<unsigned>::max()) ==
                       std::numeric_limits<std::size_t>::max()
        ? 0
        : 1;
}
