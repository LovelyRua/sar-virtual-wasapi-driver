#pragma once

#include <windows.h>
#include <avrt.h>

namespace sar_driver {

class ScopedMmcssAudioThread {
public:
    using RegisterFunction = decltype(&AvSetMmThreadCharacteristicsW);
    using RevertFunction = decltype(&AvRevertMmThreadCharacteristics);

    explicit ScopedMmcssAudioThread(
        bool enabled = true,
        RegisterFunction register_thread = &AvSetMmThreadCharacteristicsW,
        RevertFunction revert_thread = &AvRevertMmThreadCharacteristics) noexcept
        : revert_thread_(revert_thread) {
        if (!enabled) return;
        if (register_thread == nullptr || revert_thread_ == nullptr) {
            last_error_ = ERROR_INVALID_FUNCTION;
            return;
        }

        DWORD task_index = 0;
        handle_ = register_thread(L"Pro Audio", &task_index);
        if (handle_ != nullptr) {
            task_name_ = L"Pro Audio";
            return;
        }
        last_error_ = GetLastError();

        task_index = 0;
        handle_ = register_thread(L"Audio", &task_index);
        if (handle_ != nullptr) {
            task_name_ = L"Audio";
            last_error_ = ERROR_SUCCESS;
            return;
        }
        last_error_ = GetLastError();
    }

    ~ScopedMmcssAudioThread() {
        if (handle_ != nullptr && revert_thread_ != nullptr) {
            revert_thread_(handle_);
        }
    }

    ScopedMmcssAudioThread(const ScopedMmcssAudioThread&) = delete;
    ScopedMmcssAudioThread& operator=(const ScopedMmcssAudioThread&) = delete;

    bool registered() const noexcept { return handle_ != nullptr; }
    const wchar_t* task_name() const noexcept { return task_name_; }
    DWORD last_error() const noexcept { return last_error_; }

private:
    RevertFunction revert_thread_ = nullptr;
    HANDLE handle_ = nullptr;
    const wchar_t* task_name_ = nullptr;
    DWORD last_error_ = ERROR_SUCCESS;
};

}  // namespace sar_driver
