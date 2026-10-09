#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sar_driver {

constexpr unsigned kProbeRate = 48000;
constexpr std::size_t kProbeChannels = 2;
constexpr std::size_t kProbeBuses = 2;
constexpr double kProbePi = 3.14159265358979323846;
constexpr std::array<std::array<unsigned, 2>, 2> kProbeFrequencies{{
    {{997, 1501}}, {{2309, 3001}}
}};

struct SignalWindow {
    std::array<double, 2> expected_power{};
    std::array<double, 2> wrong_channel_power{};
    std::array<double, 2> other_bus_power{};
    double peak_absolute_sample = 0.0;
    std::size_t non_finite_samples = 0;
    std::size_t clipped_samples = 0;
    bool enough_signal = false;
    bool channel_order_ok = false;
    bool bus_isolation_ok = false;
    bool sample_integrity_ok = false;

    bool passed() const {
        return enough_signal && channel_order_ok && bus_isolation_ok && sample_integrity_ok;
    }
};

inline double TonePower(const float* interleaved, std::size_t frames,
                        unsigned channel, unsigned frequency) {
    if (interleaved == nullptr || frames == 0 || channel >= kProbeChannels ||
        frequency == 0 || frequency >= kProbeRate / 2) {
        return 0.0;
    }
    const double coefficient = 2.0 * std::cos(2.0 * kProbePi * frequency / kProbeRate);
    double previous = 0.0;
    double previous2 = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double sample = interleaved[frame * kProbeChannels + channel];
        const double current = sample + coefficient * previous - previous2;
        previous2 = previous;
        previous = current;
    }
    const double raw = previous * previous + previous2 * previous2 -
                       coefficient * previous * previous2;
    return raw > 0.0 ? raw / static_cast<double>(frames * frames) : 0.0;
}

inline SignalWindow AnalyzeSignalWindow(const float* interleaved, std::size_t frames,
                                        unsigned bus, double minimum_power = 1e-5,
                                        double isolation_ratio = 100.0) {
    SignalWindow result;
    if (bus >= kProbeBuses || interleaved == nullptr || frames != kProbeRate ||
        minimum_power <= 0.0 || isolation_ratio <= 1.0) {
        return result;
    }
    for (std::size_t sample = 0; sample < frames * kProbeChannels; ++sample) {
        const double value = interleaved[sample];
        if (!std::isfinite(value)) {
            ++result.non_finite_samples;
            continue;
        }
        const double absolute = std::abs(value);
        if (absolute > result.peak_absolute_sample) {
            result.peak_absolute_sample = absolute;
        }
        if (absolute >= 0.999) ++result.clipped_samples;
    }
    for (unsigned channel = 0; channel < kProbeChannels; ++channel) {
        result.expected_power[channel] = TonePower(
            interleaved, frames, channel, kProbeFrequencies[bus][channel]);
        result.wrong_channel_power[channel] = TonePower(
            interleaved, frames, channel, kProbeFrequencies[bus][1 - channel]);
        const unsigned other_bus = 1 - bus;
        for (unsigned other_channel = 0; other_channel < kProbeChannels; ++other_channel) {
            result.other_bus_power[channel] += TonePower(
                interleaved, frames, channel,
                kProbeFrequencies[other_bus][other_channel]);
        }
    }
    result.enough_signal = result.expected_power[0] >= minimum_power &&
                           result.expected_power[1] >= minimum_power;
    result.channel_order_ok = result.expected_power[0] >=
                                  result.wrong_channel_power[0] * isolation_ratio &&
                              result.expected_power[1] >=
                                  result.wrong_channel_power[1] * isolation_ratio;
    result.bus_isolation_ok = result.expected_power[0] >=
                                  result.other_bus_power[0] * isolation_ratio &&
                              result.expected_power[1] >=
                                  result.other_bus_power[1] * isolation_ratio;
    result.sample_integrity_ok = result.non_finite_samples == 0 &&
                                 result.clipped_samples == 0;
    return result;
}

inline std::array<std::int16_t, 2> ProbeFrame(unsigned bus, std::uint64_t frame,
                                               double amplitude = 0.28) {
    std::array<std::int16_t, 2> samples{};
    if (bus >= kProbeBuses || amplitude < 0.0 || amplitude > 1.0) return samples;
    for (unsigned channel = 0; channel < kProbeChannels; ++channel) {
        const double phase = 2.0 * kProbePi * kProbeFrequencies[bus][channel] *
                             (frame % kProbeRate) / kProbeRate;
        samples[channel] = static_cast<std::int16_t>(
            std::sin(phase) * amplitude * 32767.0);
    }
    return samples;
}

} // namespace sar_driver
