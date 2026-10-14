#include "src/dual_bus_signal.h"
#include "src/wasapi_capture_continuity_gate.h"
#include "src/signal_level_monitor.h"
#include "src/signal_rms_window_analyzer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

using sar_driver::AnalyzeSignalWindow;
using sar_driver::kProbeRate;
using sar_driver::ProbeFrame;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<float> MakeWindow(unsigned bus) {
    std::vector<float> samples(kProbeRate * 2);
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        const auto pcm = ProbeFrame(bus, frame);
        samples[frame * 2] = pcm[0] / 32768.0f;
        samples[frame * 2 + 1] = pcm[1] / 32768.0f;
    }
    return samples;
}

void TestPairedBuses() {
    for (unsigned bus = 0; bus < sar_driver::kProbeBuses; ++bus) {
        const auto samples = MakeWindow(bus);
        const auto result = AnalyzeSignalWindow(samples.data(), kProbeRate, bus);
        Expect(result.passed(), "paired bus must pass");
        Expect(result.expected_power[0] > 0.01, "left tone must have power");
        Expect(result.expected_power[1] > 0.01, "right tone must have power");
        Expect(result.wrong_channel_power[0] < 1e-6,
               "right tone must not leak into left");
        Expect(result.wrong_channel_power[1] < 1e-6,
               "left tone must not leak into right");
        Expect(result.other_bus_power[0] < 1e-6,
               "other bus must not leak into left");
        Expect(result.other_bus_power[1] < 1e-6,
               "other bus must not leak into right");
    }
}

void TestDcOffsetPreservesToneClassification() {
    auto samples = MakeWindow(1);
    for (float& sample : samples) sample += 0.1f;
    const auto result = AnalyzeSignalWindow(samples.data(), kProbeRate, 1);
    Expect(result.passed(), "moderate DC offset must not hide valid bus tones");
    Expect(result.channel_order_ok && result.bus_isolation_ok,
           "DC offset must not alter channel or bus classification");
    Expect(result.sample_integrity_ok && result.peak_absolute_sample < 0.999,
           "in-range DC offset must not be treated as clipping");
}

void TestProbeToneIsIndependentOfPacketBoundaries() {
    constexpr std::array<unsigned, 5> packetSizes{{1, 127, 480, 1024, 4096}};
    for (unsigned bus = 0; bus < sar_driver::kProbeBuses; ++bus) {
        std::uint64_t frame = 0;
        for (const unsigned packetSize : packetSizes) {
            for (unsigned offset = 0; offset < packetSize; ++offset) {
                const auto whole = ProbeFrame(bus, frame);
                const auto wrapped = ProbeFrame(bus, frame % kProbeRate);
                Expect(whole == wrapped,
                       "tone phase must remain stable across render packet boundaries");
                ++frame;
            }
        }
    }
}

void TestSilenceAndMissingChannel() {
    std::vector<float> silence(kProbeRate * 2, 0.0f);
    const auto zero = AnalyzeSignalWindow(silence.data(), kProbeRate, 0);
    Expect(!zero.passed(), "silence must fail");
    Expect(!zero.enough_signal, "silence must report missing signal");

    auto samples = MakeWindow(0);
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        samples[frame * 2 + 1] = 0.0f;
    }
    const auto missing = AnalyzeSignalWindow(samples.data(), kProbeRate, 0);
    Expect(!missing.passed(), "missing right channel must fail");
    Expect(missing.expected_power[0] > 0.01,
           "present left channel must remain detectable");
    Expect(missing.expected_power[1] < 1e-10,
           "missing right channel must have no expected power");
}

