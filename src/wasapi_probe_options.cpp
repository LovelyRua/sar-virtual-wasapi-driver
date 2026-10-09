#include "src/wasapi_probe_options.h"

#include <cerrno>
#include <cwchar>

namespace sar_driver {
namespace {

bool parse_unsigned(const wchar_t* text, unsigned minimum, unsigned maximum,
                    unsigned& value) {
    if (text == nullptr || *text == L'\0') return false;
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != L'\0' ||
        parsed < minimum || parsed > maximum) {
        return false;
    }
    value = static_cast<unsigned>(parsed);
    return true;
}

bool valid_id_pair(const wchar_t* render, const wchar_t* capture) {
    return render != nullptr && *render != L'\0' && capture != nullptr &&
           *capture != L'\0';
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
        options.endpoint_ids[pair * 2] = render;
        options.endpoint_ids[pair * 2 + 1] = capture;
    }
    options.pair_count = pair_count;
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

    if (std::wcscmp(argv[1], L"--single") == 0) {
        unsigned bus = 0;
        if (argc != 6 || !parse_unsigned(argv[2], 0,
                                         static_cast<unsigned>(kMaximumProbeBuses - 1),
                                         bus)) {
            error = L"Single mode requires a bus index from 0 to 3.";
            return false;
        }
        if (!set_duration(argv[5], options, error) ||
            !set_pairs(argv + 3, 1, options, error)) {
            return false;
        }
        options.first_bus = bus;
        return true;
    }

    if (std::wcscmp(argv[1], L"--multi") == 0) {
        if (argc < 7 || argc > 11 || (argc - 3) % 2 != 0) {
            error = L"Multi mode requires two to four render/capture pairs.";
            return false;
        }
        const std::size_t pair_count = static_cast<std::size_t>((argc - 3) / 2);
        if (!set_duration(argv[2], options, error) ||
            !set_pairs(argv + 3, pair_count, options, error)) {
            return false;
        }
        return true;
    }

    if (argc != 6) {
        error = L"Legacy mode requires exactly two endpoint pairs and a duration.";
        return false;
    }
    if (!set_duration(argv[5], options, error) ||
        !set_pairs(argv + 1, 2, options, error)) {
        return false;
    }
    return true;
}

}  // namespace sar_driver
