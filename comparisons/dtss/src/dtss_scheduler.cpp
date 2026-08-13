#include "comparisons/dtss_scheduler.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace {

class RangeAddMaximum {
public:
    explicit RangeAddMaximum(comparisons::SlotCount size)
        : size_(size), maximum_(static_cast<size_t>(4 * size), 0), lazy_(static_cast<size_t>(4 * size), 0) {
        build(1, 0, size_ - 1);
    }

    void add(comparisons::SlotIndex left, comparisons::SlotIndex right, comparisons::SlotCount value) {
        if (left > right) return;
        add(1, 0, size_ - 1, left, right, value);
    }

    comparisons::SlotCount maximum(comparisons::SlotIndex left, comparisons::SlotIndex right) {
        if (left > right) return std::numeric_limits<comparisons::SlotCount>::lowest();
        return maximum(1, 0, size_ - 1, left, right);
    }

private:
    void build(size_t node, comparisons::SlotIndex left, comparisons::SlotIndex right) {
        if (left == right) {
            maximum_[node] = -left;
            return;
        }
        const auto middle = left + (right - left) / 2;
        build(node * 2, left, middle);
        build(node * 2 + 1, middle + 1, right);
        maximum_[node] = std::max(maximum_[node * 2], maximum_[node * 2 + 1]);
    }

    void apply(size_t node, comparisons::SlotCount value) {
        maximum_[node] += value;
        lazy_[node] += value;
    }

    void push(size_t node) {
        if (lazy_[node] == 0) return;
        apply(node * 2, lazy_[node]);
        apply(node * 2 + 1, lazy_[node]);
        lazy_[node] = 0;
    }

    void add(
        size_t node,
        comparisons::SlotIndex left,
        comparisons::SlotIndex right,
        comparisons::SlotIndex query_left,
        comparisons::SlotIndex query_right,
        comparisons::SlotCount value
    ) {
        if (query_left <= left && right <= query_right) {
            apply(node, value);
            return;
        }
        push(node);
        const auto middle = left + (right - left) / 2;
        if (query_left <= middle) add(node * 2, left, middle, query_left, query_right, value);
        if (query_right > middle) add(node * 2 + 1, middle + 1, right, query_left, query_right, value);
        maximum_[node] = std::max(maximum_[node * 2], maximum_[node * 2 + 1]);
    }

    comparisons::SlotCount maximum(
        size_t node,
        comparisons::SlotIndex left,
        comparisons::SlotIndex right,
        comparisons::SlotIndex query_left,
        comparisons::SlotIndex query_right
    ) {
        if (query_left <= left && right <= query_right) return maximum_[node];
        push(node);
        const auto middle = left + (right - left) / 2;
        comparisons::SlotCount result = std::numeric_limits<comparisons::SlotCount>::lowest();
        if (query_left <= middle) result = std::max(result, maximum(node * 2, left, middle, query_left, query_right));
        if (query_right > middle) result = std::max(result, maximum(node * 2 + 1, middle + 1, right, query_left, query_right));
        return result;
    }

    comparisons::SlotCount size_;
    std::vector<comparisons::SlotCount> maximum_;
    std::vector<comparisons::SlotCount> lazy_;
};

} // namespace

