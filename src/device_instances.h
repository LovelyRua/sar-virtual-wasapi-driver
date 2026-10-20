#pragma once

#ifdef _WIN32

#include <windows.h>

#include <cstddef>
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
// Capacity for the experimental fleet tools, not a claim that a full-fleet
// audio soak has passed on hardware.
constexpr std::size_t kMaximumInstances = 4;

constexpr bool can_add_instance(std::size_t current) noexcept {
    return current < kMaximumInstances;
}

bool same_instance_id(const std::wstring& lhs,
                      const std::wstring& rhs) noexcept;
Result list(std::vector<Instance>& instances);
Result add(const std::wstring& inf_path, const std::wstring& label,
           Instance& created);
Result rename(const std::wstring& instance_id, const std::wstring& label);
Result remove(const std::wstring& instance_id);

} // namespace sar::devices

#endif
