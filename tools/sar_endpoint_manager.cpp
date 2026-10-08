#ifdef _WIN32

#include "src/device_instances.h"
#include "src/wasapi_inventory.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <iostream>
#include <new>
#include <iomanip>
#include <cwctype>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace {

void usage() {
    std::wcerr
        << L"SAR experimental WASAPI instance manager\n"
        << L"  sar_endpoint_manager list [--json]\n"
        << L"  sar_endpoint_manager add <signed-driver.inf> <label>\n"
        << L"  sar_endpoint_manager rename <instance-id> <label>\n"
        << L"  sar_endpoint_manager remove <instance-id> --confirm\n"
        << L"  sar_endpoint_manager endpoints [all|render|capture] [--json]\n"
        << L"  sar_endpoint_manager summary [--json]\n"
        << L"  sar_endpoint_manager wait-endpoints <all|render|capture> <count> <timeout-ms>\n"
        << L"  sar_endpoint_manager help\n\n"
        << L"The current driver supports one instance with two stereo bus pairs.\n"
        << L"This tool does not create arbitrary channel counts or remove\n"
        << L"shared driver packages. It requires elevation for edits.\n";
}

bool json_string(const std::wstring& value) {
    std::wcout << L'"';
    for (const wchar_t c : value) {
        switch (c) {
        case L'"': std::wcout << L"\\\""; break;
        case L'\\': std::wcout << L"\\\\"; break;
        case L'\b': std::wcout << L"\\b"; break;
        case L'\f': std::wcout << L"\\f"; break;
        case L'\n': std::wcout << L"\\n"; break;
        case L'\r': std::wcout << L"\\r"; break;
        case L'\t': std::wcout << L"\\t"; break;
        default:
            if (c < 0x20) {
                std::wcout << L"\\u" << std::hex << std::setw(4)
                           << std::setfill(L'0') << static_cast<unsigned>(c)
                           << std::dec << std::setfill(L' ');
            } else {
                std::wcout << c;
            }
        }
    }
    std::wcout << L'"';
    return static_cast<bool>(std::wcout);
}

bool flow_matches(const sar::devices::WasapiEndpoint& endpoint,
                  const std::wstring& filter) {
    return filter == L"all" || filter == sar::devices::flow_name(endpoint.flow);
}

void print_endpoint_json(const sar::devices::WasapiEndpoint& endpoint) {
    std::wcout << L"{\"flow\":";
    json_string(sar::devices::flow_name(endpoint.flow));
    std::wcout << L",\"name\":";
    json_string(endpoint.name);
    std::wcout << L",\"id\":";
    json_string(endpoint.id);
    std::wcout << L",\"state\":";
    json_string(sar::devices::state_name(endpoint.state));
    std::wcout << L",\"default\":" << (endpoint.is_default ? L"true" : L"false")
               << L",\"mixFormatAvailable\":"
               << (endpoint.mix_format_available ? L"true" : L"false")
               << L",\"channels\":" << endpoint.channels
               << L",\"sampleRate\":" << endpoint.sample_rate
               << L",\"sampleFormat\":";
    json_string(sar::devices::sample_format_name(endpoint.sample_format));
    std::wcout << L",\"containerBits\":" << endpoint.container_bits
               << L",\"channelMask\":" << endpoint.channel_mask
               << L",\"formatHresult\":" << static_cast<unsigned long>(endpoint.format_error)
               << L'}';
}

void print_endpoint(const sar::devices::WasapiEndpoint& endpoint) {
    std::wcout << (endpoint.is_default ? L"* " : L"  ")
               << sar::devices::flow_name(endpoint.flow) << L" | " << endpoint.name
               << L" | " << sar::devices::state_name(endpoint.state) << L" | ";
    if (endpoint.mix_format_available) {
        std::wcout << endpoint.sample_rate << L" Hz, " << endpoint.channels << L" ch, "
                   << sar::devices::sample_format_name(endpoint.sample_format) << L' '
                   << endpoint.container_bits << L" bit";
    } else {
        std::wcout << L"mix format unavailable (0x" << std::hex
                   << static_cast<unsigned long>(endpoint.format_error) << std::dec << L')';
    }
    std::wcout << L'\n' << L"    " << endpoint.id << L'\n';
}

