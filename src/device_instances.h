#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <vector>

namespace sar::devices {

struct Instance {
    std::wstring id;
    std::wstring label;
    bool started = false;
    bool problem = false;
    unsigned long problem_code = 0;
};

struct Result {
    bool ok = false;
    DWORD error = ERROR_SUCCESS;
    std::wstring message;

    static Result success();
    static Result failure(DWORD error, const wchar_t* operation);
};

// The hardware ID is intentionally fixed. Never accept a caller-selected ID here:
// it would make removal or renaming capable of touching an unrelated audio device.
constexpr wchar_t kHardwareId[] = L"Root\\SystemAudioRoute\\VirtualAudio";
// The current adapter owns bridge state per instance; keep the supported
// count conservative until concurrent multi-instance VM tests pass.
constexpr size_t kMaximumInstances = 2;

Result list(std::vector<Instance>& instances);
Result add(const std::wstring& inf_path, const std::wstring& label,
           Instance& created);
Result rename(const std::wstring& instance_id, const std::wstring& label);
Result remove(const std::wstring& instance_id);

} // namespace sar::devices

#endif
