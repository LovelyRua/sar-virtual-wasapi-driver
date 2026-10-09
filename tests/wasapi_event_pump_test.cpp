#include "src/wasapi_event_pump.h"

#include <cstddef>
#include <vector>

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
    if (!SetEvent(first) || !SetEvent(second)) return 3;
    if (pump.Wait(0, ready) != WAIT_OBJECT_0 || ready.size() != 2 ||
        ready[0] != 0 || ready[1] != 1) {
        return 4;
    }
    if (pump.Wait(0, ready) != WAIT_TIMEOUT || !ready.empty()) return 5;

    if (!SetEvent(second) || pump.Wait(0, ready) != WAIT_OBJECT_0 ||
        ready.size() != 1 || ready[0] != 1) {
        return 6;
    }
    return 0;
}
