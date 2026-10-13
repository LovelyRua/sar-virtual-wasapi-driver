#pragma once

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

namespace sar::devices {

// A fleet is stable only while the same non-empty set of adapters is ready
// across consecutive inventory samples. Endpoint arrival/removal can otherwise
// make a partially published topology look ready for one polling interval.
class FleetReadinessGate {
public:
    explicit FleetReadinessGate(unsigned required_samples = 3) noexcept
        : required_samples_(required_samples == 0 ? 1 : required_samples) {}

    bool observe(std::vector<std::wstring> instance_ids, bool all_ready) {
        canonicalize(instance_ids);
        if (has_duplicates(instance_ids)) {
            reset();
            return false;
        }

        if (!has_roster_ || !same_roster(instance_ids, roster_)) {
            roster_ = std::move(instance_ids);
            has_roster_ = true;
            consecutive_samples_ = 0;
        }

        if (roster_.empty() || !all_ready) {
            consecutive_samples_ = 0;
            return false;
        }

        if (consecutive_samples_ < required_samples_) {
            ++consecutive_samples_;
        }
        return consecutive_samples_ >= required_samples_;
    }

    void reset() noexcept {
        roster_.clear();
        has_roster_ = false;
        consecutive_samples_ = 0;
    }

    unsigned stable_samples() const noexcept { return consecutive_samples_; }

private:
    static wchar_t fold(wchar_t value) noexcept {
        return static_cast<wchar_t>(std::towupper(value));
    }

    static bool less_id(const std::wstring& lhs, const std::wstring& rhs) {
        return std::lexicographical_compare(
            lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
            [](wchar_t left, wchar_t right) { return fold(left) < fold(right); });
    }

    static bool equal_id(const std::wstring& lhs, const std::wstring& rhs) {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                          [](wchar_t left, wchar_t right) {
                              return fold(left) == fold(right);
                          });
    }

    static void canonicalize(std::vector<std::wstring>& ids) {
        std::sort(ids.begin(), ids.end(), less_id);
    }

    static bool has_duplicates(const std::vector<std::wstring>& ids) {
        for (std::size_t index = 1; index < ids.size(); ++index) {
            if (equal_id(ids[index - 1], ids[index])) return true;
        }
        return false;
    }

    static bool same_roster(const std::vector<std::wstring>& lhs,
                            const std::vector<std::wstring>& rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t index = 0; index < lhs.size(); ++index) {
            if (!equal_id(lhs[index], rhs[index])) return false;
        }
        return true;
    }

    unsigned required_samples_;
    unsigned consecutive_samples_ = 0;
    bool has_roster_ = false;
    std::vector<std::wstring> roster_;
};

} // namespace sar::devices
