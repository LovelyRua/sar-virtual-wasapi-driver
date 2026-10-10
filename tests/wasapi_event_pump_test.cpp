#include "src/wasapi_event_pump.h"

#include <cstddef>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible<sar_driver::WasapiEventPump>::value,
              "event handles must have exactly one owner");
static_assert(!std::is_copy_assignable<sar_driver::WasapiEventPump>::value,
              "event handles must not be copied");

int main() {
    sar_driver::WasapiEventPump pump;
    std::vector<std::size_t> ready;

    SetLastError(ERROR_SUCCESS);
    if (pump.Wait(0, ready) != WAIT_FAILED ||
        GetLastError() != ERROR_INVALID_PARAMETER || !ready.empty()) {
        return 1;
    }

    const HANDLE first = pump.Create();
    const HANDLE second = pump.Create();
    if (first == nullptr || second == nullptr) return 2;
    if (pump.Wait(0, ready) != WAIT_FAILED ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || !ready.empty()) return 18;
    if (!pump.Prepare(ready) || ready.capacity() < pump.size()) return 19;
    if (!SetEvent(first) || !SetEvent(second)) return 3;
    if (pump.Wait(0, ready) != WAIT_OBJECT_0 || ready.size() != 2 ||
        ready[0] != 0 || ready[1] != 1) {
        return 4;
    }
    if (pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 5;

    ready.push_back(99);
    if (pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 16;

    if (!SetEvent(second) || pump.Wait(0, ready) != WAIT_OBJECT_0 ||
        ready.size() != 1 || ready[0] != 1) {
        return 6;
    }

    if (!SetEvent(first) ||
        pump.Wait(0, ready) != WAIT_OBJECT_0 || ready.size() != 1 || ready[0] != 0 ||
        pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) {
        return 17;
    }

    for (unsigned cycle = 0; cycle < 1024; ++cycle) {
        if (!SetEvent(second) || pump.Wait(0, ready) != WAIT_OBJECT_0 ||
            ready.size() != 1 || ready[0] != 1 ||
            pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) {
            return 23;
        }
    }

    // A failed kernel wait must not leak stale readiness from a previous pass.
    sar_driver::WasapiEventPump invalid_handle_pump;
    const HANDLE invalidated = invalid_handle_pump.Create();
    if (invalidated == nullptr || !invalid_handle_pump.Prepare(ready)) return 28;
    if (!CloseHandle(invalidated)) return 29;
    ready.push_back(99);
    SetLastError(ERROR_SUCCESS);
    if (invalid_handle_pump.Wait(0, ready) != WAIT_FAILED || !ready.empty() ||
        GetLastError() != ERROR_INVALID_HANDLE) return 30;

    // All signaled clients must be drained in a single service pass in stable order.
    sar_driver::WasapiEventPump four_bus_pump;
    std::vector<HANDLE> four_bus_events;
    for (unsigned index = 0; index < 8; ++index) {
        const HANDLE event = four_bus_pump.Create();
        if (event == nullptr) return 7;
        four_bus_events.push_back(event);
    }
    if (!four_bus_pump.Prepare(ready) || ready.capacity() < four_bus_pump.size()) return 21;
    for (HANDLE event : four_bus_events) {
        if (!SetEvent(event)) return 8;
    }
    if (four_bus_pump.Wait(0, ready) != WAIT_OBJECT_0 || ready.size() != 8) {
        return 9;
    }
    for (std::size_t index = 0; index < ready.size(); ++index) {
        if (ready[index] != index) return 10;
    }
    if (four_bus_pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 11;

    // A busy pass must report every simultaneously signaled stream exactly once.
    for (HANDLE event : four_bus_events) {
        if (!SetEvent(event)) return 24;
    }
    if (four_bus_pump.Wait(0, ready) != WAIT_OBJECT_0 ||
        ready.size() != four_bus_events.size()) return 25;
    for (std::size_t index = 0; index < ready.size(); ++index) {
        if (ready[index] != index) return 26;
    }
    if (four_bus_pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 27;

    sar_driver::WasapiEventPump maximum_pump;
    for (DWORD index = 0; index < MAXIMUM_WAIT_OBJECTS; ++index) {
        if (maximum_pump.Create() == nullptr) return 12;
    }
    if (!maximum_pump.Prepare(ready) || ready.capacity() < maximum_pump.size()) return 20;
    if (maximum_pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 13;
    SetLastError(ERROR_SUCCESS);
    if (maximum_pump.Create() != nullptr || GetLastError() != ERROR_INVALID_PARAMETER) return 14;
    if (maximum_pump.size() != MAXIMUM_WAIT_OBJECTS ||
        maximum_pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 15;
    return 0;
}
