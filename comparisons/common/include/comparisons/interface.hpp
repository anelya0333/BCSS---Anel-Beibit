#ifndef COMPARISONS_INTERFACE_HPP
#define COMPARISONS_INTERFACE_HPP

#include "comparisons/neutral_model.hpp"
#include <string>
#include <chrono>

namespace comparisons {

struct PreparationResult {
    bool success{true};
    std::string message{"PREPARED"};
    std::size_t admitted_sporadic_streams{0};
    std::size_t rejected_sporadic_streams{0};
};

struct ComparisonDecision {
    bool accepted{false};
    std::string algorithm{};
    std::string decision_mechanism{};
    std::string rejection_reason{};

    SlotIndex release{0};
    SlotIndex deadline{0};
    SlotIndex completion{-1};

    std::size_t jobs_moved{0};
    std::size_t slots_changed{0};
    uint64_t states_examined{1};

    std::chrono::nanoseconds decision_time{0};
};

struct SchedulerMetrics {
    std::string algorithm_name;
    uint64_t total_requests{0};
    uint64_t accepted_requests{0};
    uint64_t rejected_requests{0};
    uint64_t periodic_deadline_misses{0};
    uint64_t sporadic_deadline_misses{0};
    uint64_t total_jobs_moved{0};
    uint64_t total_slots_changed{0};
    std::string input_fingerprint;
};

class IComparisonScheduler {
public:
    virtual ~IComparisonScheduler() = default;

    virtual std::string name() const = 0;

    virtual PreparationResult prepare(
        const NeutralBaselineSchedule& baseline,
        const NeutralWorkload& workload
    ) = 0;

    virtual ComparisonDecision on_dynamic_arrival(
        const ComparisonJob& request,
        SlotIndex current_time
    ) = 0;

    virtual void advance_to(SlotIndex time) = 0;

    virtual NeutralBaselineSchedule snapshot() const = 0;

    virtual SchedulerMetrics metrics() const = 0;
};

} // namespace comparisons

#endif // COMPARISONS_INTERFACE_HPP