void TestSwappedAndDuplicatedChannels() {
    auto swapped = MakeWindow(0);
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        const float left = swapped[frame * 2];
        swapped[frame * 2] = swapped[frame * 2 + 1];
        swapped[frame * 2 + 1] = left;
    }
    const auto reversed = AnalyzeSignalWindow(swapped.data(), kProbeRate, 0);
    Expect(!reversed.passed(), "swapped stereo channels must fail");
    Expect(!reversed.channel_order_ok, "swap must flag channel order");

    auto duplicated = MakeWindow(0);
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        duplicated[frame * 2 + 1] = duplicated[frame * 2];
    }
    const auto mono = AnalyzeSignalWindow(duplicated.data(), kProbeRate, 0);
    Expect(!mono.passed(), "duplicated mono must fail stereo check");
    Expect(!mono.enough_signal, "duplicated left tone lacks right tone");
}

void TestCrossBusAndAttenuation() {
    for (unsigned source = 0; source < sar_driver::kProbeBuses; ++source) {
        const auto signal = MakeWindow(source);
        for (unsigned target = 0; target < sar_driver::kProbeBuses; ++target) {
            if (source == target) continue;
            const auto wrongBus = AnalyzeSignalWindow(signal.data(), kProbeRate, target);
            Expect(!wrongBus.passed(), "cross-bus signal must fail");
            Expect(!wrongBus.enough_signal, "cross-bus signal lacks target tones");
        }
    }

    auto mixed = MakeWindow(0);
    for (unsigned bus = 1; bus < sar_driver::kProbeBuses; ++bus) {
        const auto other = MakeWindow(bus);
        for (std::size_t index = 0; index < mixed.size(); ++index) {
            mixed[index] += other[index] * 0.5f;
        }
    }
    const auto leakage = AnalyzeSignalWindow(mixed.data(), kProbeRate, 0);
    Expect(!leakage.passed(), "strong bus bleed must fail");
    Expect(!leakage.bus_isolation_ok, "bus bleed must be reported");

    auto lowLeakage = MakeWindow(2);
    const auto quietBus = MakeWindow(0);
    for (std::size_t index = 0; index < lowLeakage.size(); ++index) {
        lowLeakage[index] += quietBus[index] * 0.01f;
    }
    Expect(AnalyzeSignalWindow(lowLeakage.data(), kProbeRate, 2).passed(),
           "low-level unrelated bus must remain within isolation threshold");

    auto swappedBusLeakage = MakeWindow(2);
    for (std::size_t index = 0; index < swappedBusLeakage.size(); ++index) {
        swappedBusLeakage[index] += quietBus[index] * 0.2f;
    }
    const auto busLeak = AnalyzeSignalWindow(swappedBusLeakage.data(), kProbeRate, 2);
    Expect(!busLeak.passed() && !busLeak.bus_isolation_ok,
           "above-threshold unrelated bus must fail isolation");

    auto attenuated = MakeWindow(0);
    for (float& value : attenuated) value *= 0.1f;
    Expect(AnalyzeSignalWindow(attenuated.data(), kProbeRate, 0).passed(),
           "moderately attenuated clean signal must pass");
}

void TestInvalidInput() {
    const auto samples = MakeWindow(0);
    Expect(!AnalyzeSignalWindow(nullptr, kProbeRate, 0).passed(),
           "null input must fail");
    Expect(!AnalyzeSignalWindow(samples.data(), kProbeRate - 1, 0).passed(),
           "short window must fail");
    Expect(!AnalyzeSignalWindow(samples.data(), kProbeRate, 4).passed(),
           "unknown bus must fail");
    Expect(!AnalyzeSignalWindow(samples.data(), kProbeRate, 0, 0.0).passed(),
           "invalid threshold must fail");
    Expect(!AnalyzeSignalWindow(samples.data(), kProbeRate, 0, 1e-5, 1.0).passed(),
           "invalid isolation ratio must fail");
    const float one_sample = 0.25f;
    Expect(sar_driver::TonePower(&one_sample,
                                 std::numeric_limits<std::size_t>::max(), 0, 997) == 0.0,
           "tone analyzer must reject frame counts that overflow interleaved indexing");
    Expect(sar_driver::TonePower(&one_sample, 1, 2, 997) == 0.0,
           "tone analyzer must reject channel indices outside stereo");
    Expect(ProbeFrame(4, 0) == std::array<std::int16_t, 2>{0, 0},
           "invalid bus generator must be silent");
    Expect(ProbeFrame(0, 0, 2.0) == std::array<std::int16_t, 2>{0, 0},
           "invalid amplitude generator must be silent");
    Expect(ProbeFrame(0, 0) == ProbeFrame(0, kProbeRate),
           "generator phase must wrap after one second");
}

