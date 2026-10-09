#pragma once

#include <windows.h>

#include <cstddef>
#include <new>
#include <vector>

namespace sar_driver {

class WasapiEventPump {
public:
    WasapiEventPump() = default;
    WasapiEventPump(const WasapiEventPump&) = delete;
    WasapiEventPump& operator=(const WasapiEventPump&) = delete;

    ~WasapiEventPump() {
        for (HANDLE event : events_) {
            if (event != nullptr) CloseHandle(event);
        }
    }

    HANDLE Create() {
        if (events_.size() >= MAXIMUM_WAIT_OBJECTS) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) return nullptr;
        try {
            events_.push_back(event);
        } catch (const std::bad_alloc&) {
            CloseHandle(event);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        return event;
    }

    DWORD Wait(DWORD timeout_ms, std::vector<std::size_t>& ready) const {
        ready.clear();
        if (events_.empty() || events_.size() > MAXIMUM_WAIT_OBJECTS) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return WAIT_FAILED;
        }

        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(events_.size()), events_.data(), FALSE, timeout_ms);
        if (result == WAIT_TIMEOUT || result == WAIT_FAILED) return result;
        if (result >= WAIT_OBJECT_0 + events_.size()) {
            return WAIT_FAILED;
        }
        ready.push_back(result - WAIT_OBJECT_0);

        // Drain other already-signaled auto-reset events in the same service pass.
        for (std::size_t index = 0; index < events_.size(); ++index) {
            if (index == ready.front()) continue;
            if (WaitForSingleObject(events_[index], 0) == WAIT_OBJECT_0) {
                ready.push_back(index);
            }
        }
        return WAIT_OBJECT_0;
    }

    std::size_t size() const { return events_.size(); }

private:
    std::vector<HANDLE> events_;
};

} // namespace sar_driver