namespace comparisons {

DtssScheduler::DtssScheduler(DtssMode mode, bool skipping_enabled)
    : mode_(mode), skipping_enabled_(skipping_enabled) {}

PreparationResult DtssScheduler::prepare(
    const NeutralBaselineSchedule& baseline,
    const NeutralWorkload& workload
) {
    active_schedule_ = baseline;
    workload_ = workload;
    current_time_ = 0;

    metrics_ = SchedulerMetrics{};
    metrics_.algorithm_name = name();
    metrics_.input_fingerprint = compute_input_fingerprint(workload, baseline);

    active_jobs_.clear();
    for (const auto& j : workload.periodic_jobs) {
        active_jobs_[j.id] = j;
    }

    extract_target_execution_windows();

    return PreparationResult{true, "DTSS_PREPARED", 0, 0};
}

void DtssScheduler::extract_target_execution_windows() {
    tews_.clear();

    // TEWs are extracted from the static schedule for each TT job.
    // TEW_j = [r'_j, d'_j] where r'_j is job's release and d'_j is its deadline.
    // For tighter schedules, TEWs bound job execution to preserve precedent constraints.
    for (const auto& j : workload_.periodic_jobs) {
        TargetExecutionWindow tew;
        tew.job_id = j.id;
        tew.task_id = j.task_id;
        tew.tew_release = j.release;
        tew.tew_deadline = j.absolute_deadline;
        tew.duration = j.execution_requirement;

        SlotIndex alloc_start = active_schedule_.get_job_start(j.id);
        if (alloc_start >= 0) {
            tew.tew_release = std::min(j.release, alloc_start);
            tew.tew_deadline = std::max(j.absolute_deadline, alloc_start + j.execution_requirement);
        }

        tews_.push_back(tew);
    }
}

SlotCount DtssScheduler::calculate_rpc(SlotIndex t1, SlotIndex t2) const {
    if (t2 <= t1) return 0;
    SlotCount total_interval = t2 - t1;
    SlotCount allocated_in_window = 0;

    for (const auto& tew : tews_) {
        if (tew.tew_release >= t1 && tew.tew_deadline <= t2) {
            allocated_in_window += tew.duration;
        }
    }

    return std::max<SlotCount>(0, total_interval - allocated_in_window);
}

bool DtssScheduler::verify_rpca_feasibility(SlotIndex r, SlotIndex d, SlotCount C) const {
    if (d <= r || C <= 0) return false;
    if (d - r < C) return true; // Preserve the original loop's empty admissible-interval result.

    // The original predicate rejects iff, for any integer [t1,t2] inside
    // [r,d] with length >= C,
    //
    //   demand_of_TEWs_contained_in_[t1,t2] > (t2 - t1 - C).
    //
    // For fixed t1, maintain demand(t2)-t2 using range additions beginning at
    // each eligible TEW deadline. As t1 decreases, TEWs are inserted when their
    // release becomes eligible. Only TEW releases and the rightmost t1=d-C can
    // maximize the predicate between release events.
    std::vector<const TargetExecutionWindow*> jobs;
    std::vector<SlotIndex> candidate_starts{d - C};
    jobs.reserve(tews_.size());
    candidate_starts.reserve(tews_.size() + 1);
    for (const auto& tew : tews_) {
        if (tew.tew_deadline > d || tew.tew_release < r) continue;
        jobs.push_back(&tew);
        if (tew.tew_release <= d - C) candidate_starts.push_back(tew.tew_release);
    }
    std::sort(jobs.begin(), jobs.end(), [](const auto* left, const auto* right) {
        return left->tew_release > right->tew_release;
    });
    std::sort(candidate_starts.begin(), candidate_starts.end(), std::greater<SlotIndex>());
    candidate_starts.erase(std::unique(candidate_starts.begin(), candidate_starts.end()), candidate_starts.end());

    RangeAddMaximum demand_minus_end(d + 1);
    size_t next_job = 0;
    for (const SlotIndex t1 : candidate_starts) {
        while (next_job < jobs.size() && jobs[next_job]->tew_release >= t1) {
            const auto& job = *jobs[next_job++];
            demand_minus_end.add(std::max<SlotIndex>(0, job.tew_deadline), d, job.duration);
        }
        const SlotIndex earliest_end = t1 + C;
        const SlotCount worst = demand_minus_end.maximum(earliest_end, d);
        if (worst + t1 + C > 0) return false;
    }
    return true;
}

void DtssScheduler::advance_to(SlotIndex time) {
    current_time_ = time;
}

ComparisonDecision DtssScheduler::on_dynamic_arrival(
    const ComparisonJob& request,
    SlotIndex current_time
) {
    auto start_time = std::chrono::steady_clock::now();
    current_time_ = current_time;

    ComparisonDecision decision;
    decision.algorithm = name();
    decision.release = request.release;
    decision.deadline = request.absolute_deadline;

    metrics_.total_requests++;

    // RPCA feasibility check
    bool rpca_ok = verify_rpca_feasibility(request.release, request.absolute_deadline, request.execution_requirement);
    if (!rpca_ok) {
        decision.accepted = false;
        decision.rejection_reason = "RPCA_INFEASIBLE";
        metrics_.rejected_requests++;
        auto end_time = std::chrono::steady_clock::now();
        decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        return decision;
    }

    // Direct placement within TEW bounds using EDF order
    SlotIndex latest_start = request.absolute_deadline - request.execution_requirement;
    SlotIndex earliest_start = std::max(request.release, current_time_);

    SlotIndex chosen_start = -1;
    for (SlotIndex s = earliest_start; s <= latest_start; ++s) {
        if (active_schedule_.is_range_free(s, request.execution_requirement)) {
            chosen_start = s;
            break;
        }
    }

    if (chosen_start >= 0) {
        active_schedule_.assign_job(request, chosen_start);
        active_jobs_[request.id] = request;

        TargetExecutionWindow dyn_tew;
        dyn_tew.job_id = request.id;
        dyn_tew.task_id = request.task_id;
        dyn_tew.tew_release = request.release;
        dyn_tew.tew_deadline = request.absolute_deadline;
        dyn_tew.duration = request.execution_requirement;
        tews_.push_back(dyn_tew);

        decision.accepted = true;
        decision.decision_mechanism = "DTSS_RPCA_EDF_ALLOCATION";
        decision.completion = chosen_start + request.execution_requirement;
        decision.jobs_moved = 0;
        decision.slots_changed = static_cast<size_t>(request.execution_requirement);

        metrics_.accepted_requests++;
    } else {
        decision.accepted = false;
        decision.rejection_reason = "NO_CONTIGUOUS_TEW_SLOTS";
        metrics_.rejected_requests++;
    }

    auto end_time = std::chrono::steady_clock::now();
    decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    return decision;
}

} // namespace comparisons