void TestSampleIntegrity() {
    auto clipped = MakeWindow(0);
    clipped[0] = 1.0f;
    const auto clipping = AnalyzeSignalWindow(clipped.data(), kProbeRate, 0);
    Expect(!clipping.passed(), "clipped samples must fail the signal window");
    Expect(!clipping.sample_integrity_ok, "clipping must be an integrity failure");
    Expect(clipping.clipped_samples == 1, "clipped sample count must be exact");
    Expect(clipping.peak_absolute_sample == 1.0, "peak sample must be reported");

    auto invalid = MakeWindow(0);
    invalid[1] = std::numeric_limits<float>::quiet_NaN();
    const auto nonFinite = AnalyzeSignalWindow(invalid.data(), kProbeRate, 0);
    Expect(!nonFinite.passed(), "non-finite samples must fail the signal window");
    Expect(!nonFinite.sample_integrity_ok, "non-finite data must be reported");
    Expect(nonFinite.non_finite_samples == 1, "non-finite count must be exact");

    auto multipleInvalid = MakeWindow(3);
    multipleInvalid[0] = std::numeric_limits<float>::quiet_NaN();
    multipleInvalid[1] = std::numeric_limits<float>::infinity();
    multipleInvalid[2] = -1.0f;
    multipleInvalid[3] = 0.999f;
    const auto invalidSummary = AnalyzeSignalWindow(multipleInvalid.data(), kProbeRate, 3);
    Expect(invalidSummary.non_finite_samples == 2,
           "all non-finite channel samples must be counted");
    Expect(invalidSummary.clipped_samples == 2,
           "positive and negative clipping boundaries must be counted");
    Expect(!invalidSummary.sample_integrity_ok,
           "any non-finite or clipped sample must fail integrity");
}

void TestSignalThresholdBoundaries() {
    auto clean = MakeWindow(1);
    const auto baseline = AnalyzeSignalWindow(clean.data(), kProbeRate, 1);
    Expect(baseline.passed(), "reference signal must pass before threshold checks");

    auto atClipBoundary = clean;
    atClipBoundary[0] = 0.999f;
    const auto boundary = AnalyzeSignalWindow(atClipBoundary.data(), kProbeRate, 1);
    Expect(!boundary.sample_integrity_ok,
           "the documented inclusive clipping boundary must fail integrity");
    Expect(boundary.clipped_samples == 1,
           "the inclusive clipping boundary must count exactly once");

    auto justBelowBoundary = clean;
    justBelowBoundary[0] = std::nextafter(0.999f, 0.0f);
    const auto below = AnalyzeSignalWindow(justBelowBoundary.data(), kProbeRate, 1);
    Expect(below.sample_integrity_ok,
           "a finite sample immediately below clipping boundary must remain valid");

    const auto strictThreshold = AnalyzeSignalWindow(
        clean.data(), kProbeRate, 1, baseline.expected_power[0] * 1.01);
    Expect(!strictThreshold.enough_signal,
           "a threshold above measured channel power must reject the signal");
    Expect(strictThreshold.expected_power[1] == baseline.expected_power[1],
           "threshold rejection must preserve measured channel diagnostics");
}

