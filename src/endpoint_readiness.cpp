#ifdef _WIN32

#include "endpoint_readiness.h"

#include <algorithm>
#include <cwctype>
#include <thread>
#include <utility>

namespace sar::devices {
namespace {

bool same_id(const std::wstring& lhs, const std::wstring& rhs) {
    if (lhs.size() != rhs.size()) return false;
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](wchar_t left, wchar_t right) {
                          return std::towupper(left) == std::towupper(right);
                      });
}

void count_endpoint(const WasapiEndpoint& endpoint, EndpointCounts& counts) {
    const bool active = endpoint.state == DEVICE_STATE_ACTIVE;
    if (endpoint.flow == EndpointFlow::render) {
        ++counts.render;
        if (active) ++counts.render_active;
        if (active && endpoint.mix_format_available) ++counts.render_with_format;
    } else {
        ++counts.capture;
        if (active) ++counts.capture_active;
        if (active && endpoint.mix_format_available) ++counts.capture_with_format;
    }
}

ReadinessState classify(const InstanceReadiness& readiness) {
    if (!readiness.instance.started || readiness.instance.problem) {
        return ReadinessState::device_stopped;
    }

    const auto& counts = readiness.counts;
    const bool complete_render = counts.render == kRenderEndpointsPerInstance;
    const bool complete_capture = counts.capture == kCaptureEndpointsPerInstance;
    if ((!complete_render || !complete_capture) &&
        readiness.active_unassociated_endpoint_count != 0) {
        return ReadinessState::endpoint_parent_unavailable;
    }
    if (counts.render > kRenderEndpointsPerInstance ||
        counts.capture > kCaptureEndpointsPerInstance) {
        return ReadinessState::unexpected_endpoint_count;
    }
    if (!complete_render || !complete_capture) {
        return ReadinessState::endpoints_missing;
    }

    // A WASAPI item without a PnP parent cannot safely be attributed to this
    // instance. Do not use duplicate endpoint display names as a fallback.
    const bool all_parented = std::all_of(
        readiness.endpoints.begin(), readiness.endpoints.end(),
        [](const WasapiEndpoint& endpoint) {
            return !endpoint.parent_device_id.empty() && SUCCEEDED(endpoint.parent_lookup_error);
        });
    if (!all_parented) return ReadinessState::endpoint_parent_unavailable;

    if (counts.render_active != kRenderEndpointsPerInstance ||
        counts.capture_active != kCaptureEndpointsPerInstance) {
        return ReadinessState::endpoints_inactive;
    }
    if (counts.render_with_format != kRenderEndpointsPerInstance ||
        counts.capture_with_format != kCaptureEndpointsPerInstance) {
        return ReadinessState::mix_format_unavailable;
    }
    return ReadinessState::ready;
}

} // namespace

HRESULT inspect_instance_readiness(const std::wstring& instance_id,
                                   InstanceReadiness& readiness) {
    readiness = {};
    std::vector<Instance> instances;
    auto result = list(instances);
    if (!result.ok) {
        readiness.inventory_error = HRESULT_FROM_WIN32(result.error);
        return readiness.inventory_error;
    }

    const auto found = std::find_if(instances.begin(), instances.end(),
                                    [&](const Instance& instance) {
                                        return same_id(instance.id, instance_id);
                                    });
    if (found == instances.end()) {
        readiness.state = ReadinessState::device_missing;
        readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        return readiness.inventory_error;
    }
    readiness.instance = *found;

    std::vector<WasapiEndpoint> endpoints;
    const HRESULT inventory_result = enumerate_wasapi_endpoints(endpoints);
    if (FAILED(inventory_result)) {
        readiness.inventory_error = inventory_result;
        return inventory_result;
    }

    for (auto& endpoint : endpoints) {
        if (endpoint.state == DEVICE_STATE_ACTIVE &&
            FAILED(endpoint.parent_lookup_error)) {
            ++readiness.active_unassociated_endpoint_count;
        }
        if (same_id(endpoint.parent_device_id, instance_id)) {
            count_endpoint(endpoint, readiness.counts);
            readiness.endpoints.push_back(std::move(endpoint));
        }
    }
    readiness.state = classify(readiness);
    readiness.stable_samples = readiness.ready() ? 1u : 0u;
    return S_OK;
}

