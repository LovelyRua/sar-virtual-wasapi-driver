#pragma once

#include <limits>
#include <type_traits>

namespace sar_driver {

template <typename Unsigned>
constexpr void SaturatingIncrement(Unsigned& value) noexcept {
    static_assert(std::is_integral<Unsigned>::value &&
                      std::is_unsigned<Unsigned>::value,
                  "Saturating counters require an unsigned integer");
    if (value != std::numeric_limits<Unsigned>::max()) ++value;
}

template <typename Unsigned>
constexpr void SaturatingAccumulate(Unsigned& value, Unsigned amount) noexcept {
    static_assert(std::is_integral<Unsigned>::value &&
                      std::is_unsigned<Unsigned>::value,
                  "Saturating counters require an unsigned integer");
    const auto maximum = std::numeric_limits<Unsigned>::max();
    value = amount > maximum - value ? maximum : value + amount;
}

}  // namespace sar_driver