void TestSpectralAnalysisDoesNotProveTimelineContinuity() {
    auto captured = MakeWindow(0);
    constexpr unsigned firstDroppedFrame = 12000;
    constexpr unsigned droppedFrames = 4800;
    for (unsigned frame = firstDroppedFrame;
         frame + droppedFrames < kProbeRate; ++frame) {
        captured[frame * 2] = captured[(frame + droppedFrames) * 2];
        captured[frame * 2 + 1] = captured[(frame + droppedFrames) * 2 + 1];
    }
    const auto result = AnalyzeSignalWindow(captured.data(), kProbeRate, 0);
    Expect(result.passed(),
           "a spectral content pass must not be mistaken for a continuity pass");
    sar_driver::CaptureTimelineStats timeline{};
    timeline.position_gap_packets = 1;
    timeline.position_gap_frames = droppedFrames;
    const auto gapFailures = sar_driver::CaptureContinuityFailures(
        timeline, kProbeRate, kProbeRate, kProbeRate);
    Expect(!sar_driver::CaptureContinuityPassed(timeline, kProbeRate, kProbeRate,
                                                kProbeRate) &&
               sar_driver::HasCaptureContinuityFailure(
                   gapFailures, sar_driver::CaptureContinuityFailure::position_gap),
           "a measured device-position gap must fail strict continuity despite valid tones");
    timeline = {};
    timeline.silent_packets = 1;
    const auto silentFailures = sar_driver::CaptureContinuityFailures(
        timeline, kProbeRate, kProbeRate, kProbeRate);
    Expect(!sar_driver::CaptureContinuityPassed(timeline, kProbeRate, kProbeRate,
                                                kProbeRate) &&
               sar_driver::HasCaptureContinuityFailure(
                   silentFailures, sar_driver::CaptureContinuityFailure::silent_packet),
           "a measured silent packet must fail strict continuity even if the full-window spectrum passes");
    timeline = {};
    timeline.qpc_regressions = 1;
    const auto qpcFailures = sar_driver::CaptureContinuityFailures(
        timeline, kProbeRate, kProbeRate, kProbeRate);
    Expect(!sar_driver::CaptureContinuityPassed(timeline, kProbeRate, kProbeRate,
                                                kProbeRate) &&
               sar_driver::HasCaptureContinuityFailure(
                   qpcFailures, sar_driver::CaptureContinuityFailure::qpc_regression),
           "a QPC regression must fail strict continuity even if the full-window spectrum passes");
}

void TestLevelMonitorTracksCrossWindowSpread() {
    sar_driver::SignalLevelMonitor monitor;
    Expect(!monitor.WithinSpread(6.0), "empty level monitor cannot pass a limit");
    monitor.Observe({0.04, 0.04});
    monitor.Observe({0.02, 0.02});
    Expect(monitor.stats().valid_windows == 2 &&
               monitor.stats().invalid_windows == 0 &&
               monitor.stats().observed_windows == 2,
           "valid windows must contribute to the level summary");
    Expect(monitor.stats().minimum_window[0] == 2 &&
               monitor.stats().maximum_window[0] == 1,
           "automatic window indices must identify the low and high level observations");
    Expect(monitor.WithinSpread(3.1), "a 3 dB power spread must pass a 3.1 dB limit");
    Expect(!monitor.WithinSpread(2.9), "level threshold must reject an undersized limit");
    const double spread = monitor.stats().spread_db(0);
    Expect(std::abs(spread - 10.0 * std::log10(2.0)) < 1e-9,
           "level spread must be expressed as a power ratio in dB");
    Expect(monitor.WithinSpread(spread),
           "a threshold exactly equal to the measured spread must pass");
    Expect(std::abs(monitor.stats().minimum_dbfs(1) - 10.0 * std::log10(0.02)) < 1e-9,
           "minimum level must be reported relative to full scale");
}

