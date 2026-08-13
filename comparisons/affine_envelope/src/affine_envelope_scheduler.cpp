#include "comparisons/affine_envelope_scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace comparisons {

PreparationResult AffineEnvelopeScheduler::prepare(
    const NeutralBaselineSchedule& baseline,
    const NeutralWorkload& workload
) {
    (void)baseline;
    workload_ = workload;
    current_time_ = 0;

    metrics_ = SchedulerMetrics{};
    metrics_.algorithm_name = name();
    metrics_.input_fingerprint = compute_input_fingerprint(workload, baseline);

    // This co-design adaptation currently implements the paper's independent
    // TT synthesis only for dependency-free, unit-slot TT jobs. Refuse broader
    // common-model inputs explicitly instead of silently producing an invalid
    // schedule and overstating comparability.
    const bool has_dependencies = std::any_of(
        workload_.periodic_jobs.begin(), workload_.periodic_jobs.end(),
        [](const ComparisonJob& job) { return !job.predecessors.empty(); }
    );
    const bool has_multislot_tt = std::any_of(
        workload_.periodic_jobs.begin(), workload_.periodic_jobs.end(),
        [](const ComparisonJob& job) { return job.execution_requirement != 1; }
    );
    if (has_dependencies || has_multislot_tt) {
        prepared_success_ = false;
        synthesized_schedule_ = NeutralBaselineSchedule(workload_.horizon);
        generated_schedule_fingerprint_ = compute_input_fingerprint(workload_, synthesized_schedule_);
        const std::string reason = has_dependencies
            ? "AFFINE_UNSUPPORTED_PERIODIC_PRECEDENCE"
            : "AFFINE_UNSUPPORTED_NONPREEMPTIVE_MULTISLOT_TT";
        return PreparationResult{false, reason, 0, workload.sporadic_streams.size()};
    }

    calculate_arrival_curve_and_affine_envelope();
    prepared_success_ = synthesize_tt_schedule_modified_llf();

    generated_schedule_fingerprint_ = compute_input_fingerprint(workload_, synthesized_schedule_);

    if (!prepared_success_) {
        return PreparationResult{false, "AFFINE_ENVELOPE_LLF_SYNTHESIS_FAILED", 0, workload.sporadic_streams.size()};
    }

    return PreparationResult{true, "AFFINE_ENVELOPE_PREPARED", workload.sporadic_streams.size(), 0};
}

void AffineEnvelopeScheduler::calculate_arrival_curve_and_affine_envelope() {
    double total_rho = 0.0;
    double max_sigma = 0.0;

    for (const auto& ss : workload_.sporadic_streams) {
        if (ss.min_interarrival > 0) {
            double stream_rho = static_cast<double>(ss.execution_requirement) / static_cast<double>(ss.min_interarrival);
            total_rho += stream_rho;
            double stream_sigma = static_cast<double>(ss.execution_requirement);
            max_sigma += stream_sigma;
        }
    }

    affine_params_.sigma = max_sigma;
    affine_params_.rho = total_rho;
}

bool AffineEnvelopeScheduler::synthesize_tt_schedule_modified_llf() {
    synthesized_schedule_ = NeutralBaselineSchedule(workload_.horizon);

    struct LLFJob {
        ComparisonJob job;
        SlotCount remaining_C;
        SlotIndex current_release;
        SlotIndex current_deadline;
        int64_t laxity(SlotIndex t) const {
            return (current_deadline - t) - remaining_C;
        }
    };

    std::vector<LLFJob> ready_queue;
    for (const auto& j : workload_.periodic_jobs) {
        ready_queue.push_back({j, j.execution_requirement, j.release, j.absolute_deadline});
    }

    SlotCount reserved_et_slots = static_cast<SlotCount>(std::ceil(affine_params_.sigma));

    for (SlotIndex s = 0; s < workload_.horizon; ++s) {
        if (reserved_et_slots > 0 && (s % 4 == 0)) {
            reserved_et_slots--;
            continue;
        }

        int64_t min_lax = 999999;
        size_t best_idx = 999999;

        for (size_t i = 0; i < ready_queue.size(); ++i) {
            auto& lj = ready_queue[i];
            if (s >= lj.current_release && lj.remaining_C > 0) {
                int64_t lax = lj.laxity(s);
                if (lax < min_lax) {
                    min_lax = lax;
                    best_idx = i;
                }
            }
        }

        if (best_idx < ready_queue.size()) {
            auto& lj = ready_queue[best_idx];
            if (s + 1 > lj.current_deadline) {
                return false; // Impossible to synthesize TT schedule within deadline
            }
            auto& slot = synthesized_schedule_.slots[static_cast<size_t>(s)];
            slot.job_id = lj.job.id;
            slot.task_id = lj.job.task_id;
            slot.type = lj.job.type;
            slot.release = lj.job.release;
            slot.deadline = lj.job.absolute_deadline;
            lj.remaining_C--;
        }
    }

    // Verify all periodic jobs finished within deadline
    for (const auto& lj : ready_queue) {
        if (lj.remaining_C > 0) {
            synthesized_schedule_ = NeutralBaselineSchedule(workload_.horizon);
            return false; // Could not synthesize all TT jobs
        }
    }

    return true;
}

void AffineEnvelopeScheduler::advance_to(SlotIndex time) {
    current_time_ = time;
}

ComparisonDecision AffineEnvelopeScheduler::on_dynamic_arrival(
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

    if (!prepared_success_) {
        decision.accepted = false;
        decision.rejection_reason = "LLF_SYNTHESIS_FAILED";
        metrics_.rejected_requests++;
        auto end_time = std::chrono::high_resolution_clock::now();
        decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        return decision;
    }

    SlotIndex latest_start = request.absolute_deadline - request.execution_requirement;
    SlotIndex earliest_start = std::max(request.release, current_time_);

    SlotIndex chosen_start = -1;
    for (SlotIndex s = earliest_start; s <= latest_start; ++s) {
        if (synthesized_schedule_.is_range_free(s, request.execution_requirement)) {
            chosen_start = s;
            break;
        }
    }

    if (chosen_start >= 0) {
        synthesized_schedule_.assign_job(request, chosen_start);
        active_jobs_[request.id] = request;

        decision.accepted = true;
        decision.decision_mechanism = "AFFINE_ENVELOPE_ET_SERVICE";
        decision.completion = chosen_start + request.execution_requirement;
        decision.jobs_moved = 0;
        decision.slots_changed = static_cast<size_t>(request.execution_requirement);

        metrics_.accepted_requests++;
    } else {
        decision.accepted = false;
        decision.rejection_reason = "NO_SYNTHESIZED_ET_SLOTS";
        metrics_.rejected_requests++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    return decision;
}

} // namespace comparisons
