#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"
#include <chrono>

namespace comparisons {

PreparationResult StaticDirectScheduler::prepare(
    const NeutralBaselineSchedule& baseline,
    const NeutralWorkload& workload
) {
    active_schedule_ = baseline;
    workload_ = workload;
    current_time_ = 0;

    metrics_ = SchedulerMetrics{};
    metrics_.algorithm_name = name();
    metrics_.input_fingerprint = compute_input_fingerprint(workload, baseline);

    all_jobs_.clear();
    for (const auto& j : workload.periodic_jobs) {
        all_jobs_[j.id] = j;
    }

    return PreparationResult{true, "STATIC_DIRECT_PREPARED", 0, 0};
}

void StaticDirectScheduler::advance_to(SlotIndex time) {
    current_time_ = time;
}

ComparisonDecision StaticDirectScheduler::on_dynamic_arrival(
    const ComparisonJob& request,
    SlotIndex current_time
) {
    auto start_time = std::chrono::high_resolution_clock::now();
    current_time_ = current_time;

    ComparisonDecision decision;
    decision.algorithm = name();
    decision.release = request.release;
    decision.deadline = request.absolute_deadline;

    metrics_.total_requests++;

    SlotIndex latest_start = request.absolute_deadline - request.execution_requirement;
    SlotIndex earliest_start = std::max(request.release, current_time_);

    // Check precedence constraints
    for (JobID pred_id : request.predecessors) {
        SlotIndex pred_start = active_schedule_.get_job_start(pred_id);
        if (pred_start < 0) {
            // Predecessor not scheduled
            decision.accepted = false;
            decision.rejection_reason = "UNSATISFIED_PREDECESSOR";
            metrics_.rejected_requests++;
            auto end_time = std::chrono::high_resolution_clock::now();
            decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
            return decision;
        }
        auto pit = all_jobs_.find(pred_id);
        SlotCount pred_duration = (pit != all_jobs_.end()) ? pit->second.execution_requirement : 1;
        earliest_start = std::max(earliest_start, pred_start + pred_duration);
    }

    SlotIndex chosen_start = -1;
    for (SlotIndex s = earliest_start; s <= latest_start; ++s) {
        if (active_schedule_.is_range_free(s, request.execution_requirement)) {
            chosen_start = s;
            break;
        }
    }

    if (chosen_start >= 0) {
        active_schedule_.assign_job(request, chosen_start);
        all_jobs_[request.id] = request;

        decision.accepted = true;
        decision.decision_mechanism = "DIRECT_SLACK_ALLOCATION";
        decision.completion = chosen_start + request.execution_requirement;
        decision.jobs_moved = 0;
        decision.slots_changed = static_cast<size_t>(request.execution_requirement);

        metrics_.accepted_requests++;
    } else {
        decision.accepted = false;
        decision.rejection_reason = "NO_CONTIGUOUS_FREE_SLOTS";
        metrics_.rejected_requests++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    return decision;
}

SchedulerMetrics StaticDirectScheduler::metrics() const {
    return metrics_;
}

} // namespace comparisons