void TestSingleChannelCaptureSagFailsLevelGate() {
    const auto stableSamples = MakeWindow(0);
    const auto stable = AnalyzeSignalWindow(stableSamples.data(), kProbeRate, 0);
    auto saggedSamples = MakeWindow(0);
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        saggedSamples[frame * 2 + 1] *= 0.1f;
    }
    const auto sagged = AnalyzeSignalWindow(saggedSamples.data(), kProbeRate, 0);
    Expect(stable.passed() && sagged.passed(),
           "a low but present tone remains a valid content window");
    Expect(sagged.expected_power[1] > 1e-5,
           "the attenuated right channel remains above the content floor");

    sar_driver::SignalLevelMonitor monitor;
    monitor.Observe(stable.expected_power, 10);
    monitor.Observe(sagged.expected_power, 22);
    Expect(monitor.stats().valid_windows == 2 && monitor.stats().invalid_windows == 0,
           "both stable and attenuated windows contribute to the level gate");
    Expect(monitor.stats().spread_db(0) < 0.01,
           "an isolated right-channel sag must not affect the left-channel range");
    Expect(monitor.stats().minimum_window[0] == 10 &&
               monitor.stats().minimum_window[1] == 22,
           "minimum-level window must be tracked independently for each channel");
    Expect(monitor.stats().spread_db(1) > 19.0 && !monitor.WithinSpread(1.0),
           "a single-channel 20 dB capture sag must fail a 1 dB stability gate");
}

void TestSubsecondSagInsidePassingSpectralWindow() {
    auto samples = MakeWindow(1);
    constexpr unsigned sagStart = kProbeRate / 10;
    constexpr unsigned sagEnd = sagStart * 2;
    for (unsigned frame = sagStart; frame < sagEnd; ++frame) {
        samples[frame * 2] *= 0.1f;
    }
    const auto fullSecond = AnalyzeSignalWindow(samples.data(), kProbeRate, 1);
    Expect(fullSecond.passed(),
           "a brief single-channel sag may still pass full-second tone classification");

    sar_driver::SignalRmsWindowAnalyzer<kProbeRate / 10> shortWindows;
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        shortWindows.AppendFrame(samples[frame * 2], samples[frame * 2 + 1]);
    }
    const auto& stats = shortWindows.stats();
    Expect(stats.valid_windows == 10 && stats.invalid_windows == 0,
           "one second of capture must produce ten valid 100 ms RMS windows");
    Expect(stats.minimum_window[0] == 2,
           "subsecond RMS windows must localize the attenuated left interval");
    Expect(stats.spread_db(0) > 3.0 && stats.spread_db(1) < 0.1,
           "100 ms RMS range must expose a short left-channel sag without implicating right");
    Expect(!shortWindows.WithinSpread(1.0),
           "strict level stability must fail when an otherwise valid short window sags");
}

void TestPhaseStepDoesNotMasqueradeAsLevelSag() {
    auto stable = MakeWindow(1);
    auto phaseStepped = stable;
    constexpr unsigned phaseStepFrame = kProbeRate / 2;
    constexpr double phaseStep = 1.2;
    constexpr unsigned frequency = sar_driver::kProbeFrequencies[1][0];
    for (unsigned frame = phaseStepFrame; frame < kProbeRate; ++frame) {
        const double phase = 2.0 * sar_driver::kProbePi * frequency * frame /
                                 kProbeRate +
                             phaseStep;
        phaseStepped[frame * 2] = static_cast<float>(std::sin(phase) * 0.28);
    }

    const auto stableSignal = AnalyzeSignalWindow(stable.data(), kProbeRate, 1);
    const auto steppedSignal = AnalyzeSignalWindow(phaseStepped.data(), kProbeRate, 1);
    Expect(stableSignal.passed() && steppedSignal.passed(),
           "a phase step must preserve valid bus content and channel isolation");

    sar_driver::SignalLevelMonitor toneLevels;
    toneLevels.Observe(stableSignal.expected_power, 1);
    toneLevels.Observe(steppedSignal.expected_power, 2);
    Expect(toneLevels.stats().spread_db(0) > 1.0 && !toneLevels.WithinSpread(1.0),
           "single-bin tone power must retain phase-sensitive diagnostic evidence");

    sar_driver::SignalRmsWindowAnalyzer<kProbeRate / 10> shortLevels;
    for (unsigned frame = 0; frame < kProbeRate; ++frame) {
        shortLevels.AppendFrame(phaseStepped[frame * 2], phaseStepped[frame * 2 + 1]);
    }
    Expect(shortLevels.stats().valid_windows == 10 &&
               shortLevels.stats().spread_db(0) < 0.1 &&
               shortLevels.stats().spread_db(1) < 0.1 &&
               shortLevels.WithinSpread(1.0),
           "constant-amplitude phase steps must pass the short-window RMS level gate");
}

