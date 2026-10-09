#include "src/pcm_frame_ring.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdint.h>
#include <deque>
#include <random>
#include <vector>

using sar_driver::PcmFrameRing;

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    uint8_t storage[8] = {};
    PcmFrameRing ring;
    CHECK(!ring.Initialize(nullptr, 4, 2));
    CHECK(!ring.Initialize(storage, 0, 2));
    CHECK(!ring.Initialize(storage, 4, 0));
    CHECK(ring.Initialize(storage, 4, 2));

    const size_t maximum = std::numeric_limits<size_t>::max();
    CHECK(!ring.Initialize(storage, maximum / 2 + 1, 2));
    const uint8_t retained[] = {91, 92};
    CHECK(ring.Write(retained, 1).accepted_frames == 1);
    CHECK(!ring.Initialize(nullptr, 4, 2));
    uint8_t retained_output[2] = {};
    CHECK(ring.Read(retained_output, 1) == 1);
    CHECK(retained_output[0] == 91 && retained_output[1] == 92);

    const uint8_t first[] = {1, 2, 3, 4, 5, 6};
    auto written = ring.Write(first, 3);
    CHECK(written.accepted_frames == 3 && written.dropped_frames == 0 &&
          written.queued_frames == 3);
    CHECK(ring.peak_queued_frames() == 3);
    uint8_t output[12] = {};
    CHECK(ring.Read(output, 2) == 2);
    CHECK(output[0] == 1 && output[1] == 2);
    CHECK(output[2] == 3 && output[3] == 4);

    const uint8_t second[] = {7, 8, 9, 10, 11, 12, 13, 14};
    written = ring.Write(second, 4);
    CHECK(written.accepted_frames == 4 && written.dropped_frames == 1 &&
          written.queued_frames == 4);
    CHECK(ring.peak_queued_frames() == 4);
    CHECK(ring.dropped_frames() == 1 && ring.queued_frames() == 4);
    CHECK(ring.Read(output, 4) == 4);
    for (size_t i = 0; i < 8; ++i) CHECK(output[i] == second[i]);

    const uint8_t long_input[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    written = ring.Write(long_input, 6);
    CHECK(written.accepted_frames == 4 && written.dropped_frames == 2);
    CHECK(ring.Read(output, 6) == 4);
    for (size_t i = 0; i < 8; ++i) CHECK(output[i] == long_input[i + 4]);
    for (size_t i = 8; i < 12; ++i) CHECK(output[i] == 0);
    CHECK(ring.silent_frames() == 2);
    CHECK(ring.peak_queued_frames() == 4);

    ring.Reset();
    CHECK(ring.queued_frames() == 0 && ring.dropped_frames() == 0 &&
          ring.silent_frames() == 0 && ring.peak_queued_frames() == 0);
    CHECK(ring.Read(output, 1) == 0);
    CHECK(output[0] == 0 && output[1] == 0);
    CHECK(ring.silent_frames() == 1);
    CHECK(ring.Write(nullptr, 1).accepted_frames == 0);
    CHECK(ring.queued_frames() == 0);
    CHECK(ring.peak_queued_frames() == 0);

    uint8_t bus_storage[2][8] = {};
    PcmFrameRing buses[2];
    CHECK(buses[0].Initialize(bus_storage[0], 4, 2));
    CHECK(buses[1].Initialize(bus_storage[1], 4, 2));
    const uint8_t bus_one_frame[] = {21, 22};
    CHECK(buses[0].Write(first, 1).accepted_frames == 1);
    CHECK(buses[1].Write(bus_one_frame, 1).accepted_frames == 1);
    buses[0].Reset();
    CHECK(buses[0].Read(output, 1) == 0);
    CHECK(output[0] == 0 && output[1] == 0);
    CHECK(buses[1].Read(output, 1) == 1);
    CHECK(output[0] == 21 && output[1] == 22);

    // Empty operations are no-ops, including null pointers with zero frames.
    CHECK(ring.Write(nullptr, 0).accepted_frames == 0);
    CHECK(ring.Read(nullptr, 0) == 0);
    CHECK(ring.queued_frames() == 0);

    // Reject byte-count overflow before touching caller memory.
    const size_t overflow_frames = maximum / 2 + 1;
    written = ring.Write(first, overflow_frames);
    CHECK(written.accepted_frames == 0 && written.dropped_frames == 0);
    CHECK(ring.Read(output, overflow_frames) == 0);
    CHECK(ring.queued_frames() == 0);

    // Repeated wraparound must preserve frame boundaries and newest-data policy.
    ring.Reset();
    const uint8_t cycle_a[] = {31, 32, 33, 34, 35, 36};
    const uint8_t cycle_b[] = {41, 42, 43, 44, 45, 46, 47, 48, 49, 50};
    CHECK(ring.Write(cycle_a, 3).accepted_frames == 3);
    CHECK(ring.Read(output, 2) == 2);
    written = ring.Write(cycle_b, 5);
    CHECK(written.accepted_frames == 4 && written.dropped_frames == 2);
    CHECK(ring.Read(output, 4) == 4);
    CHECK(output[0] == 43 && output[1] == 44);
    CHECK(output[2] == 45 && output[3] == 46);
    CHECK(output[4] == 47 && output[5] == 48);
    CHECK(output[6] == 49 && output[7] == 50);
    CHECK(ring.dropped_frames() == 2 && ring.queued_frames() == 0);

    // Splitting a producer write into packet-sized chunks must preserve the
    // same FIFO stream as one contiguous write when the ring has headroom.
    uint8_t chunked_storage[40] = {};
    uint8_t contiguous_storage[40] = {};
    PcmFrameRing chunked;
    PcmFrameRing contiguous;
    CHECK(chunked.Initialize(chunked_storage, 10, 4));
    CHECK(contiguous.Initialize(contiguous_storage, 10, 4));
    uint8_t packet[24] = {};
    for (size_t frame = 0; frame < 6; ++frame) {
        for (size_t byte = 0; byte < 4; ++byte) {
            packet[frame * 4 + byte] = static_cast<uint8_t>(frame * 7 + byte);
        }
    }
    CHECK(contiguous.Write(packet, 6).accepted_frames == 6);
    CHECK(chunked.Write(packet, 2).accepted_frames == 2);
    CHECK(chunked.Write(packet + 8, 1).accepted_frames == 1);
    CHECK(chunked.Write(packet + 12, 3).accepted_frames == 3);
    uint8_t chunked_output[24] = {};
    uint8_t contiguous_output[24] = {};
    CHECK(chunked.Read(chunked_output, 1) == 1);
    CHECK(chunked.Read(chunked_output + 4, 5) == 5);
    CHECK(contiguous.Read(contiguous_output, 6) == 6);
    for (size_t byte = 0; byte < sizeof(packet); ++byte) {
        CHECK(chunked_output[byte] == packet[byte]);
        CHECK(contiguous_output[byte] == packet[byte]);
    }
    CHECK(chunked.queued_frames() == 0 && contiguous.queued_frames() == 0);

    // Compare long mixed read/write/reset sequences against a simple FIFO model.
    uint8_t model_storage[30] = {};
    PcmFrameRing model_ring;
    CHECK(model_ring.Initialize(model_storage, 10, 3));
    std::deque<std::array<uint8_t, 3>> model;
    std::mt19937 random(0x534152u);
    uint64_t model_dropped = 0;
    uint64_t model_silent = 0;
    for (unsigned operation = 0; operation < 2000; ++operation) {
        if (operation % 97 == 0) {
            model_ring.Reset();
            model.clear();
            model_dropped = 0;
            model_silent = 0;
            continue;
        }
        if ((random() & 1u) == 0) {
            const size_t frames = random() % 16;
            std::vector<uint8_t> input(frames * 3);
            for (size_t frame = 0; frame < frames; ++frame) {
                for (size_t byte = 0; byte < 3; ++byte) {
                    input[frame * 3 + byte] = static_cast<uint8_t>(operation + frame + byte);
                }
            }
            const auto result = model_ring.Write(input.data(), frames);
            const size_t skip = frames > 10 ? frames - 10 : 0;
            const size_t accepted = frames - skip;
            const size_t overflow = accepted > 10 - model.size()
                                        ? accepted - (10 - model.size()) : 0;
            const size_t dropped = skip + overflow;
            for (size_t frame = skip; frame < frames; ++frame) {
                if (model.size() == 10) {
                    model.pop_front();
                    ++model_dropped;
                }
                std::array<uint8_t, 3> item{};
                for (size_t byte = 0; byte < 3; ++byte) {
                    item[byte] = input[frame * 3 + byte];
                }
                model.push_back(item);
            }
            model_dropped += skip;
            CHECK(result.accepted_frames == accepted);
            CHECK(result.dropped_frames == dropped);
            CHECK(result.queued_frames == model.size());
        } else {
            const size_t frames = random() % 16;
            std::vector<uint8_t> actual(frames * 3, 0xA5);
            const size_t expected = frames < model.size() ? frames : model.size();
            CHECK(model_ring.Read(actual.data(), frames) == expected);
            for (size_t frame = 0; frame < expected; ++frame) {
                for (size_t byte = 0; byte < 3; ++byte) {
                    CHECK(actual[frame * 3 + byte] == model.front()[byte]);
                }
                model.pop_front();
            }
            for (size_t byte = expected * 3; byte < actual.size(); ++byte) {
                CHECK(actual[byte] == 0);
            }
            model_silent += frames - expected;
        }
        CHECK(model_ring.queued_frames() == model.size());
        CHECK(model_ring.dropped_frames() == model_dropped);
        CHECK(model_ring.silent_frames() == model_silent);
    }
    return 0;
}
