#ifdef _WIN32

#include "src/device_instances.h"

#include <windows.h>

#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage() {
    std::wcerr
        << L"SAR experimental WASAPI instance manager\n"
        << L"  sar_endpoint_manager list\n"
        << L"  sar_endpoint_manager add <signed-driver.inf> <label>\n"
        << L"  sar_endpoint_manager rename <instance-id> <label>\n"
        << L"  sar_endpoint_manager remove <instance-id> --confirm\n"
        << L"  sar_endpoint_manager help\n\n"
        << L"Each instance exposes the driver's fixed two stereo bus pairs.\n"
        << L"This tool does not create arbitrary channel counts or remove\n"
        << L"shared driver packages. It requires elevation for edits.\n";
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
