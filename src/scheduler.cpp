#include "bcss/scheduler.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <queue>

namespace bcss {

SlotCount BcssScheduler::count_moved_jobs(
    const Schedule& initial_schedule,
    const Schedule& candidate_schedule,
    const std::vector<Job>& initial_jobs
) {
    SlotCount count = 0;
    for (const auto& j : initial_jobs) {
        SlotIndex init_s = initial_schedule.get_job_start(j.job_id);
        SlotIndex cand_s = candidate_schedule.get_job_start(j.job_id);
        if (init_s != -1 && cand_s != init_s) {
            count++;
        }
    }
    return count;
}

bool BcssScheduler::set_periodic_baseline(const std::vector<Job>& tt_jobs, const Schedule& baseline_schedule) {
    std::string err;
    ValidationResult vres = ScheduleValidator::verify_schedule(baseline_schedule, tt_jobs, dependencies, 0);
    if (!vres.valid) {
        std::cerr << "Periodic baseline schedule validation failed: " << vres.error_message << "\n";
        return false;
    }
    active_schedule = baseline_schedule;
    all_jobs = tt_jobs;
    for (auto& job : all_jobs) {
        job.current_start = active_schedule.get_job_start(job.job_id);
        if (job.original_start < 0) {
            job.original_start = job.current_start;
        }
    }
    return true;
}

bool BcssScheduler::admit_sporadic_stream_offline(const SporadicStreamSpec& stream) {
    // 1. Parameter Sanity
    if (stream.min_inter_arrival <= 0 || stream.duration <= 0 || stream.relative_deadline <= 0 ||
        stream.duration > stream.relative_deadline) {
        return false;
    }

    // 2. Capacity utilization check: Total periodic TT + sporadic load <= 1.0
    double new_load = static_cast<double>(stream.duration) / static_cast<double>(stream.min_inter_arrival);
    double existing_sporadic_load = 0.0;
    for (const auto& s : admitted_sporadic_streams) {
        existing_sporadic_load += static_cast<double>(s.duration) / static_cast<double>(s.min_inter_arrival);
    }

    double periodic_load = 0.0;
    if (active_schedule.horizon > 0) {
        SlotCount tt_slots = 0;
        for (const auto& slot : active_schedule.slots) {
            if (!slot.is_free()) tt_slots++;
        }
        periodic_load = static_cast<double>(tt_slots) / static_cast<double>(active_schedule.horizon);
    }

    if (periodic_load + existing_sporadic_load + new_load > 1.0) {
        return false;
    }

    // 3. Static RTC Envelope Feasibility against active periodic baseline
    std::vector<SporadicStreamSpec> trial_streams = admitted_sporadic_streams;
    trial_streams.push_back(stream);

    std::string rtc_err;
    if (enable_rtc_guard && !RtcEnvelopeGuard::check_guard(active_schedule, 0, trial_streams, dependencies, rtc_err)) {
        return false; // Offline admission rejected due to RTC capacity shortfall
    }

    admitted_sporadic_streams.push_back(stream);
    return true;
}

BcssResult BcssScheduler::admit_dynamic_job(const Job& new_job, SlotIndex t_now) {
    auto start_time = std::chrono::steady_clock::now();

    auto finalize_result = [&](BcssResult res) -> BcssResult {
        auto end_time = std::chrono::steady_clock::now();
        res.stats.search_time_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count()
        );
        return res;
    };

    BcssResult result;
    result.success = false;
    result.schedule = active_schedule;
    result.pre_schedule_hash = ScheduleHasher::compute_hash(active_schedule);
    result.post_schedule_hash = result.pre_schedule_hash;
    result.decision_mechanism = "REJECT";
    result.rejection_reason = "No feasible compensation candidate found within bound K";

    SlotCount horizon = active_schedule.horizon;
    if (horizon <= 0 || max_K < 0 || t_now < 0 || t_now >= horizon) {
        result.rejection_reason = "INVALID_INPUT";
        return finalize_result(result);
    }

    // =========================================================================
    // SPORADIC CONTRACT VALIDATION (r_{k+1} - r_k >= T_min)
    // =========================================================================
    if (new_job.type == TaskType::Sporadic) {
        if (new_job.min_inter_arrival <= 0) {
            result.decision_mechanism = "SPORADIC_CONTRACT_VIOLATION";
            result.rejection_reason = "Invalid sporadic min_inter_arrival parameter";
            return finalize_result(result);
        }
        auto it = last_sporadic_arrival.find(new_job.task_id);
        if (it != last_sporadic_arrival.end()) {
            SlotIndex delta_arrival = t_now - it->second;
            if (delta_arrival < new_job.min_inter_arrival) {
                result.decision_mechanism = "SPORADIC_CONTRACT_VIOLATION";
                result.rejection_reason = "Sporadic arrival interval " + std::to_string(delta_arrival) +
                                           " < T_min (" + std::to_string(new_job.min_inter_arrival) + ")";
                return finalize_result(result);
            }
        }
    }

