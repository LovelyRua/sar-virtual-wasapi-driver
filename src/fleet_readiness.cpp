#ifdef _WIN32

#include "fleet_readiness.h"

#include "fleet_readiness_gate.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace sar::devices {
namespace {

void classify_fleet(FleetReadiness& readiness) {
    if (readiness.instances.empty()) {
        readiness.state = FleetReadinessState::no_instances;
        return;
    }
    readiness.state = readiness.ready_instances() == readiness.instances.size()
        ? FleetReadinessState::ready
        : FleetReadinessState::not_ready;
}

} // namespace

std::size_t FleetReadiness::ready_instances() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        instances.begin(), instances.end(),
        [](const InstanceReadiness& instance) { return instance.ready(); }));
}

HRESULT inspect_fleet_readiness(FleetReadiness& readiness) {
    readiness = {};

    std::vector<Instance> instances;
    const Result listed = list(instances);
    if (!listed.ok) {
        readiness.inventory_error = HRESULT_FROM_WIN32(listed.error);
        return readiness.inventory_error;
    }
    if (instances.empty()) {
        readiness.state = FleetReadinessState::no_instances;
        return S_OK;
    }

    std::vector<WasapiEndpoint> endpoints;
    const HRESULT enumerated = enumerate_wasapi_endpoints(endpoints);
    if (FAILED(enumerated)) {
        readiness.inventory_error = enumerated;
        return enumerated;
    }

    readiness.instances.reserve(instances.size());
    for (const auto& instance : instances) {
        InstanceReadiness member;
        const HRESULT assessed = inspect_instance_readiness(instance, endpoints, member);
        if (FAILED(assessed)) {
            readiness = {};
            readiness.inventory_error = assessed;
            return assessed;
        }
        readiness.instances.push_back(std::move(member));
    }
    classify_fleet(readiness);
    return S_OK;
}

HRESULT wait_for_fleet_readiness(unsigned timeout_ms, FleetReadiness& readiness) {
    readiness = {};
    if (timeout_ms == 0 || timeout_ms > 120000) {
        readiness.inventory_error = E_INVALIDARG;
        return E_INVALIDARG;
    }

    FleetReadinessGate gate(kStableReadinessSamples);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    do {
        FleetReadiness snapshot;
        const HRESULT result = inspect_fleet_readiness(snapshot);
        if (FAILED(result)) {
            readiness = std::move(snapshot);
            readiness.inventory_error = result;
            return result;
        }

        std::vector<std::wstring> ids;
        ids.reserve(snapshot.instances.size());
        for (const auto& member : snapshot.instances) ids.push_back(member.instance.id);
        const bool all_ready = !snapshot.instances.empty() &&
                               snapshot.ready_instances() == snapshot.instances.size();
        const bool stable = gate.observe(std::move(ids), all_ready);
        for (auto& member : snapshot.instances) {
            member.stable_samples = all_ready ? gate.stable_samples() : 0;
        }
        if (stable) {
            snapshot.state = FleetReadinessState::ready;
            snapshot.stable_samples = gate.stable_samples();
            readiness = std::move(snapshot);
            return S_OK;
        }

        snapshot.stable_samples = gate.stable_samples();
        classify_fleet(snapshot);
        if (snapshot.state == FleetReadinessState::ready) {
            snapshot.state = FleetReadinessState::stability_not_confirmed;
        }
        readiness = std::move(snapshot);
        const auto delay = bounded_readiness_poll_delay(
            deadline, std::chrono::steady_clock::now(),
            std::chrono::milliseconds(kReadinessPollIntervalMs));
        if (delay > std::chrono::steady_clock::duration::zero()) {
            std::this_thread::sleep_for(delay);
        }
    } while (std::chrono::steady_clock::now() < deadline);

    readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return readiness.inventory_error;
}

const wchar_t* fleet_readiness_state_name(FleetReadinessState state) {
    switch (state) {
    case FleetReadinessState::no_instances: return L"no-instances";
    case FleetReadinessState::not_ready: return L"not-ready";
    case FleetReadinessState::stability_not_confirmed:
        return L"stability-not-confirmed";
    case FleetReadinessState::ready: return L"ready";
    default: return L"unknown";
    }
}

} // namespace sar::devices

#endif
