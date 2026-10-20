#ifdef _WIN32

#include "endpoint_removal.h"

#include "fleet_readiness_gate.h"
#include "endpoint_readiness.h"
#include "endpoint_removal_policy.h"
#include "wasapi_inventory.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <new>
#include <thread>
#include <utility>

namespace sar::devices {
namespace {

bool same_text_id(const std::wstring& lhs, const std::wstring& rhs) noexcept {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](wchar_t left, wchar_t right) {
                          return std::towupper(left) == std::towupper(right);
                      });
}

HRESULT snapshot(std::vector<Instance>& instances,
                 std::vector<WasapiEndpoint>& endpoints) {
    const Result listed = list(instances);
    if (!listed.ok) return HRESULT_FROM_WIN32(listed.error);
    return enumerate_wasapi_endpoints(endpoints);
}

void roster_ids(const std::vector<Instance>& instances,
                std::vector<std::wstring>& ids) {
    ids.clear();
    ids.reserve(instances.size());
    for (const Instance& instance : instances) ids.push_back(instance.id);
}

void endpoint_ids_for(const std::vector<WasapiEndpoint>& endpoints,
                      const std::wstring& instance_id,
                      std::vector<std::wstring>& ids) {
    ids.clear();
    for (const WasapiEndpoint& endpoint : endpoints) {
        if (endpoint_can_block_removal(endpoint) &&
            endpoint_parent_matches(endpoint, instance_id)) {
            ids.push_back(endpoint.id);
        }
    }
}

bool known_endpoint(const std::wstring& id,
                    const std::vector<std::wstring>& known_ids) noexcept {
    return std::any_of(known_ids.begin(), known_ids.end(),
                       [&](const std::wstring& known) {
                           return same_text_id(id, known);
                       });
}

size_t count_remaining_endpoints(
    const std::vector<WasapiEndpoint>& endpoints,
    const std::wstring& instance_id,
    const std::vector<std::wstring>& known_ids) {
    size_t remaining = 0;
    for (const WasapiEndpoint& endpoint : endpoints) {
        if (endpoint_can_block_removal(endpoint) &&
            (endpoint_parent_matches(endpoint, instance_id) ||
             known_endpoint(endpoint.id, known_ids))) {
            ++remaining;
        }
    }
    return remaining;
}

size_t count_unassociated_endpoints(
    const std::vector<WasapiEndpoint>& endpoints) noexcept {
    return static_cast<size_t>(std::count_if(
        endpoints.begin(), endpoints.end(),
        [](const WasapiEndpoint& endpoint) {
            return endpoint_removal_ownership_unknown(endpoint);
        }));
}

void classify_timeout(InstanceRemovalReadiness& readiness) {
    const bool target_present = std::any_of(
        readiness.observed_roster.begin(), readiness.observed_roster.end(),
        [&](const std::wstring& id) {
            return same_instance_id(id, readiness.instance_id);
        });
    if (target_present) {
        readiness.state = InstanceRemovalState::device_present;
    } else if (readiness.remaining_endpoint_count != 0) {
        readiness.state = InstanceRemovalState::endpoints_remaining;
    } else if (!same_instance_roster(readiness.expected_roster,
                                     readiness.observed_roster)) {
        readiness.state = InstanceRemovalState::roster_changed;
    } else {
        readiness.state = InstanceRemovalState::stability_not_confirmed;
    }
}

} // namespace

