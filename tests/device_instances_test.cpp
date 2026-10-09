#ifdef _WIN32

#include "src/device_instances.h"

#include <windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool test_sar_hardware_id_matches_both_infs() {
    constexpr wchar_t expected[] = L"Root\\SystemAudioRoute\\VirtualAudio";
    if (std::wstring(sar::devices::kHardwareId) != expected) return false;

    const auto contains_hardware_id = [](const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        return contents.find("Root\\SystemAudioRoute\\VirtualAudio") !=
                   std::string::npos &&
               contents.find("Root\\sysvad_ComponentizedAudioSample") ==
                   std::string::npos;
    };
    return contains_hardware_id(
               "audio/sysvad/TabletAudioSample/ComponentizedAudioSample.inx") &&
           contains_hardware_id(
               "audio/sysvad/TabletAudioSample/ComponentizedAudioSampleExtension.inx");
}

} // namespace

int wmain() {
    if (!test_sar_hardware_id_matches_both_infs()) return 11;

    std::vector<sar::devices::Instance> instances;
    auto result = sar::devices::list(instances);
    if (!result.ok) {
        std::wcerr << L"Device inventory failed: " << result.message << L'\n';
        return 1;
    }
    sar::devices::Instance created;
    created.id = L"stale-id";
    created.label = L"stale-label";
    created.started = true;
    result = sar::devices::add(L"does-not-exist.inf", L"", created);
    if (result.ok || result.error != ERROR_INVALID_PARAMETER || !created.id.empty() ||
        !created.label.empty() || created.started || created.problem) return 2;
    created.id = L"stale-id";
    created.label = L"stale-label";
    result = sar::devices::add(L"does-not-exist.inf", L"Valid label", created);
    if (result.ok || result.error != ERROR_FILE_NOT_FOUND || !created.id.empty() ||
        !created.label.empty() || created.started || created.problem) return 3;
    result = sar::devices::rename(L"ROOT\\MEDIA\\0000", L"bad/name");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 4;
    result = sar::devices::rename(L"ROOT\\MEDIA\\9999", L"Valid label");
    if (result.ok || result.error != ERROR_NOT_FOUND) return 7;
    result = sar::devices::rename(L"ROOT\\MEDIA\\0000", L"");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 8;
    result = sar::devices::rename(L"ROOT\\MEDIA\\0000", L" padded ");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 9;
    result = sar::devices::remove(L"SWD\\MMDEVAPI\\UNRELATED");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 5;
    result = sar::devices::remove(L"ROOT\\MEDIA\\000");
    if (result.ok || result.error != ERROR_INVALID_PARAMETER) return 10;
    result = sar::devices::remove(L"ROOT\\MEDIA\\9999");
    if (result.ok || result.error != ERROR_NOT_FOUND) return 6;
    std::wcout << L"Device-instance safety checks passed; found "
               << instances.size() << L" SAR instances\n";
    return 0;
}

#endif
