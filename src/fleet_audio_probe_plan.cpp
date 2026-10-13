#ifdef _WIN32

#include "fleet_audio_probe_plan.h"

#include <algorithm>
#include <cwctype>

namespace sar::devices {
namespace {

struct ExpectedBus {
    const wchar_t* render_name;
    const wchar_t* capture_name;
};

constexpr ExpectedBus kExpectedBuses[] = {
    {L"Speakers (System Audio Route Experimental)",
     L"SAR Experimental Capture 1 (System Audio Route Experimental)"},
    {L"Headphones (System Audio Route Experimental)",
     L"SAR Experimental Capture 2 (System Audio Route Experimental)"},
};

bool same_id(const std::wstring& lhs, const std::wstring& rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](wchar_t left, wchar_t right) {
                          return std::towupper(left) == std::towupper(right);
                      });
}

bool endpoint_usable(const WasapiEndpoint& endpoint,
                     const std::wstring& instance_id,
                     EndpointFlow flow) {
    return endpoint.flow == flow && endpoint.state == DEVICE_STATE_ACTIVE &&
           endpoint.mix_format_available && endpoint.channels == 2 &&
           endpoint.sample_rate == 48000 && !endpoint.id.empty() &&
           !endpoint.parent_device_id.empty() &&
           SUCCEEDED(endpoint.parent_lookup_error) &&
           same_id(endpoint.parent_device_id, instance_id);
}

const WasapiEndpoint* find_endpoint(const InstanceReadiness& instance,
                                    const wchar_t* name,
                                    EndpointFlow flow) {
    const WasapiEndpoint* found = nullptr;
    for (const auto& endpoint : instance.endpoints) {
        if (endpoint.name != name || endpoint.flow != flow) continue;
        if (found != nullptr) return nullptr;
        found = &endpoint;
    }
    return found;
}

bool id_already_used(const FleetAudioProbePlan& plan,
                     const std::wstring& candidate) {
    for (const auto& bus : plan.buses) {
        if (same_id(bus.render_endpoint_id, candidate) ||
            same_id(bus.capture_endpoint_id, candidate)) return true;
    }
    return false;
}

}  // namespace

bool build_fleet_audio_probe_plan(const FleetReadiness& fleet,
                                 FleetAudioProbePlan& plan) {
    plan = {};
    if (!fleet.ready() || fleet.stable_samples < kStableReadinessSamples) {
        plan.error = L"The complete fleet did not pass its stable readiness window.";
        return false;
    }
    if (fleet.instances.empty() || fleet.instances.size() > 2) {
        plan.error = L"Audio probing supports one or two ready SAR instances.";
        return false;
    }

    for (const auto& instance : fleet.instances) {
        if (!instance.ready() || instance.stable_samples < kStableReadinessSamples ||
            instance.instance.id.empty() || instance.endpoints.size() != 4) {
            plan.buses.clear();
            plan.error = L"Every selected adapter must remain ready for three samples.";
            return false;
        }

        for (unsigned local_bus = 0; local_bus < 2; ++local_bus) {
            const auto* render = find_endpoint(
                instance, kExpectedBuses[local_bus].render_name, EndpointFlow::render);
            const auto* capture = find_endpoint(
                instance, kExpectedBuses[local_bus].capture_name, EndpointFlow::capture);
            if (render == nullptr || capture == nullptr ||
                !endpoint_usable(*render, instance.instance.id, EndpointFlow::render) ||
                !endpoint_usable(*capture, instance.instance.id, EndpointFlow::capture)) {
                plan.buses.clear();
                plan.error = L"A documented bus endpoint is missing, ambiguous, mis-parented, or not 48 kHz stereo.";
                return false;
            }
            if (same_id(render->id, capture->id) ||
                id_already_used(plan, render->id) || id_already_used(plan, capture->id)) {
                plan.buses.clear();
                plan.error = L"Probe endpoint IDs must be unique across the entire fleet.";
                return false;
            }

            plan.buses.push_back({instance.instance.id, instance.instance.label,
                                  static_cast<unsigned>(plan.buses.size()), local_bus,
                                  render->id, capture->id});
        }
    }
    return true;
}

}  // namespace sar::devices

#endif