    // Helper job lookup
    auto get_job_ptr = [&](JobID j_id) -> const Job* {
        if (j_id == new_job.job_id) return &new_job;
        for (const auto& j : all_jobs) {
            if (j.job_id == j_id) return &j;
        }
        return nullptr;
    };

    // Map of initial slot starts
    std::unordered_map<JobID, SlotIndex> initial_start_of;
    for (const auto& kv : active_schedule.job_to_start) {
        initial_start_of[kv.first] = kv.second;
    }

    // =========================================================================
    // STEP 1: Direct Available Capacity (k = 0)
    // =========================================================================
    SlotIndex start_search = std::max(t_now, new_job.release);
    SlotIndex end_search = std::min(horizon, new_job.absolute_deadline) - new_job.duration + 1;

    for (SlotIndex s = start_search; s < end_search; ++s) {
        if (active_schedule.is_range_free(s, new_job.duration)) {
            result.stats.candidates_generated++;
            Schedule temp = active_schedule;
            temp.assign_job(new_job, s);

            ValidationConfig vconfig{t_now, 0, true};
            ValidationResult vres = ScheduleValidator::verify_transition(active_schedule, temp, all_jobs, &new_job, dependencies, vconfig);
            if (vres.valid) {
                result.stats.candidates_feasible++;
            }

            std::string rtc_err;
            bool rtc_safe = true;
            if (enable_rtc_guard && !admitted_sporadic_streams.empty()) {
                result.stats.rtc_checks++;
                rtc_safe = RtcEnvelopeGuard::check_guard(temp, t_now, admitted_sporadic_streams, dependencies, rtc_err);
                if (!rtc_safe) {
                    result.stats.rtc_unsafe++;
                }
            }

            if (vres.valid && rtc_safe) {
                SlotCount moved = count_moved_jobs(active_schedule, temp, all_jobs);
                if (moved != 0) {
                    result.stats.candidates_rejected_by_validator++;
                    continue;
                }

                active_schedule = temp; // Atomic Commit!
                Job committed_job = new_job;
                committed_job.current_start = active_schedule.get_job_start(new_job.job_id);
                committed_job.original_start = committed_job.current_start;
                all_jobs.push_back(committed_job);
                if (new_job.type == TaskType::Sporadic) last_sporadic_arrival[new_job.task_id] = t_now;

                result.success = true;
                result.schedule = active_schedule;
                result.post_schedule_hash = ScheduleHasher::compute_hash(active_schedule);
                result.decision_mechanism = "ACCEPT_DIRECT";
                result.actual_k = 0;
                result.max_disp = 0;
                result.total_disp = 0;
                return finalize_result(result);
            }
        }
    }

