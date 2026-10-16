#pragma once

#ifdef _WIN32

#include "device_instances.h"

#include <string>
#include <vector>

namespace sar::devices {

enum class InstanceRemovalState {
    device_present,
    endpoints_remaining,
    endpoint_ownership_unavailable,
    roster_changed,
    stability_not_confirmed,
    error,
    removed
};

struct InstanceRemovalReadiness {
    std::wstring instance_id;
    std::vector<std::wstring> expected_roster;
    std::vector<std::wstring> observed_roster;
    size_t initial_endpoint_count = 0;
    size_t remaining_endpoint_count = 0;
    size_t unassociated_endpoint_count = 0;
    unsigned stable_samples = 0;
    InstanceRemovalState state = InstanceRemovalState::error;
    HRESULT inventory_error = S_OK;

    bool removed() const noexcept { return state == InstanceRemovalState::removed; }
};

// Removes one SAR lab adapter, then proves its PnP instance and known WASAPI
// endpoints are gone while the rest of the adapter roster remains unchanged.
HRESULT remove_and_wait(const std::wstring& instance_id, unsigned timeout_ms,
                        InstanceRemovalReadiness& readiness);
const wchar_t* removal_state_name(InstanceRemovalState state);

} // namespace sar::devices

#endif
