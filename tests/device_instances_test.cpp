#ifdef _WIN32

#include "src/device_instances.h"

#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

int wmain() {
    std::vector<sar::devices::Instance> instances;
    auto result = sar::devices::list(instances);
    if (!result.ok) {
        std::wcerr << L"Device inventory failed: " << result.message << L'\n';
        return 1;
    }
    sar::devices::Instance created;
    result = sar::devices::add(L"does-not-exist.inf", L"", created);
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 2;
    result = sar::devices::add(L"does-not-exist.inf", L"Valid label", created);
    if (result.ok || result.error != ERROR_FILE_NOT_FOUND) return 3;
    result = sar::devices::rename(L"ROOT\\MEDIA\\0000", L"bad/name");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 4;
    result = sar::devices::remove(L"SWD\\MMDEVAPI\\UNRELATED");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 5;
    result = sar::devices::remove(L"ROOT\\MEDIA\\9999");
    if (result.ok || result.error != ERROR_NOT_FOUND) return 6;
    std::wcout << L"Device-instance safety checks passed; found "
               << instances.size() << L" SAR instances\n";
    return 0;
}

#endif
