#include "comparisons/workload_adapter.hpp"
#include <algorithm>

namespace comparisons {

ComparisonJob WorkloadAdapter::convert_job(const bcss::Job& job, const bcss::DependencyGraph& deps) {
    ComparisonJob cj;
    cj.id = job.job_id;
    cj.task_id = job.task_id;

    switch (job.type) {
        case bcss::TaskType::Periodic: cj.type = TrafficType::Periodic; break;
        case bcss::TaskType::Sporadic: cj.type = TrafficType::Sporadic; break;
        case bcss::TaskType::OneShot:  cj.type = TrafficType::OneShot; break;
    }

    cj.release = job.release;
    cj.relative_deadline = job.relative_deadline;
    cj.absolute_deadline = job.absolute_deadline;
    cj.execution_requirement = job.duration;

    if (job.period > 0) cj.period = job.period;
    if (job.min_inter_arrival > 0) cj.min_interarrival = job.min_inter_arrival;
    cj.current_start = job.current_start;

    cj.predecessors = job.precedence_parents;
    auto dep_it = deps.in_edges.find(job.job_id);
    if (dep_it != deps.in_edges.end()) {
        for (bcss::JobID parent_id : dep_it->second) {
            if (std::find(cj.predecessors.begin(), cj.predecessors.end(), parent_id) == cj.predecessors.end()) {
                cj.predecessors.push_back(parent_id);
            }
        }
        std::sort(cj.predecessors.begin(), cj.predecessors.end());
    }
    return cj;
}

NeutralBaselineSchedule WorkloadAdapter::convert_schedule(const bcss::Schedule& schedule) {
    NeutralBaselineSchedule nbs(schedule.horizon);
    for (SlotIndex s = 0; s < schedule.horizon; ++s) {
        const auto& bslot = schedule.slots[static_cast<size_t>(s)];
        auto& nslot = nbs.slots[static_cast<size_t>(s)];
        nslot.slot_index = bslot.slot_index;
        nslot.job_id = bslot.job_id;
        nslot.task_id = bslot.job_id; // Default task ID to job ID if not present in slot
        switch (bslot.type) {
            case bcss::TaskType::Periodic: nslot.type = TrafficType::Periodic; break;
            case bcss::TaskType::Sporadic: nslot.type = TrafficType::Sporadic; break;
            case bcss::TaskType::OneShot:  nslot.type = TrafficType::OneShot; break;
        }
        nslot.release = bslot.release;
        nslot.deadline = bslot.deadline;
    }
    return nbs;
}

NeutralWorkload WorkloadAdapter::convert_taskset(const bcss::GeneratedTaskset& taskset) {
    NeutralWorkload nw;
    nw.horizon = taskset.config.horizon;

    for (const auto& pj : taskset.periodic_jobs) {
        nw.periodic_jobs.push_back(convert_job(pj, taskset.dependencies));
    }

    for (const auto& ss : taskset.sporadic_streams) {
        SporadicStreamDefinition ssd;
        ssd.stream_id = ss.task_id;
        ssd.min_interarrival = ss.min_inter_arrival;
        ssd.execution_requirement = ss.duration;
        ssd.relative_deadline = ss.relative_deadline;
        nw.sporadic_streams.push_back(ssd);
    }

    for (const auto& da : taskset.dynamic_arrivals) {
        nw.dynamic_arrivals.push_back(convert_job(da, taskset.dependencies));
    }

    return nw;
}

} // namespace comparisons
