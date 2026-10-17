#pragma once

#include <stddef.h>

namespace sar_driver {

// Caller owns storage and serializes access. All operations are allocation-free.
class PcmFrameRing {
public:
    struct WriteResult {
        size_t accepted_frames;
        size_t dropped_frames;
        size_t queued_frames;
    };

    struct ReadResult {
        size_t valid_frames;
        size_t missing_frames;
        size_t queued_frames;
    };

    struct Stats {
        size_t queued_frames;
        size_t peak_queued_frames;
        unsigned long long dropped_frames;
        unsigned long long silent_frames;
        unsigned long long underrun_reads;
        size_t maximum_missing_frames;
    };

    bool Initialize(unsigned char* storage, size_t capacity_frames, size_t bytes_per_frame) {
        if (storage == nullptr || capacity_frames == 0 || bytes_per_frame == 0 ||
            capacity_frames > static_cast<size_t>(-1) / bytes_per_frame ||
            capacity_frames > static_cast<size_t>(-1) / 2) {
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
        underrun_reads_ = 0;
        maximum_missing_frames_ = 0;
        peak_queued_frames_ = 0;
    }

    WriteResult Write(const unsigned char* source, size_t frames) {
        if (storage_ == nullptr || (frames != 0 && source == nullptr) ||
            frames > static_cast<size_t>(-1) / bytes_per_frame_) {
            return {0, 0, queued_frames_};
        }
        if (frames == 0) return {0, 0, queued_frames_};

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
        if (queued_frames_ > peak_queued_frames_) peak_queued_frames_ = queued_frames_;
        return {frames, dropped, queued_frames_};
    }

    // Returns valid and missing frames. Missing samples are explicitly zeroed.
    ReadResult ReadWithResult(unsigned char* destination, size_t frames) {
        if (storage_ == nullptr || (frames != 0 && destination == nullptr) ||
            frames > static_cast<size_t>(-1) / bytes_per_frame_) {
            return {0, 0, queued_frames_};
        }
        if (frames == 0) return {0, 0, queued_frames_};

        const size_t valid = frames < queued_frames_ ? frames : queued_frames_;
        CopyFromRing(destination, valid);
        queued_frames_ -= valid;
        const size_t missing = frames - valid;
        ZeroBytes(destination + valid * bytes_per_frame_, missing * bytes_per_frame_);
        silent_frames_ += missing;
        if (missing != 0) {
            ++underrun_reads_;
            if (missing > maximum_missing_frames_) maximum_missing_frames_ = missing;
        }
        return {valid, missing, queued_frames_};
    }

    // Compatibility wrapper for callers that only need the valid-frame count.
    size_t Read(unsigned char* destination, size_t frames) {
        return ReadWithResult(destination, frames).valid_frames;
    }

    size_t queued_frames() const { return queued_frames_; }
    size_t peak_queued_frames() const { return peak_queued_frames_; }
    unsigned long long dropped_frames() const { return dropped_frames_; }
    unsigned long long silent_frames() const { return silent_frames_; }
    unsigned long long underrun_reads() const { return underrun_reads_; }
    size_t maximum_missing_frames() const { return maximum_missing_frames_; }

    // The caller must serialize this with all reads and writes.
    Stats TakeStats() {
        const Stats stats{queued_frames_, peak_queued_frames_, dropped_frames_,
                          silent_frames_, underrun_reads_, maximum_missing_frames_};
        Reset();
        return stats;
    }

private:
    static void CopyBytes(unsigned char* destination, const unsigned char* source, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) destination[i] = source[i];
    }

    static void ZeroBytes(unsigned char* destination, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) destination[i] = 0;
    }

    void CopyToRing(const unsigned char* source, size_t frames) {
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

    void CopyFromRing(unsigned char* destination, size_t frames) {
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

    unsigned char* storage_ = nullptr;
    size_t capacity_frames_ = 0;
    size_t bytes_per_frame_ = 0;
    size_t read_frame_ = 0;
    size_t write_frame_ = 0;
    size_t queued_frames_ = 0;
    unsigned long long dropped_frames_ = 0;
    unsigned long long silent_frames_ = 0;
    unsigned long long underrun_reads_ = 0;
    size_t maximum_missing_frames_ = 0;
    size_t peak_queued_frames_ = 0;
};

} // namespace sar_driver