HRESULT query_endpoints(std::vector<sar::devices::WasapiEndpoint>& endpoints) {
    try {
        return sar::devices::enumerate_wasapi_endpoints(endpoints);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

int endpoints(const std::wstring& filter, bool as_json) {
    if (filter != L"all" && filter != L"render" && filter != L"capture") return 2;
    std::vector<sar::devices::WasapiEndpoint> items;
    const HRESULT result = query_endpoints(items);
    if (FAILED(result)) {
        std::wcerr << L"WASAPI inventory failed: 0x" << std::hex
                   << static_cast<unsigned long>(result) << std::dec << L'\n';
        return 1;
    }
    size_t printed = 0;
    if (as_json) std::wcout << L"[";
    for (const auto& item : items) {
        if (!flow_matches(item, filter)) continue;
        if (as_json) {
            if (printed) std::wcout << L",";
            print_endpoint_json(item);
        } else {
            print_endpoint(item);
        }
        ++printed;
    }
    if (as_json) std::wcout << L"]\n";
    else std::wcout << L"WASAPI endpoints: " << printed << L'\n';
    return std::wcout ? 0 : 1;
}

int summary(bool as_json) {
    std::vector<sar::devices::WasapiEndpoint> items;
    const HRESULT result = query_endpoints(items);
    if (FAILED(result)) {
        std::wcerr << L"WASAPI inventory failed: 0x" << std::hex
                   << static_cast<unsigned long>(result) << std::dec << L'\n';
        return 1;
    }
    size_t render_total = 0;
    size_t render_active = 0;
    size_t capture_total = 0;
    size_t capture_active = 0;
    bool default_render = false;
    bool default_capture = false;
    for (const auto& endpoint : items) {
        if (endpoint.flow == sar::devices::EndpointFlow::render) {
            ++render_total;
            render_active += endpoint.state == DEVICE_STATE_ACTIVE ? 1u : 0u;
            default_render = default_render || endpoint.is_default;
        } else {
            ++capture_total;
            capture_active += endpoint.state == DEVICE_STATE_ACTIVE ? 1u : 0u;
            default_capture = default_capture || endpoint.is_default;
        }
    }
    if (as_json) {
        std::wcout << L"{\"render\":{\"total\":" << render_total
                   << L",\"active\":" << render_active
                   << L",\"hasDefault\":" << (default_render ? L"true" : L"false")
                   << L"},\"capture\":{\"total\":" << capture_total
                   << L",\"active\":" << capture_active
                   << L",\"hasDefault\":" << (default_capture ? L"true" : L"false")
                   << L"}}\n";
    } else {
        std::wcout << L"WASAPI render: " << render_active << L" active / "
                   << render_total << L" total, default "
                   << (default_render ? L"present" : L"missing") << L'\n'
                   << L"WASAPI capture: " << capture_active << L" active / "
                   << capture_total << L" total, default "
                   << (default_capture ? L"present" : L"missing") << L'\n';
    }
    return std::wcout ? 0 : 1;
}

int wait_endpoints(const std::wstring& filter, const std::wstring& count_text,
                   const std::wstring& timeout_text) {
    if (filter != L"all" && filter != L"render" && filter != L"capture") return 2;
    wchar_t* end = nullptr;
    const unsigned long target = std::wcstoul(count_text.c_str(), &end, 10);
    if (!end || *end || target == 0 || target > 1024) return 2;
    const unsigned long timeout = std::wcstoul(timeout_text.c_str(), &end, 10);
    if (!end || *end || timeout == 0 || timeout > 120000) return 2;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout);
    size_t observed = 0;
    do {
        std::vector<sar::devices::WasapiEndpoint> items;
        const HRESULT result = query_endpoints(items);
        if (FAILED(result)) {
            std::wcerr << L"WASAPI inventory failed: 0x" << std::hex
                       << static_cast<unsigned long>(result) << std::dec << L'\n';
            return 1;
        }
        observed = static_cast<size_t>(std::count_if(
            items.begin(), items.end(), [&](const auto& item) {
                return flow_matches(item, filter) && item.state == DEVICE_STATE_ACTIVE;
            }));
        if (observed >= target) {
            std::wcout << L"active WASAPI endpoints ready: " << observed
                       << L" (required " << target << L")\n";
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } while (std::chrono::steady_clock::now() < deadline);
    std::wcerr << L"timeout: active WASAPI endpoints=" << observed
               << L", required=" << target << L'\n';
    return 3;
}

std::wstring windows_error(DWORD code) {
    if (code == ERROR_SUCCESS) return L"No Windows error";
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                         FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                         nullptr, code, 0,
                                         reinterpret_cast<LPWSTR>(&buffer),
                                         0, nullptr);
    if (!length || !buffer) return L"No description available";
    std::wstring result(buffer, length);
    LocalFree(buffer);
    while (!result.empty() &&
           (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' ')) {
        result.pop_back();
    }
    return result;
}

int fail(const sar::devices::Result& result) {
    std::wcerr << L"error: " << result.message << L" (" << result.error << L") "
               << windows_error(result.error) << L'\n';
    if (result.error == ERROR_ACCESS_DENIED || result.error == ERROR_PRIVILEGE_NOT_HELD) {
        std::wcerr << L"Run from an elevated prompt on the dedicated driver lab.\n";
    }
    return 1;
}

bool elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD bytes = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &bytes);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void print_instance(const sar::devices::Instance& instance) {
    std::wcout << instance.id << L'\n'
               << L"  label: " << instance.label << L'\n'
               << L"  state: ";
    if (instance.started) std::wcout << L"started";
    else if (instance.problem) std::wcout << L"problem " << instance.problem_code;
    else std::wcout << L"registered; start pending";
    std::wcout << L'\n';
}

