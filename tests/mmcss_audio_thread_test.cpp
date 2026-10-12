#include "src/mmcss_audio_thread.h"

#include <cstdint>
#include <cwchar>

namespace {

unsigned register_calls = 0;
unsigned revert_calls = 0;
unsigned successful_call = 0;
HANDLE reverted_handle = nullptr;
const wchar_t* requested_tasks[2]{};

HANDLE WINAPI FakeRegister(LPCWSTR task, LPDWORD task_index) {
    if (register_calls < 2) requested_tasks[register_calls] = task;
    ++register_calls;
    *task_index = register_calls;
    if (register_calls != successful_call) {
        SetLastError(ERROR_INVALID_TASK_NAME);
        return nullptr;
    }
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));
}

BOOL WINAPI FakeRevert(HANDLE handle) {
    ++revert_calls;
    reverted_handle = handle;
    return TRUE;
}

void Reset(unsigned success) {
    register_calls = 0;
    revert_calls = 0;
    successful_call = success;
    reverted_handle = nullptr;
    requested_tasks[0] = nullptr;
    requested_tasks[1] = nullptr;
}

bool test_pro_audio_registration_is_reverted_on_scope_exit() {
    Reset(1);
    {
        sar_driver::ScopedMmcssAudioThread thread(FakeRegister, FakeRevert);
        if (!thread.registered() || std::wcscmp(thread.task_name(), L"Pro Audio") != 0 ||
            thread.last_error() != ERROR_SUCCESS || register_calls != 1 || revert_calls != 0 ||
            std::wcscmp(requested_tasks[0], L"Pro Audio") != 0) return false;
    }
    return revert_calls == 1 &&
           reverted_handle == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));
}

bool test_audio_fallback_restarts_task_index_and_reverts_handle() {
    Reset(2);
    {
        sar_driver::ScopedMmcssAudioThread thread(FakeRegister, FakeRevert);
        if (!thread.registered() || std::wcscmp(thread.task_name(), L"Audio") != 0 ||
            thread.last_error() != ERROR_SUCCESS || register_calls != 2 || revert_calls != 0 ||
            std::wcscmp(requested_tasks[0], L"Pro Audio") != 0 ||
            std::wcscmp(requested_tasks[1], L"Audio") != 0) return false;
    }
    return revert_calls == 1 &&
           reverted_handle == reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x1234));
}

bool test_double_registration_failure_is_visible_and_not_reverted() {
    Reset(0);
    {
        sar_driver::ScopedMmcssAudioThread thread(FakeRegister, FakeRevert);
        if (thread.registered() || thread.task_name() != nullptr ||
            thread.last_error() != ERROR_INVALID_TASK_NAME || register_calls != 2 ||
            revert_calls != 0) return false;
    }
    return revert_calls == 0;
}

bool test_null_registration_function_fails_without_revert() {
    Reset(0);
    {
        sar_driver::ScopedMmcssAudioThread thread(nullptr, FakeRevert);
        if (thread.registered() || thread.last_error() != ERROR_INVALID_FUNCTION ||
            register_calls != 0 || revert_calls != 0) return false;
    }
    return revert_calls == 0;
}

bool test_null_revert_function_prevents_registration() {
    Reset(1);
    sar_driver::ScopedMmcssAudioThread thread(FakeRegister, nullptr);
    return !thread.registered() && thread.last_error() == ERROR_INVALID_FUNCTION &&
           register_calls == 0 && revert_calls == 0;
}

}  // namespace

int main() {
    return test_pro_audio_registration_is_reverted_on_scope_exit() &&
                   test_audio_fallback_restarts_task_index_and_reverts_handle() &&
                   test_double_registration_failure_is_visible_and_not_reverted() &&
                   test_null_registration_function_fails_without_revert() &&
                   test_null_revert_function_prevents_registration()
               ? 0
               : 1;
}
