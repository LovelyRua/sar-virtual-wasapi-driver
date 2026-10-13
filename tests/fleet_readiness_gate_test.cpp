#include "src/fleet_readiness_gate.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

using sar::devices::FleetReadinessGate;

bool test_empty_fleet_never_becomes_ready() {
    FleetReadinessGate gate;
    if (gate.observe({}, true) || gate.stable_samples() != 0) return false;
    if (gate.observe({}, true) || gate.stable_samples() != 0) return false;
    return !gate.observe({}, true);
}

bool test_stable_roster_requires_consecutive_ready_samples() {
    FleetReadinessGate gate(3);
    const std::vector<std::wstring> ids{L"ROOT\\MEDIA\\0001", L"ROOT\\MEDIA\\0002"};
    if (gate.observe(ids, true) || gate.stable_samples() != 1) return false;
    if (gate.observe(ids, true) || gate.stable_samples() != 2) return false;
    if (!gate.observe(ids, true) || gate.stable_samples() != 3) return false;
    return gate.observe(ids, true) && gate.stable_samples() == 3;
}

bool test_order_and_case_changes_do_not_reset_the_roster() {
    FleetReadinessGate gate(3);
    if (gate.observe({L"ROOT\\MEDIA\\0001", L"ROOT\\MEDIA\\0002"}, true)) return false;
    if (gate.observe({L"root\\media\\0002", L"root\\media\\0001"}, true)) return false;
    return gate.observe({L"Root\\Media\\0001", L"Root\\Media\\0002"}, true);
}

bool test_unready_member_resets_stability() {
    FleetReadinessGate gate(3);
    const std::vector<std::wstring> ids{L"ROOT\\MEDIA\\0001"};
    gate.observe(ids, true);
    gate.observe(ids, true);
    if (gate.observe(ids, false) || gate.stable_samples() != 0) return false;
    if (gate.observe(ids, true) || gate.stable_samples() != 1) return false;
    return !gate.observe(ids, true) && gate.stable_samples() == 2;
}

bool test_unready_sample_after_ready_restarts_full_stability_window() {
    FleetReadinessGate gate(3);
    const std::vector<std::wstring> ids{L"ROOT\\MEDIA\\0001"};
    if (gate.observe(ids, true) || gate.stable_samples() != 1) return false;
    if (gate.observe(ids, true) || gate.stable_samples() != 2) return false;
    if (!gate.observe(ids, true) || gate.stable_samples() != 3) return false;
    if (gate.observe(ids, false) || gate.stable_samples() != 0) return false;
    if (gate.observe(ids, true) || gate.stable_samples() != 1) return false;
    if (gate.observe(ids, true) || gate.stable_samples() != 2) return false;
    return gate.observe(ids, true) && gate.stable_samples() == 3;
}

bool test_roster_addition_or_removal_restarts_the_window() {
    FleetReadinessGate gate(3);
    gate.observe({L"ROOT\\MEDIA\\0001"}, true);
    gate.observe({L"ROOT\\MEDIA\\0001"}, true);
    if (gate.observe({L"ROOT\\MEDIA\\0001", L"ROOT\\MEDIA\\0002"}, true) ||
        gate.stable_samples() != 1) return false;
    if (gate.observe({L"ROOT\\MEDIA\\0001", L"ROOT\\MEDIA\\0002"}, true) ||
        gate.stable_samples() != 2) return false;
    return !gate.observe({L"ROOT\\MEDIA\\0001"}, true) &&
           gate.stable_samples() == 1;
}

bool test_duplicate_ids_invalidate_the_sample_and_reset_history() {
    FleetReadinessGate gate(2);
    gate.observe({L"ROOT\\MEDIA\\0001"}, true);
    const bool accepted = gate.observe(
        {L"ROOT\\MEDIA\\0001", L"root\\media\\0001"}, true);
    return !accepted && gate.stable_samples() == 0 &&
           !gate.observe({L"ROOT\\MEDIA\\0001"}, true);
}

bool test_zero_required_samples_is_clamped_to_one() {
    FleetReadinessGate gate(0);
    return gate.observe({L"ROOT\\MEDIA\\0001"}, true) &&
           gate.stable_samples() == 1;
}

} // namespace

int main() {
    if (!test_empty_fleet_never_becomes_ready()) return 1;
    if (!test_stable_roster_requires_consecutive_ready_samples()) return 2;
    if (!test_order_and_case_changes_do_not_reset_the_roster()) return 3;
    if (!test_unready_member_resets_stability()) return 4;
    if (!test_unready_sample_after_ready_restarts_full_stability_window()) return 5;
    if (!test_roster_addition_or_removal_restarts_the_window()) return 6;
    if (!test_duplicate_ids_invalidate_the_sample_and_reset_history()) return 7;
    if (!test_zero_required_samples_is_clamped_to_one()) return 8;
    std::cout << "Fleet readiness gate tests passed\n";
    return 0;
}
