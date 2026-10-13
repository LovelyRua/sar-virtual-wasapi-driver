#pragma once

#ifdef _WIN32

#include "fleet_readiness.h"

#include <string>
#include <vector>

namespace sar::devices {

struct FleetAudioProbeBus {
    std::wstring instance_id;
    std::wstring instance_label;
    unsigned fleet_bus = 0;
    unsigned instance_bus = 0;
    std::wstring render_endpoint_id;
    std::wstring capture_endpoint_id;
};

struct FleetAudioProbePlan {
    std::vector<FleetAudioProbeBus> buses;
    std::wstring error;

    bool ready() const noexcept { return error.empty() && !buses.empty(); }
};

// Produces a deterministic render/capture list for the four-bus WASAPI probe.
// Endpoint names select the two documented buses; PnP parent IDs establish
// ownership, so duplicate friendly names across adapters are never guessed.
bool build_fleet_audio_probe_plan(const FleetReadiness& fleet,
                                 FleetAudioProbePlan& plan);

}  // namespace sar::devices

#endif