void TestLevelWindowIndicesIgnoreInvalidWindowsAndKeepFirstTie() {
    sar_driver::SignalLevelMonitor monitor;
    monitor.Observe({0.04, 0.03}, 4);
    monitor.Observe({0.02, 0.03}, 8);
    monitor.Observe({0.02, 0.01}, 9);
    monitor.Observe({0.0, 0.04}, 10);
    const auto& stats = monitor.stats();
    Expect(stats.observed_windows == 4 && stats.valid_windows == 3 &&
               stats.invalid_windows == 1,
           "observed, valid, and invalid level-window counts must remain distinct");
    Expect(stats.minimum_window[0] == 8 && stats.maximum_window[0] == 4,
           "equal minimum values must retain the first window index");
    Expect(stats.minimum_window[1] == 9 && stats.maximum_window[1] == 4,
           "invalid windows must not replace valid per-channel extrema");
    Expect(std::isinf(sar_driver::SignalLevelStats::power_dbfs(0.0)) &&
               std::isinf(sar_driver::SignalLevelStats::power_dbfs(
                   std::numeric_limits<double>::quiet_NaN())),
           "zero or non-finite window power must map to negative infinity dBFS");
}

void TestInvalidLevelWindowDoesNotPoisonValidRange() {
    sar_driver::SignalLevelMonitor monitor;
    monitor.Observe({0.04, 0.04});
    monitor.Observe({0.0, 0.03});
    monitor.Observe({std::numeric_limits<double>::infinity(), 0.04});
    monitor.Observe({0.02, 0.02});
    Expect(monitor.stats().valid_windows == 2 &&
               monitor.stats().invalid_windows == 2,
           "invalid windows must be counted without changing valid extrema");
    Expect(!monitor.WithinSpread(4.0), "an invalid window must fail the optional level gate");
    Expect(std::abs(monitor.stats().spread_db(0) - 10.0 * std::log10(2.0)) < 1e-9,
           "invalid windows must not widen the valid level range");
    Expect(std::isinf(monitor.stats().spread_db(2)),
           "out-of-range channel queries must fail closed");
    Expect(!monitor.WithinSpread(std::numeric_limits<double>::quiet_NaN()),
           "non-finite level limits must fail closed");
    Expect(!monitor.WithinSpread(0.0) && !monitor.WithinSpread(-1.0) &&
               !monitor.WithinSpread(std::numeric_limits<double>::infinity()),
           "zero, negative, and infinite spread limits must fail closed");
}

} // namespace

int main() {
    TestPairedBuses();
    TestDcOffsetPreservesToneClassification();
    TestProbeToneIsIndependentOfPacketBoundaries();
    TestSilenceAndMissingChannel();
    TestSwappedAndDuplicatedChannels();
    TestCrossBusAndAttenuation();
    TestInvalidInput();
    TestSampleIntegrity();
    TestSignalThresholdBoundaries();
    TestSpectralAnalysisDoesNotProveTimelineContinuity();
    TestLevelMonitorTracksCrossWindowSpread();
    TestSingleChannelCaptureSagFailsLevelGate();
    TestLevelWindowIndicesIgnoreInvalidWindowsAndKeepFirstTie();
    TestInvalidLevelWindowDoesNotPoisonValidRange();
    TestSubsecondSagInsidePassingSpectralWindow();
    TestPhaseStepDoesNotMasqueradeAsLevelSag();
    if (failures != 0) {
        std::fprintf(stderr, "%d signal-analysis checks failed\n", failures);
        return 1;
    }
    std::puts("dual bus signal analysis passed");
    return 0;
}
