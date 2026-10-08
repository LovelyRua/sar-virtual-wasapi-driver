#pragma once

#ifdef _WIN32

#include "device_instances.h"
#include "wasapi_inventory.h"

#include <chrono>
#include <string>
#include <vector>

namespace sar::devices {

// The current experimental adapter installs two render/capture bus pairs.
// These counts are intentionally explicit: a started root devnode is not a
// usable audio adapter until all four Windows audio endpoints are published.
constexpr size_t kRenderEndpointsPerInstance = 2;
constexpr size_t kCaptureEndpointsPerInstance = 2;
constexpr unsigned kStableReadinessSamples = 3;
constexpr unsigned kReadinessPollIntervalMs = 250;

enum class ReadinessState {
    device_missing,
    device_stopped,
    endpoint_parent_unavailable,
    endpoints_missing,
    unexpected_endpoint_count,
    endpoints_inactive,
    mix_format_unavailable,
    stability_not_confirmed,
    ready
};

struct EndpointCounts {
    size_t render = 0;
    size_t render_active = 0;
    size_t render_with_format = 0;
    size_t capture = 0;
    size_t capture_active = 0;
    size_t capture_with_format = 0;
};

struct InstanceReadiness {
    Instance instance;
    std::vector<WasapiEndpoint> endpoints;
    EndpointCounts counts;
    size_t unassociated_endpoint_count = 0;
    ReadinessState state = ReadinessState::device_missing;
    HRESULT inventory_error = S_OK;
    unsigned stable_samples = 0;

    bool ready() const noexcept { return state == ReadinessState::ready; }
};

HRESULT inspect_instance_readiness(const std::wstring& instance_id,
                                   InstanceReadiness& readiness);
HRESULT wait_for_instance_readiness(const std::wstring& instance_id,
                                    unsigned timeout_ms,
                                    InstanceReadiness& readiness);
const wchar_t* readiness_state_name(ReadinessState state);
const wchar_t* readiness_hint(ReadinessState state);

} // namespace sar::devices

#endif
