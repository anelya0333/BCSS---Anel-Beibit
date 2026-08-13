#include "comparisons/slot_shifting_scheduler.hpp"
#include <algorithm>
#include <chrono>

namespace comparisons {

SlotShiftingScheduler::SlotShiftingScheduler(SlotShiftingMode mode)
    : mode_(mode) {}

PreparationResult SlotShiftingScheduler::prepare(
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

    construct_capacity_intervals();
    update_leeway();

    size_t admitted_streams = 0;
    size_t rejected_streams = 0;

    admitted_sporadic_streams_.clear();
    admitted_sporadic_specs_.clear();
    last_sporadic_release_.clear();
    for (const auto& ss : workload.sporadic_streams) {
        // The paper-native preemptive model supports the offline sporadic
        // guarantee. The non-preemptive common-communication adaptation does
        // not claim that guarantee; reporting streams as admitted there would
        // misclassify later runtime losses as protected-traffic failures.
        if (mode_ == SlotShiftingMode::PaperNative && evaluate_offline_sporadic_stream(ss)) {
            admitted_sporadic_streams_.insert(ss.stream_id);
            admitted_sporadic_specs_.push_back(ss);
            admitted_streams++;
        } else {
            rejected_streams++;
        }
    }
    initialize_sporadic_reserve();

    const std::string message = mode_ == SlotShiftingMode::PaperNative
        ? "SLOT_SHIFTING_PAPER_NATIVE_PREPARED"
        : "SLOT_SHIFTING_COMMON_NO_PROTECTED_SPORADIC_ADMISSION";
    return PreparationResult{true, message, admitted_streams, rejected_streams};
}

void SlotShiftingScheduler::construct_capacity_intervals() {
    std::set<SlotIndex> boundaries;
    boundaries.insert(0);
    boundaries.insert(active_schedule_.horizon);

    for (const auto& j : workload_.periodic_jobs) {
        if (j.absolute_deadline > 0 && j.absolute_deadline <= active_schedule_.horizon) {
            boundaries.insert(j.absolute_deadline);
        }
    }

    std::vector<SlotIndex> sorted_b(boundaries.begin(), boundaries.end());
    intervals_.clear();

    for (size_t i = 0; i < sorted_b.size() - 1; ++i) {
        CapacityInterval ci;
        ci.index = i;
        ci.start = sorted_b[i];
        ci.end = sorted_b[i + 1];

        SlotCount free_count = 0;
        for (SlotIndex s = ci.start; s < ci.end; ++s) {
            if (active_schedule_.is_free(s)) {
                free_count++;
            }
        }
        ci.spare_capacity = free_count;
        ci.leeway = 0;
        intervals_.push_back(ci);
    }
}

void SlotShiftingScheduler::update_leeway() {
    if (intervals_.empty()) return;
    int64_t n = static_cast<int64_t>(intervals_.size());

    for (int64_t k = 0; k < n; ++k) {
        SlotCount min_cum_spare = 999999;
        SlotCount cum_spare = 0;
        for (int64_t m = k; m < n; ++m) {
            cum_spare += intervals_[static_cast<size_t>(m)].spare_capacity;
            min_cum_spare = std::min(min_cum_spare, cum_spare);
        }
        intervals_[static_cast<size_t>(k)].leeway = min_cum_spare;
    }
}

bool SlotShiftingScheduler::evaluate_offline_sporadic_stream(const SporadicStreamDefinition& stream) {
    // Alkoudsi et al. 2025 Clarification:
    // Evaluate sporadic demand bound function over intervals bounded by H.
    SlotCount H = active_schedule_.horizon;
    for (const auto& ci : intervals_) {
        SlotCount delta = H - ci.start;
        if (delta <= 0) continue;

        SlotCount demand = 0;
        std::vector<SporadicStreamDefinition> trial = admitted_sporadic_specs_;
        trial.push_back(stream);
        for (const auto& candidate : trial) {
            if (delta >= candidate.relative_deadline) {
                const SlotCount occurrences = (delta / candidate.min_interarrival) + 1;
                demand += occurrences * candidate.execution_requirement;
            }
        }

        if (ci.leeway < demand) {
            return false; // Infeasible
        }
    }
    return true;
}

void SlotShiftingScheduler::initialize_sporadic_reserve() {
    const SlotCount horizon = active_schedule_.horizon;
    for (auto& interval : intervals_) {
        interval.sporadic_reserve = 0;
        const SlotCount delta = horizon - interval.start;
        for (const auto& stream : admitted_sporadic_specs_) {
            if (delta >= stream.relative_deadline) {
                const SlotCount occurrences = (delta / stream.min_interarrival) + 1;
                interval.sporadic_reserve += occurrences * stream.execution_requirement;
            }
        }
    }
}

bool SlotShiftingScheduler::check_leeway_availability(const ComparisonJob& request) const {
    if (intervals_.empty()) return false;
    for (const auto& ci : intervals_) {
        if (ci.end > request.release && ci.start < request.absolute_deadline) {
            if (ci.leeway < request.execution_requirement) return false;
            if (request.type != TrafficType::Sporadic &&
                ci.leeway - request.execution_requirement < ci.sporadic_reserve) {
                return false;
            }
        }
    }
    return true;
}

void SlotShiftingScheduler::consume_sporadic_reserve(SlotIndex r, SlotIndex d, SlotCount C) {
    for (auto& interval : intervals_) {
        if (interval.end > r && interval.start < d) {
            interval.sporadic_reserve = std::max<SlotCount>(0, interval.sporadic_reserve - C);
        }
    }
}

void SlotShiftingScheduler::deduct_leeway(SlotIndex r, SlotIndex d, SlotCount C) {
    SlotCount remaining_to_deduct = C;
    for (auto& ci : intervals_) {
        if (ci.end > r && ci.start < d && remaining_to_deduct > 0) {
            SlotCount take = std::min(ci.spare_capacity, remaining_to_deduct);
            ci.spare_capacity -= take;
            remaining_to_deduct -= take;
        }
    }
    update_leeway();
}

void SlotShiftingScheduler::advance_to(SlotIndex time) {
    current_time_ = time;
}

ComparisonDecision SlotShiftingScheduler::on_dynamic_arrival(
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

    // Contract verification for sporadic streams
    if (request.type == TrafficType::Sporadic) {
        if (admitted_sporadic_streams_.count(request.task_id) == 0) {
            decision.accepted = false;
            decision.rejection_reason = "STREAM_NOT_OFFLINE_ADMITTED";
            metrics_.rejected_requests++;
            auto end_time = std::chrono::high_resolution_clock::now();
            decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
            return decision;
        }

        auto last_it = last_sporadic_release_.find(request.task_id);
        if (last_it != last_sporadic_release_.end()) {
            if (request.release - last_it->second < request.min_interarrival.value_or(1)) {
                decision.accepted = false;
                decision.rejection_reason = "TMIN_CONTRACT_VIOLATED";
                metrics_.rejected_requests++;
                auto end_time = std::chrono::high_resolution_clock::now();
                decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
                return decision;
            }
        }
        last_sporadic_release_[request.task_id] = request.release;
    }

    // Leeway check
    bool leeway_ok = check_leeway_availability(request);
    if (!leeway_ok) {
        decision.accepted = false;
        decision.rejection_reason = "INSUFFICIENT_LEEWAY";
        metrics_.rejected_requests++;
        auto end_time = std::chrono::high_resolution_clock::now();
        decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        return decision;
    }

    // Allocation placement
    SlotIndex chosen_start = -1;
    SlotIndex latest_start = request.absolute_deadline - request.execution_requirement;
    SlotIndex earliest_start = std::max(request.release, current_time_);

    if (mode_ == SlotShiftingMode::CommonCommunication) {
        for (SlotIndex s = earliest_start; s <= latest_start; ++s) {
            if (active_schedule_.is_range_free(s, request.execution_requirement)) {
                chosen_start = s;
                break;
            }
        }
    } else {
        // Native preemptive / slot shifting placement
        for (SlotIndex s = earliest_start; s <= latest_start; ++s) {
            if (active_schedule_.is_range_free(s, request.execution_requirement)) {
                chosen_start = s;
                break;
            }
        }
    }

    // If no direct gap exists, perform the interval relocation that gives
    // Slot Shifting its distinct runtime behavior. Only allocations that have
    // not started may move; jobs are reinserted by EDF within their own
    // release/deadline windows. This uses no BCSS K bound, ranking, BFS, or RTC.
    NeutralBaselineSchedule relocated_schedule;
    size_t relocated_jobs = 0;
    size_t relocated_slots = 0;
    if (chosen_start < 0) {
        relocated_schedule = active_schedule_;
        std::vector<ComparisonJob> movable;
        std::unordered_map<JobID, SlotIndex> previous_starts;
        for (const auto& entry : active_jobs_) {
            const ComparisonJob& job = entry.second;
            const SlotIndex old_start = active_schedule_.get_job_start(job.id);
            if (old_start >= current_time_) {
                previous_starts[job.id] = old_start;
                relocated_schedule.clear_range(old_start, job.execution_requirement);
                movable.push_back(job);
            }
        }
        movable.push_back(request);
        std::sort(movable.begin(), movable.end(), [](const ComparisonJob& left, const ComparisonJob& right) {
            if (left.absolute_deadline != right.absolute_deadline) {
                return left.absolute_deadline < right.absolute_deadline;
            }
            if (left.release != right.release) return left.release < right.release;
            return left.id < right.id;
        });

        bool feasible = true;
        for (const auto& job : movable) {
            SlotIndex earliest = std::max(current_time_, job.release);
            for (JobID predecessor : job.predecessors) {
                const SlotIndex predecessor_start = relocated_schedule.get_job_start(predecessor);
                auto predecessor_it = active_jobs_.find(predecessor);
                if (predecessor_start < 0 || predecessor_it == active_jobs_.end()) {
                    feasible = false;
                    break;
                }
                earliest = std::max(
                    earliest,
                    predecessor_start + predecessor_it->second.execution_requirement
                );
            }
            if (!feasible) break;
            const SlotIndex latest = job.absolute_deadline - job.execution_requirement;
            SlotIndex selected = -1;
            for (SlotIndex start = earliest; start <= latest; ++start) {
                if (relocated_schedule.is_range_free(start, job.execution_requirement)) {
                    selected = start;
                    break;
                }
            }
            if (selected < 0) {
                feasible = false;
                break;
            }
            relocated_schedule.assign_job(job, selected);
        }

        if (feasible) {
            chosen_start = relocated_schedule.get_job_start(request.id);
            for (const auto& entry : previous_starts) {
                const SlotIndex new_start = relocated_schedule.get_job_start(entry.first);
                if (new_start != entry.second) {
                    ++relocated_jobs;
                }
            }
            for (SlotIndex slot = current_time_; slot < active_schedule_.horizon; ++slot) {
                if (active_schedule_.slots[static_cast<size_t>(slot)].job_id !=
                    relocated_schedule.slots[static_cast<size_t>(slot)].job_id) {
                    ++relocated_slots;
                }
            }
        }
    }

    if (chosen_start >= 0) {
        if (relocated_schedule.horizon > 0 && relocated_schedule.get_job_start(request.id) >= 0) {
            active_schedule_ = relocated_schedule;
        } else {
            active_schedule_.assign_job(request, chosen_start);
        }
        active_jobs_[request.id] = request;
        deduct_leeway(request.release, request.absolute_deadline, request.execution_requirement);
        if (request.type == TrafficType::Sporadic) {
            consume_sporadic_reserve(request.release, request.absolute_deadline, request.execution_requirement);
        }

        decision.accepted = true;
        decision.decision_mechanism = relocated_jobs > 0
            ? "SLOT_SHIFTING_INTERVAL_RELOCATION"
            : "SLOT_SHIFTING_LEEWAY_ALLOCATION";
        decision.completion = chosen_start + request.execution_requirement;
        decision.jobs_moved = relocated_jobs;
        decision.slots_changed = relocated_slots > 0
            ? relocated_slots
            : static_cast<size_t>(request.execution_requirement);

        metrics_.accepted_requests++;
    } else {
        decision.accepted = false;
        decision.rejection_reason = "NO_VALID_LEEWAY_PLACEMENT";
        metrics_.rejected_requests++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    decision.decision_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    return decision;
}

} // namespace comparisons
