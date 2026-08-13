#ifndef COMPARISONS_DTSS_SCHEDULER_HPP
#define COMPARISONS_DTSS_SCHEDULER_HPP

#include "comparisons/interface.hpp"
#include <vector>
#include <unordered_map>

namespace comparisons {

enum class DtssMode {
    StaticGranularity,
    DynamicGranularity
};

struct TargetExecutionWindow {
    JobID job_id{-1};
    TaskID task_id{-1};
    SlotIndex tew_release{0};
    SlotIndex tew_deadline{0};
    SlotCount duration{1};
};

class DtssScheduler : public IComparisonScheduler {
public:
    explicit DtssScheduler(DtssMode mode = DtssMode::StaticGranularity, bool skipping_enabled = false);

    std::string name() const override {
        return (mode_ == DtssMode::StaticGranularity) ? "DTSS_Static" : "DTSS_Dynamic";
    }

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

    SchedulerMetrics metrics() const override { return metrics_; }

    bool is_skipping_enabled() const { return skipping_enabled_; }

    // Debug State Inspection (Requirement 71)
    std::vector<TargetExecutionWindow> get_tews() const { return tews_; }

    // RPCA Calculation public for oracle verification
    SlotCount calculate_rpc(SlotIndex t1, SlotIndex t2) const;
    bool verify_rpca_feasibility(SlotIndex r, SlotIndex d, SlotCount C) const;

private:
    void extract_target_execution_windows();

    DtssMode mode_;
    bool skipping_enabled_;
    NeutralBaselineSchedule active_schedule_;
    NeutralWorkload workload_;
    SlotIndex current_time_{0};
    SchedulerMetrics metrics_;

    std::vector<TargetExecutionWindow> tews_;
    std::unordered_map<JobID, ComparisonJob> active_jobs_;
};

} // namespace comparisons

#endif // COMPARISONS_DTSS_SCHEDULER_HPP