int list() {
    std::vector<sar::devices::Instance> items;
    const auto result = sar::devices::list(items);
    if (!result.ok) return fail(result);
    std::wcout << L"SAR experimental instances: " << items.size() << L" / "
               << sar::devices::kMaximumInstances << L'\n';
    for (const auto& item : items) print_instance(item);
    return 0;
}

int list_json() {
    std::vector<sar::devices::Instance> items;
    const auto result = sar::devices::list(items);
    if (!result.ok) return fail(result);
    std::wcout << L"{\"maximum\":" << sar::devices::kMaximumInstances
               << L",\"instances\":[";
    for (size_t index = 0; index < items.size(); ++index) {
        if (index) std::wcout << L",";
        const auto& item = items[index];
        std::wcout << L"{\"id\":";
        json_string(item.id);
        std::wcout << L",\"label\":";
        json_string(item.label);
        std::wcout << L",\"started\":" << (item.started ? L"true" : L"false")
                   << L",\"problem\":" << (item.problem ? L"true" : L"false")
                   << L",\"problemCode\":" << item.problem_code << L'}';
    }
    std::wcout << L"]}\n";
    return std::wcout ? 0 : 1;
}

int add(const std::wstring& inf_path, const std::wstring& label) {
    sar::devices::Instance created;
    const auto result = sar::devices::add(inf_path, label, created);
    if (!result.ok) return fail(result);
    std::wcout << L"Registered SAR experimental instance:\n";
    print_instance(created);
    if (!created.started) {
        std::wcout << L"Device is not started yet. Refresh with 'list' and inspect"
                   << L" Device Manager before using its audio endpoints.\n";
    }
    return 0;
}

int rename(const std::wstring& id, const std::wstring& label) {
    const auto result = sar::devices::rename(id, label);
    if (!result.ok) return fail(result);
    std::wcout << L"Renamed " << id << L" to " << label << L'\n';
    std::wcout << L"The Windows audio endpoint display names may still reflect"
               << L" the INF; the PnP instance label is changed.\n";
    return 0;
}

int remove(const std::wstring& id) {
    const auto result = sar::devices::remove(id);
    if (!result.ok) return fail(result);
    std::wcout << L"Removed " << id << L'\n'
               << L"The shared signed driver package remains installed.\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wcscmp(argv[1], L"help") == 0) {
        usage();
        return 0;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"list") == 0) return list();
    if (argc == 3 && std::wcscmp(argv[1], L"list") == 0 &&
        std::wcscmp(argv[2], L"--json") == 0) return list_json();
    if (argc == 2 && std::wcscmp(argv[1], L"summary") == 0) return summary(false);
    if (argc == 3 && std::wcscmp(argv[1], L"summary") == 0 &&
        std::wcscmp(argv[2], L"--json") == 0) return summary(true);
    if ((argc >= 2 && argc <= 4) && std::wcscmp(argv[1], L"endpoints") == 0) {
        std::wstring filter = L"all";
        bool as_json = false;
        for (int index = 2; index < argc; ++index) {
            if (std::wcscmp(argv[index], L"--json") == 0) {
                if (as_json) return 2;
                as_json = true;
            } else if (filter == L"all") {
                filter = argv[index];
            } else {
                return 2;
            }
        }
        return endpoints(filter, as_json);
    }
    if (argc == 5 && std::wcscmp(argv[1], L"wait-endpoints") == 0) {
        return wait_endpoints(argv[2], argv[3], argv[4]);
    }

    if (argc == 4 && std::wcscmp(argv[1], L"add") == 0) {
        if (!elevated()) {
            std::wcerr << L"error: an elevated prompt is required for add\n";
            return 1;
        }
        return add(argv[2], argv[3]);
    }
    if (argc == 4 && std::wcscmp(argv[1], L"rename") == 0) {
        if (!elevated()) {
            std::wcerr << L"error: an elevated prompt is required for rename\n";
            return 1;
        }
        return rename(argv[2], argv[3]);
    }
    if (argc == 4 && std::wcscmp(argv[1], L"remove") == 0 &&
        std::wcscmp(argv[3], L"--confirm") == 0) {
        if (!elevated()) {
            std::wcerr << L"error: an elevated prompt is required for remove\n";
            return 1;
        }
        return remove(argv[2]);
    }
    usage();
    return 2;
}

#endif