HRESULT wait_for_instance_readiness(const std::wstring& instance_id,
                                    unsigned timeout_ms,
                                    InstanceReadiness& readiness) {
    readiness = {};
    if (timeout_ms == 0 || timeout_ms > 120000) {
        readiness.inventory_error = E_INVALIDARG;
        return E_INVALIDARG;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    unsigned consecutive_ready = 0;
    HRESULT last_result = S_OK;

    do {
        InstanceReadiness snapshot;
        last_result = inspect_instance_readiness(instance_id, snapshot);
        if (FAILED(last_result) && last_result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
            readiness = std::move(snapshot);
            readiness.inventory_error = last_result;
            return last_result;
        }

        if (SUCCEEDED(last_result) && snapshot.ready()) {
            ++consecutive_ready;
            snapshot.stable_samples = consecutive_ready;
            readiness = std::move(snapshot);
            if (consecutive_ready >= kStableReadinessSamples) return S_OK;
        } else {
            consecutive_ready = 0;
            readiness = std::move(snapshot);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kReadinessPollIntervalMs));
    } while (std::chrono::steady_clock::now() < deadline);

    readiness.stable_samples = consecutive_ready;
    if (consecutive_ready != 0) {
        readiness.state = ReadinessState::stability_not_confirmed;
    }
    readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return readiness.inventory_error;
}

const wchar_t* readiness_state_name(ReadinessState state) {
    switch (state) {
    case ReadinessState::device_missing: return L"device-missing";
    case ReadinessState::device_stopped: return L"device-stopped";
    case ReadinessState::endpoint_parent_unavailable: return L"endpoint-parent-unavailable";
    case ReadinessState::endpoints_missing: return L"endpoints-missing";
    case ReadinessState::unexpected_endpoint_count: return L"unexpected-endpoint-count";
    case ReadinessState::endpoints_inactive: return L"endpoints-inactive";
    case ReadinessState::mix_format_unavailable: return L"mix-format-unavailable";
    case ReadinessState::stability_not_confirmed: return L"stability-not-confirmed";
    case ReadinessState::ready: return L"ready";
    default: return L"unknown";
    }
}

const wchar_t* readiness_hint(ReadinessState state) {
    switch (state) {
    case ReadinessState::device_missing:
        return L"The SAR root Media device is not present. Add it with a signed package.";
    case ReadinessState::device_stopped:
        return L"The PnP adapter is not started. Inspect Device Manager and Kernel-PnP events.";
    case ReadinessState::endpoint_parent_unavailable:
        return L"Windows did not expose PnP parent IDs for every endpoint; names are not used to guess ownership.";
    case ReadinessState::endpoints_missing:
        return L"AudioEndpointBuilder may still be publishing endpoints; wait and inspect driver/Audio events.";
    case ReadinessState::unexpected_endpoint_count:
        return L"The published topology differs from this prototype's two render plus two capture endpoints.";
    case ReadinessState::endpoints_inactive:
        return L"One or more expected endpoints are present but disabled, unplugged, or otherwise inactive.";
    case ReadinessState::mix_format_unavailable:
        return L"One or more active endpoints could not provide an IAudioClient mix format.";
    case ReadinessState::stability_not_confirmed:
        return L"The topology appeared ready but did not remain ready for three consecutive inventory samples.";
    case ReadinessState::ready:
        return L"All four endpoints are active, parented to this adapter, and expose mix formats across three samples.";
    default:
        return L"Readiness state is unknown.";
    }
}

} // namespace sar::devices

#endif
