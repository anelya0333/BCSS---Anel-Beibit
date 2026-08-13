#ifndef COMPARISONS_SLOT_SHIFTING_SCHEDULER_HPP
#define COMPARISONS_SLOT_SHIFTING_SCHEDULER_HPP

#include "comparisons/interface.hpp"
#include <vector>
#include <unordered_map>
#include <set>

namespace comparisons {

enum class SlotShiftingMode {
    PaperNative,         // Slot-level preemptive execution
    CommonCommunication  // Contiguous C slots requirement
};

struct CapacityInterval {
    size_t index{0};
    SlotIndex start{0};
    SlotIndex end{0};
    SlotCount spare_capacity{0};
    SlotCount leeway{0};
    SlotCount sporadic_reserve{0};
};

class SlotShiftingScheduler : public IComparisonScheduler {
public:
    explicit SlotShiftingScheduler(SlotShiftingMode mode = SlotShiftingMode::CommonCommunication);

    std::string name() const override {
        return (mode_ == SlotShiftingMode::PaperNative) ? "SlotShifting_Native" : "SlotShifting_Common";
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

    // Debug State Inspection (Requirement 71)
    std::vector<CapacityInterval> get_capacity_intervals() const { return intervals_; }
    bool is_sporadic_stream_admitted(TaskID stream_id) const {
        return admitted_sporadic_streams_.count(stream_id) != 0;
    }

private:
    void construct_capacity_intervals();
    void update_leeway();

    bool evaluate_offline_sporadic_stream(const SporadicStreamDefinition& stream);
    bool check_leeway_availability(const ComparisonJob& request) const;
    void deduct_leeway(SlotIndex r, SlotIndex d, SlotCount C);
    void initialize_sporadic_reserve();
    void consume_sporadic_reserve(SlotIndex r, SlotIndex d, SlotCount C);

    SlotShiftingMode mode_;
    NeutralBaselineSchedule active_schedule_;
    NeutralWorkload workload_;
    SlotIndex current_time_{0};
    SchedulerMetrics metrics_;

    std::vector<CapacityInterval> intervals_;
    std::unordered_map<JobID, ComparisonJob> active_jobs_;
    std::unordered_map<TaskID, SlotIndex> last_sporadic_release_;
    std::set<TaskID> admitted_sporadic_streams_;
    std::vector<SporadicStreamDefinition> admitted_sporadic_specs_;
};

} // namespace comparisons

#endif // COMPARISONS_SLOT_SHIFTING_SCHEDULER_HPP
