#include "bcss/workload.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace bcss {

GeneratedTaskset WorkloadGenerator::generate(const WorkloadConfig& config) {
    GeneratedTaskset taskset;
    taskset.config = config;

    std::mt19937_64 rng(config.seed);
    const uint64_t effective_trace_seed = config.trace_seed == 0
        ? (config.seed ^ UINT64_C(0x9e3779b97f4a7c15))
        : config.trace_seed;
    std::mt19937_64 trace_rng(effective_trace_seed);
    SlotCount H = config.horizon;

    // 1. Generate Periodic TT Tasks and Initial Schedule
    SlotCount target_tt_slots = static_cast<SlotCount>(static_cast<double>(H) * config.periodic_load);
    taskset.baseline_schedule = Schedule(H);

    std::vector<SlotCount> candidate_periods = {5, 10, 20, 50};
    std::uniform_int_distribution<size_t> p_dist(0, candidate_periods.size() - 1);
    std::uniform_int_distribution<SlotCount> dur_dist(1, config.allow_multi_slot ? config.max_duration : 1);
    auto draw_duration = [&](std::mt19937_64& engine) -> SlotCount {
        if (!config.allow_multi_slot || config.max_duration <= 1 || config.multislot_regime == "c1") return 1;
        if (config.multislot_regime == "c2") {
            std::bernoulli_distribution choose_two(0.70);
            return std::min<SlotCount>(config.max_duration, choose_two(engine) ? 2 : 1);
        }
        if (config.multislot_regime == "mixed" || config.multislot_regime == "heavy") {
            std::uniform_int_distribution<int> pct(1, 100);
            const int value = pct(engine);
            if (config.multislot_regime == "mixed") {
                if (value <= 60) return 1;
                if (value <= 90) return std::min<SlotCount>(2, config.max_duration);
                return std::min<SlotCount>(4, config.max_duration);
            }
            if (value <= 20) return 1;
            if (value <= 60) return std::min<SlotCount>(2, config.max_duration);
            return std::min<SlotCount>(4, config.max_duration);
        }
        return dur_dist(engine);
    };

    SlotCount placed_slots = 0;
    TaskID task_id = 0;
    JobID job_id = 0;

    std::vector<SlotIndex> candidate_starts(static_cast<size_t>(H));
    for (SlotIndex s = 0; s < H; ++s) {
        candidate_starts[static_cast<size_t>(s)] = s;
    }
    std::shuffle(candidate_starts.begin(), candidate_starts.end(), rng);

    for (SlotIndex start : candidate_starts) {
        if (placed_slots >= target_tt_slots) break;
        SlotCount remaining = target_tt_slots - placed_slots;
        SlotCount period = std::min(candidate_periods[p_dist(rng)], H);
        SlotCount dur = std::min({draw_duration(rng), period, remaining});
        while (dur > 1 && !taskset.baseline_schedule.is_range_free(start, dur)) {
            --dur;
        }
        if (!taskset.baseline_schedule.is_range_free(start, dur)) continue;

        SlotIndex rel = (start / period) * period;
        SlotCount rel_deadline = std::min(period, H - rel);
        if (start + dur > rel + rel_deadline) continue;

        const TaskID logical_task_id = config.periodic_stream_count > 0
            ? task_id % config.periodic_stream_count
            : task_id;
        Job j(logical_task_id, job_id, TaskType::Periodic, rel, rel_deadline, dur, period, -1);
        if (taskset.baseline_schedule.assign_job(j, start)) {
            j.current_start = start;
            j.original_start = start;
            taskset.periodic_jobs.push_back(j);
            placed_slots += dur;
            ++task_id;
            ++job_id;
        }
    }

    std::sort(taskset.periodic_jobs.begin(), taskset.periodic_jobs.end(), [](const Job& a, const Job& b) {
        if (a.current_start != b.current_start) return a.current_start < b.current_start;
        return a.job_id < b.job_id;
    });

    // 2. Generate Admitted Sporadic Streams
    SlotCount n_sporadic = config.candidate_sporadic_stream_count >= 0
        ? config.candidate_sporadic_stream_count
        : std::max<SlotCount>(1, static_cast<SlotCount>(static_cast<double>(H) * config.sporadic_ratio / 10.0));
    for (SlotCount i = 0; i < n_sporadic; ++i) {
        SlotCount dur = draw_duration(rng);
        SlotCount T_min = candidate_periods[p_dist(rng)];
        if (config.candidate_sporadic_utilization >= 0.0 && n_sporadic > 0) {
            const double per_stream = config.candidate_sporadic_utilization / static_cast<double>(n_sporadic);
            T_min = per_stream > 0.0
                ? std::max<SlotCount>(dur, static_cast<SlotCount>(std::ceil(static_cast<double>(dur) / per_stream)))
                : H + 1;
        }
        dur = std::min(dur, T_min);
        SlotCount deadline = T_min;
        if (config.sporadic_deadline_ratio > 0.0) {
            deadline = std::max<SlotCount>(dur, static_cast<SlotCount>(std::llround(config.sporadic_deadline_ratio * static_cast<double>(dur))));
        }
        SporadicStreamSpec spec{100 + i, T_min, dur, deadline};
        taskset.sporadic_streams.push_back(spec);
    }

    // 3. Generate Compliant Sporadic Instances & Online One-Shot Dynamic Arrivals
    // Dynamic identifiers must remain disjoint from every generated periodic
    // instance. Large thesis-scale horizons can contain far more than 2,000
    // periodic instances, so a fixed starting value corrupts the schedule by
    // aliasing existing jobs.
    JobID dyn_job_id = std::max<JobID>(2000, job_id);

    // A. Generate Compliant Sporadic Arrivals for Admitted Streams
    double activity_factor = 0.50;
    switch (config.profile) {
        case WorkloadProfile::Quiet: activity_factor = 0.25; break;
        case WorkloadProfile::Normal: activity_factor = 0.50; break;
        case WorkloadProfile::Busy: activity_factor = 0.80; break;
        case WorkloadProfile::Worst: activity_factor = 1.00; break;
    }

    for (const auto& stream : taskset.sporadic_streams) {
        SlotIndex r = 0;
        if (!config.synchronized_arrivals && stream.min_inter_arrival > 1) {
            std::uniform_int_distribution<SlotIndex> phase_dist(
                0,
                std::min<SlotIndex>(H - 1, stream.min_inter_arrival - 1)
            );
            r = phase_dist(trace_rng);
        }
        while (r + stream.duration <= H) {
            SlotCount D = std::min(H - r, stream.relative_deadline);
            if (D >= stream.duration) {
                Job sp_j(stream.task_id, dyn_job_id++, TaskType::Sporadic, r, D, stream.duration, -1, stream.min_inter_arrival);
                taskset.dynamic_arrivals.push_back(sp_j);
            }
            SlotCount target_iat = static_cast<SlotCount>(std::ceil(
                static_cast<double>(stream.min_inter_arrival) / activity_factor
            ));
            SlotCount max_extra = std::max<SlotCount>(0, 2 * (target_iat - stream.min_inter_arrival));
            std::uniform_int_distribution<SlotCount> extra_dist(0, max_extra);
            SlotCount extra = (config.profile == WorkloadProfile::Worst) ? 0 : extra_dist(trace_rng);
            r += stream.min_inter_arrival + extra;
        }
    }

    // B. Generate Online One-Shot Dynamic Arrivals
    SlotCount target_oneshot_slots = std::max<SlotCount>(
        0,
        static_cast<SlotCount>(std::llround(static_cast<double>(H) * config.oneshot_ratio))
    );
    std::uniform_int_distribution<SlotIndex> rel_dist(0, std::max<SlotIndex>(0, H - 2));

    SlotCount min_w = 1, max_w = 3;
    if (config.tightness == "medium") { min_w = 4; max_w = 8; }
    else if (config.tightness == "loose") { min_w = 9; max_w = 20; }
    std::uniform_int_distribution<SlotCount> win_dist(min_w, max_w);

    SlotCount offered_oneshot_slots = 0;
    SlotCount i = 0;
    while (offered_oneshot_slots < target_oneshot_slots) {
        SlotIndex r = config.synchronized_arrivals ? 0 : rel_dist(trace_rng);
        SlotCount w = win_dist(trace_rng);
        SlotCount remaining = target_oneshot_slots - offered_oneshot_slots;
        SlotCount dur = std::min({draw_duration(trace_rng), w, remaining});
        if (r + dur > H) {
            r = H - dur;
        }
        SlotCount D = config.oneshot_deadline_ratio > 0.0
            ? std::max<SlotCount>(dur, static_cast<SlotCount>(std::llround(config.oneshot_deadline_ratio * static_cast<double>(dur))))
            : std::max(dur, w);
        if (r + D > H) D = H - r;
        if (D < dur) D = dur;

        Job oj(1000 + i, dyn_job_id++, TaskType::OneShot, r, D, dur, -1, -1);
        taskset.dynamic_arrivals.push_back(oj);
        offered_oneshot_slots += dur;
        ++i;
    }

    // Sort dynamic arrivals by release time
    std::sort(taskset.dynamic_arrivals.begin(), taskset.dynamic_arrivals.end(), [](const Job& a, const Job& b) {
        if (a.release != b.release) return a.release < b.release;
        return a.job_id < b.job_id;
    });

    // 4. Generate Precedence Dependencies (DAG)
    if (config.dependency_density > 0.0 && taskset.periodic_jobs.size() >= 2) {
        size_t n_edges = static_cast<size_t>(static_cast<double>(taskset.periodic_jobs.size()) * config.dependency_density);
        for (size_t e = 0; e < n_edges; ++e) {
            std::uniform_int_distribution<size_t> node_dist(0, taskset.periodic_jobs.size() - 2);
            size_t p_idx = node_dist(rng);
            size_t c_idx = p_idx + 1; // Guarantees parent index < child index -> DAG (no cycles)
            const JobID parent_id = taskset.periodic_jobs[p_idx].job_id;
            const JobID child_id = taskset.periodic_jobs[c_idx].job_id;
            if (taskset.dependencies.add_dependency(parent_id, child_id)) {
                taskset.periodic_jobs[c_idx].precedence_parents.push_back(parent_id);
            }
        }
        taskset.dependencies.compute_virtual_bounds(taskset.periodic_jobs);
    }

    return taskset;
}

} // namespace bcss
