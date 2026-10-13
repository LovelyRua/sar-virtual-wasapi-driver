#pragma once

#ifdef _WIN32

#include "endpoint_readiness.h"

#include <cstddef>
#include <vector>

namespace sar::devices {

enum class FleetReadinessState {
    no_instances,
    not_ready,
    stability_not_confirmed,
    ready
};

struct FleetReadiness {
    std::vector<InstanceReadiness> instances;
    FleetReadinessState state = FleetReadinessState::no_instances;
    unsigned stable_samples = 0;
    HRESULT inventory_error = S_OK;

    std::size_t ready_instances() const noexcept;
    bool ready() const noexcept { return state == FleetReadinessState::ready; }
};

HRESULT inspect_fleet_readiness(FleetReadiness& readiness);
HRESULT wait_for_fleet_readiness(unsigned timeout_ms, FleetReadiness& readiness);
const wchar_t* fleet_readiness_state_name(FleetReadinessState state);

} // namespace sar::devices

#endif
