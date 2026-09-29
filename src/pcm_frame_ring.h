#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sar_driver {

// Caller owns storage and serializes access. All operations are allocation-free.
class PcmFrameRing {
public:
    struct WriteResult {
        size_t accepted_frames;
        size_t dropped_frames;
    };

    bool Initialize(uint8_t* storage, size_t capacity_frames, size_t bytes_per_frame) {
        if (storage == nullptr || capacity_frames == 0 || bytes_per_frame == 0 ||
            capacity_frames > SIZE_MAX / bytes_per_frame || capacity_frames > SIZE_MAX / 2) {
            return false;
        }
        storage_ = storage;
        capacity_frames_ = capacity_frames;
        bytes_per_frame_ = bytes_per_frame;
        Reset();
        return true;
    }

    void Reset() {
        read_frame_ = 0;
        write_frame_ = 0;
        queued_frames_ = 0;
        dropped_frames_ = 0;
        silent_frames_ = 0;
    }

    WriteResult Write(const uint8_t* source, size_t frames) {
        if (storage_ == nullptr || (frames != 0 && source == nullptr) ||
            frames > SIZE_MAX / bytes_per_frame_) {
            return {0, 0};
        }
        if (frames == 0) return {0, 0};

        const size_t skipped_input = frames > capacity_frames_ ? frames - capacity_frames_ : 0;
        source += skipped_input * bytes_per_frame_;
        frames -= skipped_input;

        const size_t overflow = frames > capacity_frames_ - queued_frames_
                                    ? frames - (capacity_frames_ - queued_frames_) : 0;
        read_frame_ = (read_frame_ + overflow) % capacity_frames_;
        queued_frames_ -= overflow;
        const size_t dropped = skipped_input + overflow;
        dropped_frames_ += dropped;

        CopyToRing(source, frames);
        queued_frames_ += frames;
        return {frames, dropped};
    }

    // Returns valid frames. Any missing frames are explicitly zeroed.
    size_t Read(uint8_t* destination, size_t frames) {
        if (storage_ == nullptr || (frames != 0 && destination == nullptr) ||
            frames > SIZE_MAX / bytes_per_frame_) {
            return 0;
        }
        if (frames == 0) return 0;

        const size_t valid = frames < queued_frames_ ? frames : queued_frames_;
        CopyFromRing(destination, valid);
        queued_frames_ -= valid;
        const size_t missing = frames - valid;
        ZeroBytes(destination + valid * bytes_per_frame_, missing * bytes_per_frame_);
        silent_frames_ += missing;
        return valid;
    }

    size_t queued_frames() const { return queued_frames_; }
    uint64_t dropped_frames() const { return dropped_frames_; }
    uint64_t silent_frames() const { return silent_frames_; }

private:
    static void CopyBytes(uint8_t* destination, const uint8_t* source, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) destination[i] = source[i];
    }

    static void ZeroBytes(uint8_t* destination, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) destination[i] = 0;
    }

    void CopyToRing(const uint8_t* source, size_t frames) {
        while (frames != 0) {
            const size_t chunk = frames < capacity_frames_ - write_frame_
                                     ? frames : capacity_frames_ - write_frame_;
            CopyBytes(storage_ + write_frame_ * bytes_per_frame_, source,
                      chunk * bytes_per_frame_);
            source += chunk * bytes_per_frame_;
            write_frame_ = (write_frame_ + chunk) % capacity_frames_;
            frames -= chunk;
        }
    }

    void CopyFromRing(uint8_t* destination, size_t frames) {
        while (frames != 0) {
            const size_t chunk = frames < capacity_frames_ - read_frame_
                                     ? frames : capacity_frames_ - read_frame_;
            CopyBytes(destination, storage_ + read_frame_ * bytes_per_frame_,
                      chunk * bytes_per_frame_);
            destination += chunk * bytes_per_frame_;
            read_frame_ = (read_frame_ + chunk) % capacity_frames_;
            frames -= chunk;
        }
    }

    uint8_t* storage_ = nullptr;
    size_t capacity_frames_ = 0;
    size_t bytes_per_frame_ = 0;
    size_t read_frame_ = 0;
    size_t write_frame_ = 0;
    size_t queued_frames_ = 0;
    uint64_t dropped_frames_ = 0;
    uint64_t silent_frames_ = 0;
};

} // namespace sar_driver
