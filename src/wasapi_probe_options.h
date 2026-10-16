#pragma once

#include "signal_level_coverage_policy.h"

#include <array>
#include <cstddef>
#include <string>

namespace sar_driver {

constexpr unsigned kMinimumProbeSeconds = 5;
constexpr unsigned kMaximumProbeSeconds = 3600;
constexpr std::size_t kMaximumProbeBuses = 8;
constexpr std::size_t kMaximumProbeArguments = kMaximumProbeBuses * 2 + 6;

struct WasapiProbeOptions {
    unsigned duration_seconds = 0;
    unsigned first_bus = 0;
    std::size_t pair_count = 0;
    double maximum_level_spread_db = 0.0;
    std::size_t minimum_level_windows = 0;
    bool enable_mmcss = true;
    std::array<const wchar_t*, kMaximumProbeBuses * 2> endpoint_ids{};
};

bool ParseWasapiProbeOptions(int argc, const wchar_t* const* argv,
                             WasapiProbeOptions& options,
                             std::wstring& error);

}  // namespace sar_driver
