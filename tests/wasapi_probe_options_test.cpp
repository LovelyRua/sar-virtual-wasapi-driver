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

bool test_multi_mode_rejects_reused_render_endpoint() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"--multi", L"10", L"same-render", L"c0", L"SAME-RENDER", L"c1"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 7, options);
}

bool test_multi_mode_rejects_reused_capture_endpoint() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"--multi", L"10", L"r0", L"same-capture", L"r1", L"same-capture"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 7, options);
}

bool test_multi_mode_rejects_cross_flow_duplicate_endpoint() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"--multi", L"10", L"shared-id", L"c0", L"r1", L"shared-id"}};
    sar_driver::WasapiProbeOptions options;
    return !parse(args.data(), 7, options);
}

bool test_single_mode_allows_independent_endpoint_pair() {
    const std::array<const wchar_t*, 6> args{{L"probe", L"--single", L"1", L"render-id", L"capture-id", L"5"}};
    sar_driver::WasapiProbeOptions options;
    return parse(args.data(), 6, options) && options.pair_count == 1 &&
           std::wcscmp(options.endpoint_ids[0], L"render-id") == 0 &&
           std::wcscmp(options.endpoint_ids[1], L"capture-id") == 0;
}

bool test_invalid_reused_endpoint_does_not_publish_partial_options() {
    const std::array<const wchar_t*, 7> args{{L"probe", L"--multi", L"10", L"r0", L"c0", L"r0", L"c1"}};
    sar_driver::WasapiProbeOptions options;
    options.duration_seconds = 99;
    options.pair_count = 4;
    if (parse(args.data(), 7, options)) return false;
    return options.duration_seconds == 0 && options.pair_count == 0 &&
           options.endpoint_ids[0] == nullptr && options.endpoint_ids[1] == nullptr;
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
                   test_multi_mode_rejects_reused_render_endpoint() &&
                   test_multi_mode_rejects_reused_capture_endpoint() &&
                   test_multi_mode_rejects_cross_flow_duplicate_endpoint() &&
                   test_single_mode_allows_independent_endpoint_pair() &&
                   test_invalid_reused_endpoint_does_not_publish_partial_options() &&
                   test_null_argument_array_is_rejected()
               ? 0
               : 1;
}
