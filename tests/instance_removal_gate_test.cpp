#include "src/fleet_readiness_gate.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

using sar::devices::InstanceRemovalGate;

bool test_removal_requires_three_stable_absent_samples() {
    InstanceRemovalGate gate(3);
    const std::vector<std::wstring> expected{L"ROOT\\MEDIA\\0002"};
    const std::vector<std::wstring> observed{L"root\\media\\0002"};
    if (gate.observe(expected, observed, true) || gate.stable_samples() != 1) return false;
    if (gate.observe(expected, observed, true) || gate.stable_samples() != 2) return false;
    if (!gate.observe(expected, observed, true) || gate.stable_samples() != 3) return false;
    return gate.observe(expected, observed, true) && gate.stable_samples() == 3;
}

bool test_endpoint_reappearance_resets_removal_window() {
    InstanceRemovalGate gate(3);
    const std::vector<std::wstring> expected{L"ROOT\\MEDIA\\0002"};
    gate.observe(expected, expected, true);
    gate.observe(expected, expected, true);
    if (gate.observe(expected, expected, false) || gate.stable_samples() != 0) return false;
    if (gate.observe(expected, expected, true) || gate.stable_samples() != 1) return false;
    return !gate.observe(expected, expected, true) && gate.stable_samples() == 2;
}

bool test_unexpected_adapter_blocks_removal_confirmation() {
    InstanceRemovalGate gate(2);
    const std::vector<std::wstring> expected{L"ROOT\\MEDIA\\0002"};
    const std::vector<std::wstring> unexpected{L"ROOT\\MEDIA\\0003"};
    if (gate.observe(expected, unexpected, true) || gate.stable_samples() != 0) return false;
    return !gate.observe(expected, unexpected, true) && gate.stable_samples() == 0;
}

bool test_removing_last_instance_allows_stable_empty_roster() {
    InstanceRemovalGate gate(2);
    if (gate.observe({}, {}, true) || gate.stable_samples() != 1) return false;
    return gate.observe({}, {}, true) && gate.stable_samples() == 2;
}

bool test_roster_comparison_ignores_order_and_case() {
    return sar::devices::same_instance_roster(
        {L"ROOT\\MEDIA\\0001", L"ROOT\\MEDIA\\0002"},
        {L"root\\media\\0002", L"root\\media\\0001"});
}

bool test_duplicate_roster_ids_never_confirm_teardown() {
    InstanceRemovalGate gate(1);
    const std::vector<std::wstring> duplicated{
        L"ROOT\\MEDIA\\0002", L"root\\media\\0002"};
    return !gate.observe(duplicated, duplicated, true) &&
           gate.stable_samples() == 0 &&
           !sar::devices::same_instance_roster(duplicated, duplicated);
}

bool test_case_change_does_not_reset_stability() {
    InstanceRemovalGate gate(3);
    if (gate.observe({L"ROOT\\MEDIA\\0002"}, {L"root\\media\\0002"}, true)) return false;
    if (gate.observe({L"root\\media\\0002"}, {L"ROOT\\MEDIA\\0002"}, true)) return false;
    return gate.observe({L"Root\\Media\\0002"}, {L"rOoT\\mEdIa\\0002"}, true);
}

bool test_zero_sample_requirement_is_clamped() {
    InstanceRemovalGate gate(0);
    return gate.observe({}, {}, true) && gate.stable_samples() == 1;
}

} // namespace

int main() {
    if (!test_removal_requires_three_stable_absent_samples()) return 1;
    if (!test_endpoint_reappearance_resets_removal_window()) return 2;
    if (!test_unexpected_adapter_blocks_removal_confirmation()) return 3;
    if (!test_removing_last_instance_allows_stable_empty_roster()) return 4;
    if (!test_roster_comparison_ignores_order_and_case()) return 5;
    if (!test_duplicate_roster_ids_never_confirm_teardown()) return 6;
    if (!test_case_change_does_not_reset_stability()) return 7;
    if (!test_zero_sample_requirement_is_clamped()) return 8;
    std::cout << "Instance removal gate tests passed\n";
    return 0;
}
