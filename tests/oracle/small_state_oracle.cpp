#include "small_state_oracle.hpp"
#include <functional>
#include <cmath>

namespace bcss {

OracleResult SmallStateOracle::solve_exhaustive(
    const Schedule& initial_schedule,
    const std::vector<Job>& initial_jobs,
    const Job& new_job,
    const DependencyGraph& deps,
    SlotIndex t_now,
    SlotCount max_K
) {
    OracleResult res;
    res.feasible = false;

    SlotCount H = initial_schedule.horizon;
    if (H > 8) return res; // Small-state verification limit

    std::vector<Job> full_jobs = initial_jobs;
    full_jobs.push_back(new_job);

    std::unordered_map<JobID, SlotIndex> initial_starts;
    for (const auto& kv : initial_schedule.job_to_start) {
        initial_starts[kv.first] = kv.second;
    }
    initial_starts[new_job.job_id] = -1;

    // Enumerate all feasible schedule arrangements
    Schedule empty_sched(H);

    std::function<void(size_t, Schedule, SlotCount)> backtrack = [&](size_t idx, Schedule current, SlotCount moved_so_far) {
        if (moved_so_far > max_K) return;

        if (idx == full_jobs.size()) {
            ValidationConfig vconfig{t_now, max_K, true};
            ValidationResult vres = ScheduleValidator::verify_transition(initial_schedule, current, initial_jobs, &new_job, deps, vconfig);
            if (vres.valid) {
                // Compute displacement metrics
                SlotCount max_disp = 0;
                SlotCount total_disp = 0;
                for (const auto& j : initial_jobs) {
                    SlotIndex old_s = initial_starts[j.job_id];
                    SlotIndex new_s = current.get_job_start(j.job_id);
                    if (old_s != -1 && new_s != -1) {
                        SlotCount disp = std::abs(new_s - old_s);
                        max_disp = std::max(max_disp, disp);
                        total_disp += disp;
                    }
                }

                if (!res.feasible || vres.moved_jobs_count < res.min_k ||
                    (vres.moved_jobs_count == res.min_k && max_disp < res.min_delta_max) ||
                    (vres.moved_jobs_count == res.min_k && max_disp == res.min_delta_max && total_disp < res.min_delta_total)) {
                    res.feasible = true;
                    res.best_schedule = current;
                    res.min_k = vres.moved_jobs_count;
                    res.min_delta_max = max_disp;
                    res.min_delta_total = total_disp;
                }
            }
            return;
        }

        const Job& j = full_jobs[idx];
        SlotIndex start_s = std::max(t_now, j.release);
        SlotIndex end_s = std::min(H, j.absolute_deadline) - j.duration + 1;

        for (SlotIndex s = start_s; s < end_s; ++s) {
            if (current.is_range_free(s, j.duration)) {
                Schedule next_sched = current;
                next_sched.assign_job(j, s);

                SlotIndex orig_s = initial_starts[j.job_id];
                SlotCount add_cost = (orig_s != -1 && orig_s != s) ? 1 : 0;

                backtrack(idx + 1, next_sched, moved_so_far + add_cost);
            }
        }
    };

    backtrack(0, empty_sched, 0);
    return res;
}

} // namespace bcss
