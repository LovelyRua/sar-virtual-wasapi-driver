#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace sar_driver {

constexpr unsigned kMinimumProbeSeconds = 5;
constexpr unsigned kMaximumProbeSeconds = 3600;
constexpr std::size_t kMaximumProbeBuses = 4;

struct WasapiProbeOptions {
    unsigned duration_seconds = 0;
    unsigned first_bus = 0;
    std::size_t pair_count = 0;
    std::array<const wchar_t*, kMaximumProbeBuses * 2> endpoint_ids{};
};

bool ParseWasapiProbeOptions(int argc, const wchar_t* const* argv,
                             WasapiProbeOptions& options,
                             std::wstring& error);

}  // namespace sar_driver