HRESULT remove_and_wait(const std::wstring& instance_id, unsigned timeout_ms,
                        InstanceRemovalReadiness& readiness) {
    readiness = {};
    try {
        readiness.instance_id = instance_id;
        if (timeout_ms == 0 || timeout_ms > 120000 || instance_id.empty()) {
            readiness.state = InstanceRemovalState::error;
            readiness.inventory_error = E_INVALIDARG;
            return E_INVALIDARG;
        }

        std::vector<Instance> before_instances;
        std::vector<WasapiEndpoint> before_endpoints;
        HRESULT result = snapshot(before_instances, before_endpoints);
        if (FAILED(result)) {
            readiness.state = InstanceRemovalState::error;
            readiness.inventory_error = result;
            return result;
        }
        readiness.unassociated_endpoint_count =
            count_unassociated_endpoints(before_endpoints);
        if (readiness.unassociated_endpoint_count != 0) {
            readiness.state = InstanceRemovalState::endpoint_ownership_unavailable;
            readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            return readiness.inventory_error;
        }
        const auto target = std::find_if(
            before_instances.begin(), before_instances.end(),
            [&](const Instance& instance) {
                return same_instance_id(instance.id, instance_id);
            });
        if (target == before_instances.end()) {
            readiness.state = InstanceRemovalState::error;
            readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            return readiness.inventory_error;
        }

        roster_ids(before_instances, readiness.expected_roster);
        readiness.expected_roster.erase(
            std::remove_if(readiness.expected_roster.begin(),
                           readiness.expected_roster.end(),
                           [&](const std::wstring& id) {
                               return same_instance_id(id, instance_id);
                           }),
            readiness.expected_roster.end());
        std::vector<std::wstring> known_endpoint_ids;
        endpoint_ids_for(before_endpoints, target->id, known_endpoint_ids);
        readiness.initial_endpoint_count = known_endpoint_ids.size();

        const Result removed = remove(target->id);
        if (!removed.ok) {
            readiness.state = InstanceRemovalState::error;
            readiness.inventory_error = HRESULT_FROM_WIN32(removed.error);
            return readiness.inventory_error;
        }

        InstanceRemovalGate gate(kStableReadinessSamples);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        do {
            std::vector<Instance> current_instances;
            std::vector<WasapiEndpoint> current_endpoints;
            result = snapshot(current_instances, current_endpoints);
            if (FAILED(result)) {
                readiness.state = InstanceRemovalState::error;
                readiness.inventory_error = result;
                return result;
            }
            readiness.unassociated_endpoint_count =
                count_unassociated_endpoints(current_endpoints);
            if (readiness.unassociated_endpoint_count != 0) {
                readiness.state = InstanceRemovalState::endpoint_ownership_unavailable;
                readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                return readiness.inventory_error;
            }
            roster_ids(current_instances, readiness.observed_roster);
            readiness.remaining_endpoint_count = count_remaining_endpoints(
                current_endpoints, target->id, known_endpoint_ids);
            const bool endpoints_absent = readiness.remaining_endpoint_count == 0;
            const bool stable = gate.observe(readiness.expected_roster,
                                             readiness.observed_roster,
                                             endpoints_absent);
            readiness.stable_samples = gate.stable_samples();
            if (stable) {
                readiness.state = InstanceRemovalState::removed;
                readiness.inventory_error = S_OK;
                return S_OK;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kReadinessPollIntervalMs));
        } while (std::chrono::steady_clock::now() < deadline);

        classify_timeout(readiness);
        readiness.inventory_error = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        return readiness.inventory_error;
    } catch (const std::bad_alloc&) {
        readiness.state = InstanceRemovalState::error;
        readiness.inventory_error = E_OUTOFMEMORY;
        return readiness.inventory_error;
    }
}

const wchar_t* removal_state_name(InstanceRemovalState state) {
    switch (state) {
    case InstanceRemovalState::device_present: return L"device-present";
    case InstanceRemovalState::endpoints_remaining: return L"endpoints-remaining";
    case InstanceRemovalState::endpoint_ownership_unavailable:
        return L"endpoint-ownership-unavailable";
    case InstanceRemovalState::roster_changed: return L"roster-changed";
    case InstanceRemovalState::stability_not_confirmed:
        return L"stability-not-confirmed";
    case InstanceRemovalState::error: return L"error";
    case InstanceRemovalState::removed: return L"removed";
    default: return L"unknown";
    }
}

} // namespace sar::devices

#endif
