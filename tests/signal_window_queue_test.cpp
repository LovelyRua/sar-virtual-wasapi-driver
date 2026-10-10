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

bool test_empty_consume_does_not_invoke_consumer() {
    Queue queue;
    unsigned calls = 0;
    const bool consumed = queue.try_consume_one(
        [&](const float*, std::size_t) { ++calls; });
    return !consumed && calls == 0 && queue.queued_windows() == 0;
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

bool test_multiple_queue_wraps_keep_window_edges_intact() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned cycle = 1; cycle <= 1000; ++cycle) {
        for (unsigned slot = 0; slot < 3; ++slot) {
            window.fill(static_cast<float>(cycle * 3 + slot));
            if (!queue.try_push(window.data(), window.size())) return false;
        }
        for (unsigned slot = 0; slot < 3; ++slot) {
            const float expected = static_cast<float>(cycle * 3 + slot);
            bool intact = false;
            if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                    intact = count == kSamples && samples[0] == expected &&
                             samples[count / 2] == expected &&
                             samples[count - 1] == expected;
                }) || !intact) {
                return false;
            }
        }
    }
    return queue.queued_windows() == 0 && queue.dropped_windows() == 0 &&
           queue.invalid_pushes() == 0;
}

bool test_full_queue_keeps_oldest_windows_and_rejects_newest() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned id = 1; id <= 9; ++id) {
        window.fill(static_cast<float>(id));
        const bool accepted = queue.try_push(window.data(), window.size());
        if (accepted != (id <= 3)) return false;
    }
    if (queue.dropped_windows() != 6 || queue.queued_windows() != 3) return false;
    for (unsigned expected = 1; expected <= 3; ++expected) {
        bool intact = false;
        if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                intact = count == kSamples && samples[0] == expected &&
                         samples[count - 1] == expected;
            }) || !intact) {
            return false;
        }
    }
    return queue.queued_windows() == 0 && queue.dropped_windows() == 6;
}

bool test_partial_drain_retains_remaining_fifo_entries() {
    Queue queue;
    std::array<float, kSamples> window{};
    for (unsigned id = 11; id <= 13; ++id) {
        window.fill(static_cast<float>(id));
        if (!queue.try_push(window.data(), window.size())) return false;
    }
    bool firstIntact = false;
    if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
            firstIntact = count == kSamples && samples[0] == 11.0F &&
                          samples[count - 1] == 11.0F;
        }) || !firstIntact || queue.queued_windows() != 2) return false;
    window.fill(14.0F);
    if (!queue.try_push(window.data(), window.size()) || queue.queued_windows() != 3) {
        return false;
    }
    for (unsigned expected = 12; expected <= 14; ++expected) {
        bool intact = false;
        if (!queue.try_consume_one([&](const float* samples, std::size_t count) {
                intact = count == kSamples && samples[0] == expected &&
                         samples[count - 1] == expected;
            }) || !intact) return false;
    }
    return queue.queued_windows() == 0 && queue.dropped_windows() == 0;
}

bool test_single_producer_consumer_publication() {
    constexpr unsigned kWindowCount = 20000;
    Queue queue;
    std::atomic_bool producer_done{false};
    std::atomic_bool valid{true};
    unsigned failure_reason = 0;
    unsigned failure_expected = 0;
    std::size_t failure_sample = 0;
    unsigned failure_actual = 0;
    std::thread producer([&] {
        std::array<float, kSamples> window{};
        for (unsigned id = 1; id <= kWindowCount; ++id) {
            for (std::size_t sample = 0; sample < kSamples; ++sample) {
                window[sample] = static_cast<float>(id * 100 + sample);
            }
            while (!queue.try_push(window.data(), window.size())) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    for (unsigned expected = 1; expected <= kWindowCount;) {
        bool consumed = queue.try_consume_one(
            [&](const float* samples, std::size_t count) {
                if (count != kSamples) {
                    valid.store(false, std::memory_order_relaxed);
                    failure_reason = 1;
                    failure_expected = expected;
                    return;
                }
                for (std::size_t sample = 0; sample < count; ++sample) {
                    if (samples[sample] != static_cast<float>(expected * 100 + sample)) {
                        valid.store(false, std::memory_order_relaxed);
                        failure_reason = 2;
                        failure_expected = expected;
                        failure_sample = sample;
                        failure_actual = static_cast<unsigned>(samples[sample]);
                        return;
                    }
                }
            });
        if (consumed) {
            ++expected;
        } else if (producer_done.load(std::memory_order_acquire)) {
            valid.store(false, std::memory_order_relaxed);
            failure_reason = 3;
            failure_expected = expected;
            break;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    const bool passed = valid.load(std::memory_order_relaxed) && queue.queued_windows() == 0;
    if (!passed) {
        std::fprintf(stderr,
                     "SPSC stress details: reason=%u expected=%u sample=%zu actual=%u queued=%zu dropped=%llu\n",
                     failure_reason, failure_expected, failure_sample, failure_actual,
                     queue.queued_windows(),
                     static_cast<unsigned long long>(queue.dropped_windows()));
    }
    return passed;
}

}  // namespace

int main() {
    bool (*tests[])() = {
        test_rejects_invalid_windows,
        test_preserves_fifo_order_and_samples,
        test_empty_consume_does_not_invoke_consumer,
        test_full_queue_drops_without_overwriting,
        test_drop_counter_tracks_every_rejected_window,
        test_invalid_push_does_not_corrupt_queued_window,
        test_reuses_slots_after_consumer_releases_them,
        test_full_queue_keeps_oldest_windows_and_rejects_newest,
        test_multiple_queue_wraps_keep_window_edges_intact,
        test_partial_drain_retains_remaining_fifo_entries,
        test_single_producer_consumer_publication};
    const char* names[] = {
        "invalid windows",
        "FIFO order and samples",
        "empty queue does not call consumer",
        "full queue preserves existing data",
        "drop counter accounting",
        "invalid push preserves queued data",
        "slot reuse",
        "full queue preserves oldest windows",
        "repeated queue wraps preserve window edges",
        "partial drain preserves FIFO entries",
        "SPSC publication stress"};
    for (std::size_t index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index]()) {
            std::fprintf(stderr, "FAIL: %s\n", names[index]);
            return 1;
        }
    }
    return 0;
}
