#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sar_driver {

// Builds analysis windows only from a single uninterrupted capture segment.
// The caller owns the SPSC queue and signals its consumer after a successful
// completed-window push.
template <typename Queue>
class CaptureSignalWindowAssembler {
public:
    static constexpr std::size_t kChannels = 2;
    static_assert(Queue::kSamplesPerWindow % kChannels == 0,
                  "Signal analysis windows must contain complete stereo frames");
    static constexpr std::size_t kFramesPerWindow =
        Queue::kSamplesPerWindow / kChannels;

    bool AppendFrame(float left, float right, Queue& queue) noexcept {
        const auto offset = pending_frames_ * kChannels;
        pending_[offset] = left;
        pending_[offset + 1] = right;
        ++pending_frames_;
        if (pending_frames_ != kFramesPerWindow) return false;

        pending_frames_ = 0;
        SaturatingIncrement(completed_windows_);
        if (!queue.try_push(pending_.data(), pending_.size())) {
            SaturatingIncrement(rejected_windows_);
            return false;
        }
        return true;
    }

    bool DiscardPartialSegment() noexcept {
        if (pending_frames_ == 0) return false;
        AddSaturated(discarded_segment_frames_, pending_frames_);
        pending_frames_ = 0;
        return true;
    }

    [[nodiscard]] std::size_t pending_frames() const noexcept {
        return pending_frames_;
    }

    [[nodiscard]] std::uint64_t completed_windows() const noexcept {
        return completed_windows_;
    }

    [[nodiscard]] std::uint64_t rejected_windows() const noexcept {
        return rejected_windows_;
    }

    [[nodiscard]] std::uint64_t discarded_segment_frames() const noexcept {
        return discarded_segment_frames_;
    }

private:
    static void SaturatingIncrement(std::uint64_t& value) noexcept {
        if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
    }

    static void AddSaturated(std::uint64_t& target, std::uint64_t amount) noexcept {
        target = amount > std::numeric_limits<std::uint64_t>::max() - target
                     ? std::numeric_limits<std::uint64_t>::max()
                     : target + amount;
    }

    std::array<float, Queue::kSamplesPerWindow> pending_{};
    std::size_t pending_frames_ = 0;
    std::uint64_t completed_windows_ = 0;
    std::uint64_t rejected_windows_ = 0;
    std::uint64_t discarded_segment_frames_ = 0;
};

}  // namespace sar_driver
