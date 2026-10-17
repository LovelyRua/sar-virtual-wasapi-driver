#include "src/signal_rms_window_analyzer.h"

#include <cmath>
#include <limits>

namespace {

bool complete_windows_report_channel_mean_power() {
    sar_driver::SignalRmsWindowAnalyzer<4> analyzer;
    if (analyzer.AppendFrame(0.25f, -0.5f) ||
        analyzer.AppendFrame(0.25f, -0.5f) ||
        analyzer.AppendFrame(0.25f, -0.5f) ||
        !analyzer.AppendFrame(0.25f, -0.5f)) return false;
    const auto& stats = analyzer.stats();
    return analyzer.pending_frames() == 0 && analyzer.completed_windows() == 1 &&
           stats.valid_windows == 1 && stats.invalid_windows == 0 &&
           std::abs(stats.minimum_power[0] - 0.0625) < 1e-9 &&
           std::abs(stats.minimum_power[1] - 0.25) < 1e-9 &&
           stats.minimum_window[0] == 1 && stats.minimum_window[1] == 1;
}

bool a_short_channel_sag_is_independently_tracked() {
    sar_driver::SignalRmsWindowAnalyzer<4> analyzer;
    for (int frame = 0; frame < 4; ++frame) analyzer.AppendFrame(0.2f, 0.2f);
    for (int frame = 0; frame < 4; ++frame) analyzer.AppendFrame(0.05f, 0.2f);
    const auto& stats = analyzer.stats();
    return stats.valid_windows == 2 && stats.invalid_windows == 0 &&
           stats.minimum_window[0] == 2 && stats.minimum_window[1] == 1 &&
           stats.spread_db(0) > 12.0 && stats.spread_db(1) < 1e-9;
}

bool a_segment_break_discards_only_the_incomplete_tail() {
    sar_driver::SignalRmsWindowAnalyzer<4> analyzer;
    analyzer.AppendFrame(0.9f, 0.9f);
    analyzer.AppendFrame(0.9f, 0.9f);
    if (!analyzer.DiscardPartialSegment() || analyzer.DiscardPartialSegment()) return false;
    for (int frame = 0; frame < 4; ++frame) analyzer.AppendFrame(0.25f, 0.5f);
    const auto& stats = analyzer.stats();
    return analyzer.discarded_segment_frames() == 2 && analyzer.pending_frames() == 0 &&
           analyzer.completed_windows() == 1 && stats.valid_windows == 1 &&
           std::abs(stats.minimum_power[0] - 0.0625) < 1e-9 &&
           std::abs(stats.minimum_power[1] - 0.25) < 1e-9;
}

bool non_finite_samples_fail_closed_without_poisoning_prior_windows() {
    sar_driver::SignalRmsWindowAnalyzer<2> analyzer;
    analyzer.AppendFrame(0.2f, 0.2f);
    analyzer.AppendFrame(0.2f, 0.2f);
    analyzer.AppendFrame(std::numeric_limits<float>::quiet_NaN(), 0.2f);
    analyzer.AppendFrame(0.2f, 0.2f);
    const auto& stats = analyzer.stats();
    return stats.valid_windows == 1 && stats.invalid_windows == 1 &&
           stats.minimum_window[0] == 1 && stats.minimum_window[1] == 1 &&
           !analyzer.WithinSpread(60.0);
}

bool minimum_valid_window_count_is_enforced() {
    sar_driver::SignalRmsWindowAnalyzer<2> analyzer;
    for (unsigned frame = 0; frame < 4; ++frame) {
        analyzer.AppendFrame(0.2f, 0.2f);
    }
    return analyzer.stats().valid_windows == 2 &&
           !analyzer.WithinSpread(1.0, 3) &&
           analyzer.WithinSpread(1.0, 2);
}

}  // namespace

int main() {
    return complete_windows_report_channel_mean_power() &&
                   a_short_channel_sag_is_independently_tracked() &&
                   a_segment_break_discards_only_the_incomplete_tail() &&
                   non_finite_samples_fail_closed_without_poisoning_prior_windows() &&
                   minimum_valid_window_count_is_enforced()
               ? 0
               : 1;
}
