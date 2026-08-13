#include "bcss/validator.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iostream>

namespace bcss {

ValidationResult ScheduleValidator::verify_schedule(
    const Schedule& schedule,
    const std::vector<Job>& all_jobs,
    const DependencyGraph& deps,
    SlotIndex t_now
) {
    (void)t_now;
    ValidationResult res;
    res.valid = false;
    res.moved_jobs_count = 0;

    // 1. Horizon & Slot Index Integrity
    if (schedule.horizon <= 0) {
        res.error_message = "Invalid horizon size: " + std::to_string(schedule.horizon);
        return res;
    }

    if (static_cast<SlotCount>(schedule.slots.size()) != schedule.horizon) {
        res.error_message = "Schedule slots vector size (" + std::to_string(schedule.slots.size()) +
                            ") does not match horizon (" + std::to_string(schedule.horizon) + ")";
        return res;
    }

    for (SlotIndex s = 0; s < schedule.horizon; ++s) {
        if (schedule.slots[static_cast<size_t>(s)].slot_index != s) {
            res.error_message = "Slot index mismatch at index " + std::to_string(s);
            return res;
        }
    }

    // 2. Map input jobs
    std::unordered_map<JobID, const Job*> job_map;
    for (const auto& j : all_jobs) {
        if (job_map.count(j.job_id)) {
            res.error_message = "Duplicate job ID in input set: " + std::to_string(j.job_id);
            return res;
        }
        if (j.release < 0 || j.absolute_deadline <= j.release || j.duration <= 0) {
            res.error_message = "Malformed job parameters for job ID " + std::to_string(j.job_id);
            return res;
        }
        job_map[j.job_id] = &j;
    }

    // 3. Scan schedule slots & verify contiguous allocation of length C
    std::unordered_map<JobID, std::vector<SlotIndex>> assigned_slots;
    for (SlotIndex s = 0; s < schedule.horizon; ++s) {
        JobID j_id = schedule.slots[static_cast<size_t>(s)].job_id;
        if (j_id != -1) {
            auto it = job_map.find(j_id);
            if (it == job_map.end()) {
                res.error_message = "Slot " + std::to_string(s) + " assigned unknown job ID " + std::to_string(j_id);
                return res;
            }
            assigned_slots[j_id].push_back(s);
        }
    }

    // 4. Verify each assigned job's contiguous allocation, release, and deadline
    for (const auto& kv : assigned_slots) {
        JobID j_id = kv.first;
        const auto& slots_vec = kv.second;
        const Job* j = job_map[j_id];

        if (static_cast<SlotCount>(slots_vec.size()) != j->duration) {
            res.error_message = "Job " + std::to_string(j_id) + " assigned " +
                                std::to_string(slots_vec.size()) + " slots, but duration C=" +
                                std::to_string(j->duration);
            return res;
        }

        SlotIndex start = slots_vec.front();
        SlotIndex finish = start + j->duration;

        // Check contiguity
        for (size_t i = 0; i < slots_vec.size(); ++i) {
            if (slots_vec[i] != start + static_cast<SlotIndex>(i)) {
                res.error_message = "Job " + std::to_string(j_id) + " allocation is not contiguous";
                return res;
            }
        }

        // Release check
        if (start < j->release) {
            res.error_message = "Job " + std::to_string(j_id) + " start slot " + std::to_string(start) +
                                " is before physical release r=" + std::to_string(j->release);
            return res;
        }

        // Deadline check
        if (finish > j->absolute_deadline) {
            res.error_message = "Job " + std::to_string(j_id) + " finish slot " + std::to_string(finish) +
                                " exceeds absolute deadline d=" + std::to_string(j->absolute_deadline);
            return res;
        }
    }

    // 5. Verify Precedence Dependencies
    std::string dep_err;
    if (!deps.verify_schedule_dependencies(schedule, all_jobs, dep_err)) {
        res.error_message = dep_err;
        return res;
    }

    res.valid = true;
    return res;
}

ValidationResult ScheduleValidator::verify_transition(
    const Schedule& initial_schedule,
    const Schedule& candidate_schedule,
    const std::vector<Job>& initial_jobs,
    const Job* new_job,
    const DependencyGraph& deps,
    const ValidationConfig& config
) {
    ValidationResult res;
    res.valid = false;
    res.moved_jobs_count = 0;

    // 1. Initial schedule feasibility
    ValidationResult init_v = verify_schedule(initial_schedule, initial_jobs, deps, config.t_now);
    if (!init_v.valid) {
        res.error_message = "Initial schedule is invalid: " + init_v.error_message;
        return res;
    }

    // 2. Candidate schedule feasibility
    std::vector<Job> full_jobs = initial_jobs;
    if (new_job) {
        for (const auto& j : initial_jobs) {
            if (j.job_id == new_job->job_id) {
                res.error_message = "New job ID " + std::to_string(new_job->job_id) + " collides with existing job";
                return res;
            }
        }
        full_jobs.push_back(*new_job);
    }

    ValidationResult cand_v = verify_schedule(candidate_schedule, full_jobs, deps, config.t_now);
    if (!cand_v.valid) {
        res.error_message = "Candidate schedule is invalid: " + cand_v.error_message;
        return res;
    }

    // 3. Horizon matching
    if (initial_schedule.horizon != candidate_schedule.horizon) {
        res.error_message = "Horizon mismatch: initial=" + std::to_string(initial_schedule.horizon) +
                            ", candidate=" + std::to_string(candidate_schedule.horizon);
        return res;
    }

    // 4. Past Immutability Check: slots s < t_now must be byte-for-byte identical
    for (SlotIndex s = 0; s < config.t_now && s < initial_schedule.horizon; ++s) {
        if (initial_schedule.slots[static_cast<size_t>(s)].job_id !=
            candidate_schedule.slots[static_cast<size_t>(s)].job_id) {
            res.error_message = "Past slot " + std::to_string(s) + " (< t_now=" + std::to_string(config.t_now) +
                                ") modified: initial job=" + std::to_string(initial_schedule.slots[static_cast<size_t>(s)].job_id) +
                                ", candidate job=" + std::to_string(candidate_schedule.slots[static_cast<size_t>(s)].job_id);
            return res;
        }
    }

    // Executing jobs immutability: jobs whose allocation started at b < t_now cannot move
    std::unordered_map<JobID, const Job*> initial_job_map;
    for (const auto& j : initial_jobs) {
        initial_job_map[j.job_id] = &j;
    }

    for (const auto& kv : initial_schedule.job_to_start) {
        JobID j_id = kv.first;
        SlotIndex init_start = kv.second;
        if (init_start < config.t_now) { // Job execution started or finished in the past
            SlotIndex cand_start = candidate_schedule.get_job_start(j_id);
            if (cand_start != init_start) {
                res.error_message = "Job " + std::to_string(j_id) + " started in past (start=" +
                                    std::to_string(init_start) + " < t_now=" + std::to_string(config.t_now) +
                                    ") and cannot be moved";
                return res;
            }
        }
    }

    // 5. Preserved Jobs Check: Every initial job present in initial_schedule must be present in candidate_schedule
    for (const auto& kv : initial_schedule.job_to_start) {
        JobID j_id = kv.first;
        if (candidate_schedule.get_job_start(j_id) < 0) {
            res.error_message = "Previously accepted job ID " + std::to_string(j_id) + " was silently dropped!";
            return res;
        }
    }

    // 6. New Job Requirement
    if (new_job && config.require_new_job) {
        if (candidate_schedule.get_job_start(new_job->job_id) < 0) {
            res.error_message = "New job ID " + std::to_string(new_job->job_id) + " was not admitted";
            return res;
        }
    }

    // 7. Count Reassigned (Moved) Existing Jobs (Each distinct moved job counts ONCE toward K)
    SlotCount moved_count = 0;
    for (const auto& kv : initial_schedule.job_to_start) {
        JobID j_id = kv.first;
        SlotIndex old_s = kv.second;
        SlotIndex new_s = candidate_schedule.get_job_start(j_id);
        if (old_s != new_s) {
            moved_count++;
        }
    }

    res.moved_jobs_count = moved_count;

    // 8. Recourse Bound Check: moved_count <= max_K
    if (moved_count > config.max_K) {
        res.error_message = "Recourse bound violated: moved " + std::to_string(moved_count) +
                            " jobs, exceeding K=" + std::to_string(config.max_K);
        return res;
    }

    res.valid = true;
    return res;
}

} // namespace bcss
