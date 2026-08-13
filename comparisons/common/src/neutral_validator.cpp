#include "comparisons/neutral_validator.hpp"
#include <map>

namespace comparisons {

ValidationResult NeutralValidator::validate_schedule(
    const NeutralBaselineSchedule& sched,
    const std::vector<ComparisonJob>& all_jobs,
    SlotIndex t_now
) {
    (void)t_now;
    SlotCount H = sched.horizon;

    std::map<JobID, std::pair<SlotIndex, SlotCount>> job_allocations;

    for (SlotIndex s = 0; s < H; ++s) {
        const auto& slot = sched.slots[static_cast<size_t>(s)];
        if (!slot.is_free()) {
            if (job_allocations.find(slot.job_id) == job_allocations.end()) {
                job_allocations[slot.job_id] = {s, 1};
            } else {
                job_allocations[slot.job_id].second++;
            }
        }
    }

    std::map<JobID, ComparisonJob> job_map;
    for (const auto& j : all_jobs) {
        job_map[j.id] = j;
    }

    for (const auto& [jid, alloc] : job_allocations) {
        SlotIndex start = alloc.first;
        SlotCount count = alloc.second;

        auto it = job_map.find(jid);
        if (it == job_map.end()) continue;
        const auto& job = it->second;

        if (start < job.release) {
            return {false, "Job " + std::to_string(jid) + " start " + std::to_string(start) + " < release " + std::to_string(job.release)};
        }
        if (start + count > job.absolute_deadline) {
            return {false, "Job " + std::to_string(jid) + " completion " + std::to_string(start + count) + " > deadline " + std::to_string(job.absolute_deadline)};
        }
        if (count != job.execution_requirement) {
            return {false, "Job " + std::to_string(jid) + " allocated " + std::to_string(count) + " slots != required C=" + std::to_string(job.execution_requirement)};
        }
    }

    // Precedence constraints
    for (const auto& job : all_jobs) {
        if (job_allocations.count(job.id) == 0) continue;
        SlotIndex my_start = job_allocations[job.id].first;

        for (JobID pred_id : job.predecessors) {
            if (job_allocations.count(pred_id) > 0) {
                const auto& pred_alloc = job_allocations[pred_id];
                SlotIndex pred_finish = pred_alloc.first + pred_alloc.second;
                if (pred_finish > my_start) {
                    return {false, "Precedence violation: Pred " + std::to_string(pred_id) + " finish " + std::to_string(pred_finish) + " > Job " + std::to_string(job.id) + " start " + std::to_string(my_start)};
                }
            }
        }
    }

    return {true, "VALID"};
}

} // namespace comparisons
