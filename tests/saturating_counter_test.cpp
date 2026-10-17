#include "src/saturating_counter.h"

#include <cstdint>
#include <limits>

int main() {
    std::uint64_t count = 0;
    sar_driver::SaturatingIncrement(count);
    if (count != 1) return 1;

    count = std::numeric_limits<std::uint64_t>::max() - 1;
    sar_driver::SaturatingIncrement(count);
    sar_driver::SaturatingIncrement(count);
    if (count != std::numeric_limits<std::uint64_t>::max()) return 2;

    std::uint32_t packets = std::numeric_limits<std::uint32_t>::max() - 2;
    sar_driver::SaturatingAccumulate(packets, std::uint32_t{1});
    if (packets != std::numeric_limits<std::uint32_t>::max() - 1) return 3;
    sar_driver::SaturatingAccumulate(packets, std::uint32_t{8});
    if (packets != std::numeric_limits<std::uint32_t>::max()) return 4;
    sar_driver::SaturatingAccumulate(packets, std::uint32_t{0});
    return packets == std::numeric_limits<std::uint32_t>::max() ? 0 : 5;
}
