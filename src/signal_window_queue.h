#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sar_driver {

// Single-producer/single-consumer queue for fixed-size interleaved analysis
// windows. The producer is the WASAPI event pump; it never waits for analysis.
template <std::size_t Frames, std::size_t Channels, std::size_t SlotCount>
class SignalWindowQueue {
    static_assert(Frames > 0, "A signal window must contain frames");
    static_assert(Channels > 0, "A signal window must contain channels");
    static_assert(SlotCount > 1, "The queue needs at least two slots");

public:
    static constexpr std::size_t kSamplesPerWindow = Frames * Channels;

    bool try_push(const float* interleaved, std::size_t sample_count) noexcept {
        if (interleaved == nullptr || sample_count != kSamplesPerWindow) {
            invalid_pushes_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const std::uint64_t write = write_sequence_.load(std::memory_order_relaxed);
        const std::uint64_t read = read_sequence_.load(std::memory_order_acquire);
        if (write - read >= SlotCount) {
            dropped_windows_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::memcpy(windows_[write % SlotCount].data(), interleaved,
                    sizeof(float) * kSamplesPerWindow);
        write_sequence_.store(write + 1, std::memory_order_release);
        return true;
    }

    template <typename Consumer>
    bool try_consume_one(Consumer&& consume) {
        const std::uint64_t read = read_sequence_.load(std::memory_order_relaxed);
        const std::uint64_t write = write_sequence_.load(std::memory_order_acquire);
        if (read == write) return false;

        consume(windows_[read % SlotCount].data(), kSamplesPerWindow);
        read_sequence_.store(read + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint64_t dropped_windows() const noexcept {
        return dropped_windows_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t invalid_pushes() const noexcept {
        return invalid_pushes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t queued_windows() const noexcept {
        const std::uint64_t write = write_sequence_.load(std::memory_order_acquire);
        const std::uint64_t read = read_sequence_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

private:
    std::array<std::array<float, kSamplesPerWindow>, SlotCount> windows_{};
    alignas(64) std::atomic<std::uint64_t> write_sequence_{0};
    alignas(64) std::atomic<std::uint64_t> read_sequence_{0};
    std::atomic<std::uint64_t> dropped_windows_{0};
    std::atomic<std::uint64_t> invalid_pushes_{0};
};

}  // namespace sar_driver
