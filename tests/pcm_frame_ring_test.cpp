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
    CHECK(written.accepted_frames == 3 && written.dropped_frames == 0);
    uint8_t output[12] = {};
    CHECK(ring.Read(output, 2) == 2);
    CHECK(output[0] == 1 && output[1] == 2);
    CHECK(output[2] == 3 && output[3] == 4);

    const uint8_t second[] = {7, 8, 9, 10, 11, 12, 13, 14};
    written = ring.Write(second, 4);
    CHECK(written.accepted_frames == 4 && written.dropped_frames == 1);
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

    ring.Reset();
    CHECK(ring.queued_frames() == 0 && ring.dropped_frames() == 0 &&
          ring.silent_frames() == 0);
    CHECK(ring.Read(output, 1) == 0);
    CHECK(output[0] == 0 && output[1] == 0);
    CHECK(ring.silent_frames() == 1);
    CHECK(ring.Write(nullptr, 1).accepted_frames == 0);
    CHECK(ring.queued_frames() == 0);
    return 0;
}
