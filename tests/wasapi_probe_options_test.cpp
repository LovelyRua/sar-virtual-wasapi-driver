#include "src/wasapi_probe_options.h"

#include <array>
#include <cstddef>
#include <cwchar>
#include <string>

namespace {

bool parse(const wchar_t* const* argv, int argc,
           sar_driver::WasapiProbeOptions& options) {
    std::wstring error;
    return sar_driver::ParseWasapiProbeOptions(argc, argv, options, error);
}

bool test_legacy_two_pair_syntax() {
    const std::array<const wchar_t*, 6> args{{L"probe", L"r0", L"c0", L"r1", L"c1", L"15"}};
    sar_driver::WasapiProbeOptions options;
    return parse(args.data(), static_cast<int>(args.size()), options) &&
           options.duration_seconds == 15 && options.first_bus == 0 &&
           options.pair_count == 2 && options.endpoint_ids[0] == args[1] &&
           options.endpoint_ids[3] == args[4];
}

bool test_legacy_syntax_rejects_extra_arguments() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"r0", L"c0", L"r1", L"c1", L"15", L"extra"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), static_cast<int>(args.size()), options);
}

bool test_duration_bounds_and_overflow() {
    const std::array<const wchar_t*, 6> too_short{{L"probe", L"r0", L"c0", L"r1", L"c1", L"4"}};
    const std::array<const wchar_t*, 6> too_long{{L"probe", L"r0", L"c0", L"r1", L"c1", L"3601"}};
    const std::array<const wchar_t*, 6> overflow{{L"probe", L"r0", L"c0", L"r1", L"c1", L"999999999999999999999"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(too_short.data(), 6, options) &&
           !parse(too_long.data(), 6, options) &&
           !parse(overflow.data(), 6, options);
}

bool test_single_mode_accepts_last_bus() {
    const std::array<const wchar_t*, 6> args{{L"probe", L"--single", L"3", L"r3", L"c3", L"5"}};
    sar_driver::WasapiProbeOptions options;
    return parse(args.data(), 6, options) && options.pair_count == 1 &&
           options.first_bus == 3 && options.endpoint_ids[0] == args[3];
}

bool test_single_mode_rejects_out_of_range_bus() {
    const std::array<const wchar_t*, 6> args{{L"probe", L"--single", L"4", L"r", L"c", L"5"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 6, options);
}

bool test_single_mode_rejects_missing_id() {
    const std::array<const wchar_t*, 6> args{{L"probe", L"--single", L"0", L"r", L"", L"5"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 6, options);
}

bool test_multi_mode_accepts_two_pairs() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"--multi", L"10", L"r0", L"c0", L"r1", L"c1"}};
    sar_driver::WasapiProbeOptions options;
    return parse(args.data(), 7, options) && options.duration_seconds == 10 &&
           options.pair_count == 2 && options.first_bus == 0;
}

bool test_multi_mode_accepts_four_pairs() {
    const std::array<const wchar_t*, 11> args{{L"probe", L"--multi", L"60", L"r0", L"c0", L"r1", L"c1", L"r2", L"c2", L"r3", L"c3"}};
    sar_driver::WasapiProbeOptions options;
    return parse(args.data(), 11, options) && options.pair_count == 4 &&
           options.endpoint_ids[6] == args[9] && options.endpoint_ids[7] == args[10];
}

bool test_multi_mode_rejects_odd_endpoint_count() {
    const std::array<const wchar_t*, 8> args{{L"probe", L"--multi", L"10", L"r0", L"c0", L"r1", L"c1", L"extra"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 8, options);
}

bool test_multi_mode_rejects_one_or_five_pairs() {
    const std::array<const wchar_t*, 5> one{{L"probe", L"--multi", L"10", L"r0", L"c0"}};
    const std::array<const wchar_t*, 13> five{{L"probe", L"--multi", L"10", L"r0", L"c0", L"r1", L"c1", L"r2", L"c2", L"r3", L"c3", L"r4", L"c4"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(one.data(), 5, options) && !parse(five.data(), 13, options);
}

bool test_null_argument_array_is_rejected() {
    sar_driver::WasapiProbeOptions options;
    std::wstring error;
    return !sar_driver::ParseWasapiProbeOptions(0, nullptr, options, error) &&
           !error.empty();
}

}  // namespace

int main() {
    return test_legacy_two_pair_syntax() &&
                   test_legacy_syntax_rejects_extra_arguments() &&
                   test_duration_bounds_and_overflow() &&
                   test_single_mode_accepts_last_bus() &&
                   test_single_mode_rejects_out_of_range_bus() &&
                   test_single_mode_rejects_missing_id() &&
                   test_multi_mode_accepts_two_pairs() &&
                   test_multi_mode_accepts_four_pairs() &&
                   test_multi_mode_rejects_odd_endpoint_count() &&
                   test_multi_mode_rejects_one_or_five_pairs() &&
                   test_null_argument_array_is_rejected()
               ? 0
               : 1;
}