    // =========================================================================
    // STEP 2: Unused Sporadic-Capacity Reclamation (k = 1)
    // (Requires K >= 1 because moving an existing job modifies 1 allocation)
    // =========================================================================
    if (enable_reclamation && max_K >= 1) {
        for (const auto& j_existing : all_jobs) {
            if (j_existing.current_start > t_now && j_existing.release <= t_now) {
                SlotIndex orig_s = j_existing.current_start;
                for (SlotIndex target_s = t_now; target_s < orig_s; ++target_s) {
                    Schedule temp = active_schedule;
                    temp.remove_job(j_existing.job_id, j_existing.duration);
                    if (temp.is_range_free(target_s, j_existing.duration) && temp.is_range_free(orig_s, new_job.duration)) {
                        result.stats.candidates_generated++;
                        temp.assign_job(j_existing, target_s);
                        temp.assign_job(new_job, orig_s);

                        ValidationConfig vconfig{t_now, max_K, true}; // Reclaiming an existing job requires K >= 1
                        ValidationResult vres = ScheduleValidator::verify_transition(active_schedule, temp, all_jobs, &new_job, dependencies, vconfig);
                        if (vres.valid) {
                            result.stats.candidates_feasible++;
                        }

                        std::string rtc_err;
                        bool rtc_safe = true;
                        if (enable_rtc_guard && !admitted_sporadic_streams.empty()) {
                            result.stats.rtc_checks++;
                            rtc_safe = RtcEnvelopeGuard::check_guard(temp, t_now, admitted_sporadic_streams, dependencies, rtc_err);
                            if (!rtc_safe) {
                                result.stats.rtc_unsafe++;
                            }
                        }

                        if (vres.valid && rtc_safe) {
                            SlotCount moved = count_moved_jobs(active_schedule, temp, all_jobs);
                            if (moved != 1 || moved > max_K) {
                                result.stats.candidates_rejected_by_validator++;
                                continue;
                            }

                            active_schedule = temp; // Atomic Commit!
                            for (auto& job : all_jobs) {
                                job.current_start = active_schedule.get_job_start(job.job_id);
                            }
                            Job committed_job = new_job;
                            committed_job.current_start = active_schedule.get_job_start(new_job.job_id);
                            committed_job.original_start = committed_job.current_start;
                            all_jobs.push_back(committed_job);
                            if (new_job.type == TaskType::Sporadic) last_sporadic_arrival[new_job.task_id] = t_now;

                            result.success = true;
                            result.schedule = active_schedule;
                            result.post_schedule_hash = ScheduleHasher::compute_hash(active_schedule);
                            result.decision_mechanism = "ACCEPT_RECLAIM";
                            result.actual_k = 1;
                            result.max_disp = std::abs(target_s - orig_s);
                            result.total_disp = result.max_disp;
                            return finalize_result(result);
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // STEP 3: Compensation-Aware Periodic Slot Stealing (k <= K)
    // =========================================================================
    if (!enable_compensation || max_K == 0) {
        // Strict BCSS-0 Invariant: Zero existing jobs may move
        assert(result.post_schedule_hash == result.pre_schedule_hash);
        return finalize_result(result);
    }

    std::vector<PathCandidate> valid_candidates;
    std::queue<PathCandidate> q;

    PathCandidate init_cand;
    init_cand.jobs.push_back(new_job.job_id);
    init_cand.pending.push_back(new_job.job_id);
    q.push(init_cand);

    const uint64_t MAX_NODES = 50000;
    SlotCount best_safe_k = max_K + 1;

    while (!q.empty() && result.stats.nodes_expanded < MAX_NODES) {
        PathCandidate curr = q.front();
        q.pop();
        result.stats.nodes_expanded++;
        const SlotCount current_displaced = curr.jobs.empty()
            ? 0
            : static_cast<SlotCount>(curr.jobs.size() - 1U);
        if (current_displaced > best_safe_k) {
            result.stats.paths_pruned++;
            continue;
        }
        result.stats.max_search_depth = std::max<uint64_t>(
            result.stats.max_search_depth,
            curr.jobs.empty() ? 0U : static_cast<uint64_t>(curr.jobs.size() - 1)
        );

        if (curr.pending.empty()) {
            // All displaced jobs placed! Compute exact schedule-level displacement & recourse
            Schedule temp = active_schedule;
            for (size_t i = 1; i < curr.jobs.size(); ++i) {
                JobID j_id = curr.jobs[i];
                SlotIndex old_s = initial_start_of.count(j_id) ? initial_start_of[j_id] : -1;
                const Job* j_ptr = get_job_ptr(j_id);
                if (old_s != -1 && j_ptr) temp.remove_job(j_id, j_ptr->duration);
            }
            for (size_t i = 0; i < curr.jobs.size(); ++i) {
                JobID j_id = curr.jobs[i];
                SlotIndex new_s = curr.starts[i];
                const Job* j_ptr = get_job_ptr(j_id);
                if (j_ptr) temp.assign_job(*j_ptr, new_s);
            }

            SlotCount exact_k = count_moved_jobs(active_schedule, temp, all_jobs);
            if (exact_k <= max_K) {
                PathCandidate complete_cand = curr;
                complete_cand.k = exact_k;
                complete_cand.max_disp = 0;
                complete_cand.total_disp = 0;

                for (size_t i = 1; i < curr.jobs.size(); ++i) {
                    JobID j_id = curr.jobs[i];
                    SlotIndex old_s = initial_start_of.count(j_id) ? initial_start_of[j_id] : -1;
                    SlotIndex new_s = curr.starts[i];
                    if (old_s != -1) {
                        SlotCount disp = std::abs(new_s - old_s);
                        complete_cand.max_disp = std::max(complete_cand.max_disp, disp);
                        complete_cand.total_disp += disp;
                    }
                }

                result.stats.candidates_found++;
                result.stats.candidates_generated++;
                ValidationConfig vconfig{t_now, max_K, true};
                ValidationResult vres = ScheduleValidator::verify_transition(
                    active_schedule,
                    temp,
                    all_jobs,
                    &new_job,
                    dependencies,
                    vconfig
                );
                if (!vres.valid) {
                    result.stats.candidates_rejected_by_validator++;
                    continue;
                }
                result.stats.candidates_feasible++;

                std::string rtc_err;
                if (enable_rtc_guard && !admitted_sporadic_streams.empty()) {
                    result.stats.rtc_checks++;
                    if (!RtcEnvelopeGuard::check_guard(temp, t_now, admitted_sporadic_streams, dependencies, rtc_err)) {
                        result.stats.candidates_rejected_by_rtc++;
                        result.stats.rtc_unsafe++;
                        continue;
                    }
                }

                best_safe_k = std::min(best_safe_k, exact_k);
                valid_candidates.push_back(complete_cand);
            }
            continue;
        }

        JobID active_job_id = curr.pending.front();
        PathCandidate next_base = curr;
        next_base.pending.erase(next_base.pending.begin());

        const Job* active_job = get_job_ptr(active_job_id);
        if (!active_job) continue;

        SlotIndex s_start = std::max(t_now, active_job->release);
        SlotIndex s_end = std::min(horizon, active_job->absolute_deadline) - active_job->duration + 1;

        for (SlotIndex s = s_start; s < s_end; ++s) {
            PathCandidate next = next_base;
            next.starts.push_back(s);

            // Find overlapping occupants at target range [s, s + duration)
            std::vector<JobID> occupants;
            for (SlotIndex slot_idx = s; slot_idx < s + active_job->duration; ++slot_idx) {
                JobID occ = active_schedule.slots[static_cast<size_t>(slot_idx)].job_id;
                if (occ != -1 && occ != active_job_id &&
                    std::find(occupants.begin(), occupants.end(), occ) == occupants.end() &&
                    std::find(next.jobs.begin(), next.jobs.end(), occ) == next.jobs.end()) {
                    occupants.push_back(occ);
                }
            }

            // Check cycle prevention
            bool cycle = false;
            for (JobID occ : occupants) {
                if (std::find(curr.jobs.begin(), curr.jobs.end(), occ) != curr.jobs.end()) {
                    cycle = true;
                    break;
                }
            }

            if (!cycle) {
                for (JobID occ : occupants) {
                    next.jobs.push_back(occ);
                    next.pending.push_back(occ);
                }
                // Every newly displaced distinct occupant must leave an
                // allocation now used by another path job. Therefore a path
                // containing more than K existing occupants cannot later
                // satisfy the exact schedule-level recourse bound.
                const SlotCount displaced_existing = static_cast<SlotCount>(next.jobs.size() - 1U);
                if (displaced_existing <= max_K && displaced_existing <= best_safe_k) {
                    q.push(next);
                } else {
                    result.stats.paths_pruned++;
                }
            } else {
                result.stats.paths_pruned++;
            }
        }
    }

    if (valid_candidates.empty()) {
        assert(result.post_schedule_hash == result.pre_schedule_hash);
        return finalize_result(result);
    }

    // =========================================================================
    // STEP 4: Rank Candidates using Lexicographical 3-Stage Key (k, delta_max, delta_total)
    // =========================================================================
    std::sort(valid_candidates.begin(), valid_candidates.end());

    // =========================================================================
    // STEP 5: Commit the best already validated and RTC-safe candidate
    // =========================================================================
    for (const auto& cand : valid_candidates) {
        Schedule temp = active_schedule;

        for (size_t i = 1; i < cand.jobs.size(); ++i) {
            JobID j_id = cand.jobs[i];
            SlotIndex old_s = initial_start_of.count(j_id) ? initial_start_of[j_id] : -1;
            const Job* j_ptr = get_job_ptr(j_id);
            if (old_s != -1 && j_ptr) temp.remove_job(j_id, j_ptr->duration);
        }

        for (size_t i = 0; i < cand.jobs.size(); ++i) {
            JobID j_id = cand.jobs[i];
            SlotIndex new_s = cand.starts[i];
            const Job* j_ptr = get_job_ptr(j_id);
            if (j_ptr) temp.assign_job(*j_ptr, new_s);
        }

        // Exact independent recourse verification assertion
        SlotCount exact_k = count_moved_jobs(active_schedule, temp, all_jobs);
        assert(exact_k <= max_K);

        // Atomic Commit!
        active_schedule = temp;
        for (auto& job : all_jobs) {
            job.current_start = active_schedule.get_job_start(job.job_id);
        }
        Job committed_job = new_job;
        committed_job.current_start = active_schedule.get_job_start(new_job.job_id);
        committed_job.original_start = committed_job.current_start;
        all_jobs.push_back(committed_job);
        if (new_job.type == TaskType::Sporadic) last_sporadic_arrival[new_job.task_id] = t_now;

        result.success = true;
        result.schedule = active_schedule;
        result.post_schedule_hash = ScheduleHasher::compute_hash(active_schedule);
        result.decision_mechanism = "ACCEPT_COMPENSATION";
        result.actual_k = exact_k;
        result.max_disp = cand.max_disp;
        result.total_disp = cand.total_disp;
        return finalize_result(result);
    }

    // Hash Rejection Property: post_hash == pre_hash
    assert(result.post_schedule_hash == result.pre_schedule_hash);
    return finalize_result(result);
}

} // namespace bcss
