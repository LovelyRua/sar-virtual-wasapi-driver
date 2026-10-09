#include "src/pcm_frame_ring.h"

#include <stdint.h>

using sar_driver::PcmFrameRing;

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    uint8_t storage[8] = {};
    PcmFrameRing ring;
    CHECK(!ring.Initialize(nullptr, 4, 2));
    CHECK(!ring.Initialize(storage, 0, 2));
    CHECK(!ring.Initialize(storage, 4, 0));
    CHECK(ring.Initialize(storage, 4, 2));

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
    return 0;
}
