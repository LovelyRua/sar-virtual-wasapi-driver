#include "src/wasapi_probe_options.h"

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <utility>

namespace sar_driver {
namespace {

bool parse_unsigned(const wchar_t* text, unsigned minimum, unsigned maximum,
                    unsigned& value) {
    if (text == nullptr || *text == L'\0') return false;
    unsigned parsed = 0;
    for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor) {
        if (*cursor < L'0' || *cursor > L'9') return false;
        const unsigned digit = static_cast<unsigned>(*cursor - L'0');
        if (digit > maximum || parsed > (maximum - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool valid_id_pair(const wchar_t* render, const wchar_t* capture) {
    return render != nullptr && *render != L'\0' && capture != nullptr &&
           *capture != L'\0';
}

bool same_endpoint_id(const wchar_t* left, const wchar_t* right) {
    if (left == nullptr || right == nullptr) return false;
    while (*left != L'\0' && *right != L'\0') {
        if (std::towupper(*left) != std::towupper(*right)) return false;
        ++left;
        ++right;
    }
    return *left == L'\0' && *right == L'\0';
}

bool endpoint_is_reused(const wchar_t* candidate,
                       const wchar_t* const* ids,
                       std::size_t endpoint_count) {
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        if (same_endpoint_id(candidate, ids[index])) return true;
    }
    return false;
}

bool set_duration(const wchar_t* text, WasapiProbeOptions& options,
                  std::wstring& error) {
    if (parse_unsigned(text, kMinimumProbeSeconds, kMaximumProbeSeconds,
                       options.duration_seconds)) {
        return true;
    }
    error = L"Duration must be 5..3600 seconds.";
    return false;
}

bool set_pairs(const wchar_t* const* ids, std::size_t pair_count,
               WasapiProbeOptions& options, std::wstring& error) {
    if (pair_count < 1 || pair_count > kMaximumProbeBuses) {
        error = L"Probe requires one to four render/capture pairs.";
        return false;
    }
    for (std::size_t pair = 0; pair < pair_count; ++pair) {
        const auto* render = ids[pair * 2];
        const auto* capture = ids[pair * 2 + 1];
        if (!valid_id_pair(render, capture)) {
            error = L"Every bus requires a render ID and a capture ID.";
            return false;
        }
        if (endpoint_is_reused(render, ids, pair * 2) ||
            endpoint_is_reused(capture, ids, pair * 2 + 1) ||
            same_endpoint_id(render, capture)) {
            error = L"Every probe bus must use unique render and capture endpoint IDs.";
            return false;
        }
    }
    for (std::size_t pair = 0; pair < pair_count; ++pair) {
        const auto* render = ids[pair * 2];
        const auto* capture = ids[pair * 2 + 1];
        options.endpoint_ids[pair * 2] = render;
        options.endpoint_ids[pair * 2 + 1] = capture;
    }
    options.pair_count = pair_count;
    return true;
}

bool parse_level_limit(const wchar_t* text, double& value) {
    if (text == nullptr || *text == L'\0') return false;
    wchar_t* end = nullptr;
    errno = 0;
    value = std::wcstod(text, &end);
    return errno == 0 && end != text && *end == L'\0' && std::isfinite(value) &&
           value >= 0.1 && value <= 60.0;
}

bool strip_optional_arguments(int& argc, const wchar_t* const* argv,
                              WasapiProbeOptions& options,
                              std::wstring& error) {
    bool saw_level_limit = false;
    bool saw_mmcss_toggle = false;
    while (argc > 1) {
        const wchar_t* tail = argv[argc - 1];
        if (tail == nullptr) break;
        if (std::wcscmp(tail, L"--no-mmcss") == 0) {
            if (saw_mmcss_toggle) {
                error = L"--no-mmcss may only be specified once.";
                return false;
            }
            saw_mmcss_toggle = true;
            options.enable_mmcss = false;
            --argc;
            continue;
        }
        if (std::wcscmp(tail, L"--max-level-spread-db") == 0) {
            error = L"Level spread limit must be a number from 0.1 to 60 dB.";
            return false;
        }
        if (argc > 2 && argv[argc - 2] != nullptr &&
            std::wcscmp(argv[argc - 2], L"--max-level-spread-db") == 0) {
            if (saw_level_limit) {
                error = L"--max-level-spread-db may only be specified once.";
                return false;
            }
            double value = 0.0;
            if (!parse_level_limit(tail, value)) {
                error = L"Level spread limit must be a number from 0.1 to 60 dB.";
                return false;
            }
            saw_level_limit = true;
            options.maximum_level_spread_db = value;
            argc -= 2;
            continue;
        }
        break;
    }
    return true;
}

}  // namespace

bool ParseWasapiProbeOptions(int argc, const wchar_t* const* argv,
                             WasapiProbeOptions& options,
                             std::wstring& error) {
    options = {};
    error.clear();
    if (argv == nullptr || argc < 2 || argv[1] == nullptr) {
        error = L"Missing probe arguments.";
        return false;
    }
    WasapiProbeOptions candidate;
    if (!strip_optional_arguments(argc, argv, candidate, error)) return false;
    if (argc < 2) {
        error = L"Missing probe arguments.";
        return false;
    }

    if (std::wcscmp(argv[1], L"--single") == 0) {
        unsigned bus = 0;
        if (argc != 6 || !parse_unsigned(argv[2], 0,
                                         static_cast<unsigned>(kMaximumProbeBuses - 1),
                                         bus)) {
            error = L"Single mode requires a bus index from 0 to 3.";
            return false;
        }
        if (!set_duration(argv[5], candidate, error) ||
            !set_pairs(argv + 3, 1, candidate, error)) {
            return false;
        }
        candidate.first_bus = bus;
        options = std::move(candidate);
        return true;
    }

    if (std::wcscmp(argv[1], L"--multi") == 0) {
        if (argc < 7 || argc > 11 || (argc - 3) % 2 != 0) {
            error = L"Multi mode requires two to four render/capture pairs.";
            return false;
        }
        const std::size_t pair_count = static_cast<std::size_t>((argc - 3) / 2);
        if (!set_duration(argv[2], candidate, error) ||
            !set_pairs(argv + 3, pair_count, candidate, error)) {
            return false;
        }
        options = std::move(candidate);
        return true;
    }

    if (argc != 6) {
        error = L"Legacy mode requires exactly two endpoint pairs and a duration.";
        return false;
    }
    if (!set_duration(argv[5], candidate, error) ||
        !set_pairs(argv + 1, 2, candidate, error)) {
        return false;
    }
    options = std::move(candidate);
    return true;
}

}  // namespace sar_driver
