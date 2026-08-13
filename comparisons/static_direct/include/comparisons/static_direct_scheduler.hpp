#ifndef COMPARISONS_STATIC_DIRECT_SCHEDULER_HPP
#define COMPARISONS_STATIC_DIRECT_SCHEDULER_HPP

#include "comparisons/interface.hpp"
#include <unordered_map>

namespace comparisons {

class StaticDirectScheduler : public IComparisonScheduler {
public:
    StaticDirectScheduler() = default;

    std::string name() const override { return "StaticDirect"; }

    PreparationResult prepare(
        const NeutralBaselineSchedule& baseline,
        const NeutralWorkload& workload
    ) override;

    ComparisonDecision on_dynamic_arrival(
        const ComparisonJob& request,
        SlotIndex current_time
    ) override;

    void advance_to(SlotIndex time) override;

    NeutralBaselineSchedule snapshot() const override { return active_schedule_; }

    SchedulerMetrics metrics() const override;

private:
    NeutralBaselineSchedule active_schedule_;
    NeutralWorkload workload_;
    SlotIndex current_time_{0};
    SchedulerMetrics metrics_;
    std::unordered_map<JobID, ComparisonJob> all_jobs_;
};

} // namespace comparisons

#endif // COMPARISONS_STATIC_DIRECT_SCHEDULER_HPP
