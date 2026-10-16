#ifdef _WIN32

#include "src/fleet_audio_probe_plan.h"

#include <string>
#include <vector>

namespace {

using sar::devices::EndpointFlow;
using sar::devices::FleetAudioProbePlan;
using sar::devices::FleetReadiness;
using sar::devices::InstanceReadiness;
using sar::devices::WasapiEndpoint;

WasapiEndpoint endpoint(const std::wstring& instance_id,
                        const std::wstring& name, const std::wstring& id,
                        EndpointFlow flow) {
    WasapiEndpoint result;
    result.name = name;
    result.id = id;
    result.parent_device_id = instance_id;
    result.flow = flow;
    result.state = DEVICE_STATE_ACTIVE;
    result.mix_format_available = true;
    result.channels = 2;
    result.sample_rate = 48000;
    return result;
}

InstanceReadiness instance(unsigned number) {
    std::wstring suffix = std::to_wstring(number);
    suffix.insert(0, 4 - suffix.size(), L'0');
    const std::wstring id = std::wstring(L"ROOT\\MEDIA\\") + suffix;
    const std::wstring label = std::wstring(L"Lab ") + suffix;
    InstanceReadiness result;
    result.instance.id = id;
    result.instance.label = label;
    result.state = sar::devices::ReadinessState::ready;
    result.stable_samples = sar::devices::kStableReadinessSamples;
    result.endpoints = {
        endpoint(id, std::wstring(L"Speakers (") + label + L")",
                 std::wstring(L"render-speaker-") + suffix, EndpointFlow::render),
        endpoint(id, std::wstring(L"Headphones (") + label + L")",
                 std::wstring(L"render-headphone-") + suffix, EndpointFlow::render),
        endpoint(id, std::wstring(L"SAR Experimental Capture 1 (") + label + L")",
                 std::wstring(L"capture-1-") + suffix, EndpointFlow::capture),
        endpoint(id, std::wstring(L"SAR Experimental Capture 2 (") + label + L")",
                 std::wstring(L"capture-2-") + suffix, EndpointFlow::capture),
    };
    return result;
}

FleetReadiness fleet(unsigned count = 1) {
    FleetReadiness result;
    result.state = sar::devices::FleetReadinessState::ready;
    result.stable_samples = sar::devices::kStableReadinessSamples;
    for (unsigned number = 1; number <= count; ++number) {
        result.instances.push_back(instance(number));
    }
    return result;
}

bool test_single_instance_plan_is_deterministic() {
    FleetAudioProbePlan plan;
    const auto input = fleet();
    return sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.size() == 2 && plan.buses[0].fleet_bus == 0 &&
           plan.buses[0].instance_bus == 0 &&
           plan.buses[0].render_endpoint_id == L"render-speaker-0001" &&
           plan.buses[0].capture_endpoint_id == L"capture-1-0001" &&
           plan.buses[1].fleet_bus == 1 && plan.buses[1].instance_bus == 1 &&
           plan.buses[1].render_endpoint_id == L"render-headphone-0001" &&
           plan.buses[1].capture_endpoint_id == L"capture-2-0001";
}

bool test_two_instances_produce_four_parented_pairs() {
    FleetAudioProbePlan plan;
    const auto input = fleet(2);
    return sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.size() == 4 && plan.buses[2].fleet_bus == 2 &&
           plan.buses[2].instance_bus == 0 &&
           plan.buses[2].instance_id == L"ROOT\\MEDIA\\0002" &&
           plan.buses[2].render_endpoint_id == L"render-speaker-0002" &&
           plan.buses[3].fleet_bus == 3 && plan.buses[3].instance_bus == 1 &&
           plan.buses[3].render_endpoint_id == L"render-headphone-0002";
}

bool test_four_instances_produce_eight_parented_pairs_in_order() {
    FleetAudioProbePlan plan;
    if (!sar::devices::build_fleet_audio_probe_plan(fleet(4), plan) ||
        plan.buses.size() != 8) return false;
    for (unsigned index = 0; index < 8; ++index) {
        std::wstring suffix = std::to_wstring(index / 2 + 1);
        suffix.insert(0, 4 - suffix.size(), L'0');
        const bool second_bus = (index % 2) != 0;
        const auto& bus = plan.buses[index];
        if (bus.fleet_bus != index || bus.instance_bus != index % 2 ||
            bus.instance_id != std::wstring(L"ROOT\\MEDIA\\") + suffix ||
            bus.render_endpoint_id != std::wstring(second_bus
                ? L"render-headphone-" : L"render-speaker-") + suffix ||
            bus.capture_endpoint_id != std::wstring(second_bus
                ? L"capture-2-" : L"capture-1-") + suffix) return false;
    }
    return true;
}

bool test_empty_or_oversized_fleet_is_rejected() {
    FleetAudioProbePlan plan;
    auto empty = fleet(0);
    empty.state = sar::devices::FleetReadinessState::no_instances;
    FleetReadiness oversized = fleet(5);
    return !sar::devices::build_fleet_audio_probe_plan(empty, plan) &&
           plan.buses.empty() && !plan.error.empty() &&
           !sar::devices::build_fleet_audio_probe_plan(oversized, plan) &&
           plan.buses.empty();
}

bool test_unstable_or_unready_fleet_is_rejected() {
    FleetAudioProbePlan plan;
    auto unstable = fleet();
    unstable.stable_samples = 2;
    if (sar::devices::build_fleet_audio_probe_plan(unstable, plan)) return false;
    auto unready_member = fleet();
    unready_member.instances[0].stable_samples = 2;
    return !sar::devices::build_fleet_audio_probe_plan(unready_member, plan) &&
           plan.buses.empty();
}

bool test_endpoint_parent_must_match_instance() {
    FleetAudioProbePlan plan;
    auto input = fleet();
    input.instances[0].endpoints[0].parent_device_id = L"ROOT\\MEDIA\\0099";
    return !sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.empty();
}

bool test_endpoint_names_must_match_registered_instance_label() {
    FleetAudioProbePlan plan;
    auto input = fleet();
    input.instances[0].instance.label = L"Renamed after endpoint publication";
    return !sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.empty();
}

bool test_endpoint_enumeration_order_does_not_change_bus_pairing() {
    FleetAudioProbePlan plan;
    auto input = fleet(2);
    for (auto& member : input.instances) {
        std::reverse(member.endpoints.begin(), member.endpoints.end());
    }
    return sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.size() == 4 &&
           plan.buses[0].render_endpoint_id == L"render-speaker-0001" &&
           plan.buses[0].capture_endpoint_id == L"capture-1-0001" &&
           plan.buses[1].render_endpoint_id == L"render-headphone-0001" &&
           plan.buses[1].capture_endpoint_id == L"capture-2-0001" &&
           plan.buses[2].render_endpoint_id == L"render-speaker-0002" &&
           plan.buses[2].capture_endpoint_id == L"capture-1-0002" &&
           plan.buses[3].render_endpoint_id == L"render-headphone-0002" &&
           plan.buses[3].capture_endpoint_id == L"capture-2-0002";
}

bool test_missing_or_ambiguous_expected_endpoint_is_rejected() {
    FleetAudioProbePlan plan;
    auto missing = fleet();
    missing.instances[0].endpoints.erase(missing.instances[0].endpoints.begin());
    auto ambiguous = fleet();
    ambiguous.instances[0].endpoints.push_back(ambiguous.instances[0].endpoints[0]);
    return !sar::devices::build_fleet_audio_probe_plan(missing, plan) &&
           !sar::devices::build_fleet_audio_probe_plan(ambiguous, plan);
}

bool test_non_stereo_or_non_48khz_endpoint_is_rejected() {
    FleetAudioProbePlan plan;
    auto mono = fleet();
    mono.instances[0].endpoints[0].channels = 1;
    auto wrong_rate = fleet();
    wrong_rate.instances[0].endpoints[2].sample_rate = 44100;
    return !sar::devices::build_fleet_audio_probe_plan(mono, plan) &&
           !sar::devices::build_fleet_audio_probe_plan(wrong_rate, plan);
}

bool test_duplicate_endpoint_ids_across_instances_are_rejected_case_insensitively() {
    FleetAudioProbePlan plan;
    auto input = fleet(2);
    input.instances[1].endpoints[0].id = L"RENDER-SPEAKER-0001";
    return !sar::devices::build_fleet_audio_probe_plan(input, plan) &&
           plan.buses.empty();
}

bool test_inactive_or_unformatted_endpoints_are_rejected() {
    FleetAudioProbePlan plan;
    auto inactive = fleet();
    inactive.instances[0].endpoints[2].state = DEVICE_STATE_DISABLED;
    auto no_format = fleet();
    no_format.instances[0].endpoints[3].mix_format_available = false;
    return !sar::devices::build_fleet_audio_probe_plan(inactive, plan) &&
           !sar::devices::build_fleet_audio_probe_plan(no_format, plan);
}

}  // namespace

int main() {
    return test_single_instance_plan_is_deterministic() &&
                   test_two_instances_produce_four_parented_pairs() &&
                   test_four_instances_produce_eight_parented_pairs_in_order() &&
                   test_empty_or_oversized_fleet_is_rejected() &&
                   test_unstable_or_unready_fleet_is_rejected() &&
                   test_endpoint_parent_must_match_instance() &&
                   test_endpoint_names_must_match_registered_instance_label() &&
                   test_endpoint_enumeration_order_does_not_change_bus_pairing() &&
                   test_missing_or_ambiguous_expected_endpoint_is_rejected() &&
                   test_non_stereo_or_non_48khz_endpoint_is_rejected() &&
                   test_duplicate_endpoint_ids_across_instances_are_rejected_case_insensitively() &&
                   test_inactive_or_unformatted_endpoints_are_rejected()
               ? 0
               : 1;
}

#endif
