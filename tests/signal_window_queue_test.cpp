#include "src/signal_window_queue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

using Queue = sar_driver::SignalWindowQueue<4, 2, 3>;
constexpr std::size_t kSamples = Queue::kSamplesPerWindow;

bool test_rejects_invalid_windows() {
    Queue queue;
    std::array<float, kSamples> samples{};
    if (queue.try_push(nullptr, samples.size())) return false;
    if (queue.try_push(samples.data(), samples.size() - 1)) return false;
    if (queue.invalid_pushes() != 2 || queue.dropped_windows() != 0) return false;
    return queue.queued_windows() == 0;
}

bool test_preserves_fifo_order_and_samples() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned id = 1; id <= 3; ++id) {
        window.fill(static_cast<float>(id));
        if (!queue.try_push(window.data(), window.size())) return false;
    }
    if (queue.queued_windows() != 3) return false;

    for (unsigned expected = 1; expected <= 3; ++expected) {
        bool checked = false;
        const bool consumed = queue.try_consume_one(
            [&](const float* samples, std::size_t count) {
                checked = count == kSamples;
                for (std::size_t i = 0; i < count; ++i) {
                    checked = checked && samples[i] == static_cast<float>(expected);
                }
            });
        if (!consumed || !checked) return false;
    }
    return queue.queued_windows() == 0 && !queue.try_consume_one(
        [](const float*, std::size_t) {});
}

bool test_full_queue_drops_without_overwriting() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned id = 1; id <= 3; ++id) {
        window.fill(static_cast<float>(id));
        if (!queue.try_push(window.data(), window.size())) return false;
    }
    window.fill(99.0F);
    if (queue.try_push(window.data(), window.size())) return false;
    if (queue.dropped_windows() != 1 || queue.queued_windows() != 3) return false;

    for (unsigned expected = 1; expected <= 3; ++expected) {
        bool checked = false;
        if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                checked = count == kSamples && samples[0] == expected &&
                          samples[count - 1] == expected;
            }) || !checked) {
            return false;
        }
    }
    return true;
}

bool test_drop_counter_tracks_every_rejected_window() {
    Queue queue;
    std::array<float, kSamples> window{};
    unsigned accepted = 0;
    for (unsigned id = 1; id <= 12; ++id) {
        window.fill(static_cast<float>(id));
        accepted += queue.try_push(window.data(), window.size()) ? 1u : 0u;
    }
    if (accepted != 3 || queue.dropped_windows() != 9 ||
        queue.queued_windows() != 3) {
        return false;
    }
    for (unsigned expected = 1; expected <= 3; ++expected) {
        if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                if (count != kSamples || samples[0] != expected ||
                    samples[count - 1] != expected) {
                    accepted = 0;
                }
            }) || accepted == 0) {
            return false;
        }
    }
    return queue.queued_windows() == 0 && queue.dropped_windows() == 9;
}

bool test_invalid_push_does_not_corrupt_queued_window() {
    Queue queue;
    std::array<float, kSamples> window{};
    window.fill(42.0F);
    if (!queue.try_push(window.data(), window.size())) return false;
    if (queue.try_push(nullptr, window.size()) ||
        queue.try_push(window.data(), window.size() - 1)) {
        return false;
    }
    bool intact = false;
    return queue.try_consume_one([&](const float* samples, std::size_t count) {
               intact = count == kSamples && samples[0] == 42.0F &&
                        samples[count - 1] == 42.0F;
           }) && intact && queue.invalid_pushes() == 2 &&
           queue.dropped_windows() == 0 && queue.queued_windows() == 0;
}

bool test_reuses_slots_after_consumer_releases_them() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned id = 0; id < 2000; ++id) {
        window.fill(static_cast<float>(id));
        if (!queue.try_push(window.data(), window.size())) return false;
        bool checked = false;
        if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                checked = count == kSamples && samples[0] == id &&
                          samples[count - 1] == id;
            }) || !checked) {
            return false;
        }
    }
    return queue.queued_windows() == 0 && queue.dropped_windows() == 0;
}

bool test_single_producer_consumer_publication() {
    constexpr unsigned kWindowCount = 20000;
    Queue queue;
    std::atomic_bool producer_done{false};
    std::atomic_bool valid{true};
    std::thread producer([&] {
        std::array<float, kSamples> window{};
        for (unsigned id = 1; id <= kWindowCount; ++id) {
            window.fill(static_cast<float>(id));
            while (!queue.try_push(window.data(), window.size())) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    for (unsigned expected = 1; expected <= kWindowCount;) {
        bool consumed = queue.try_consume_one(
            [&](const float* samples, std::size_t count) {
                if (count != kSamples ||
                    samples[0] != static_cast<float>(expected) ||
                    samples[count - 1] != static_cast<float>(expected)) {
                    valid.store(false, std::memory_order_relaxed);
                }
            });
        if (consumed) {
            ++expected;
        } else if (producer_done.load(std::memory_order_acquire)) {
            valid.store(false, std::memory_order_relaxed);
            break;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    return valid.load(std::memory_order_relaxed) && queue.queued_windows() == 0;
}

}  // namespace

int main() {
    const bool (*tests[])() = {
        test_rejects_invalid_windows,
        test_preserves_fifo_order_and_samples,
        test_full_queue_drops_without_overwriting,
        test_drop_counter_tracks_every_rejected_window,
        test_invalid_push_does_not_corrupt_queued_window,
        test_reuses_slots_after_consumer_releases_them,
        test_single_producer_consumer_publication};
    const char* names[] = {
        "invalid windows",
        "FIFO order and samples",
        "full queue preserves existing data",
        "drop counter accounting",
        "invalid push preserves queued data",
        "slot reuse",
        "SPSC publication stress"};
    for (std::size_t index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index]()) {
            std::fprintf(stderr, "FAIL: %s\n", names[index]);
            return 1;
        }
    }
    return 0;
}
