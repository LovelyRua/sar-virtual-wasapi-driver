#include "src/signal_level_coverage_policy.h"
#include "src/signal_rms_window_analyzer.h"

int main() {
    constexpr unsigned duration_seconds = 15;
    constexpr std::size_t required_windows =
        sar_driver::MinimumLevelWindowsForDuration(duration_seconds);
    sar_driver::SignalRmsWindowAnalyzer<1> analyzer;

    for (std::size_t window = 0; window + 1 < required_windows; ++window) {
        analyzer.AppendFrame(0.25f, 0.25f);
    }
    if (analyzer.stats().valid_windows != required_windows - 1 ||
        analyzer.WithinSpread(1.0, required_windows)) {
        return 1;
    }

    analyzer.AppendFrame(0.25f, 0.25f);
    return analyzer.stats().valid_windows == required_windows &&
                   analyzer.WithinSpread(1.0, required_windows)
        ? 0
        : 2;
}
