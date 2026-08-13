#include "bcss/rtc_guard.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <functional>

namespace bcss {

// ============================================================================
// ARRIVAL / DEMAND BOUND CURVE: α(Δ)
// ============================================================================
//
// Mathematically:
// 1. Demand Bound Function DBF(Δ): Upper bound on workload that ARRIVES AND MUST
//    COMPLETE within window Δ. For a sporadic stream with relative deadline D_i,
//    no job released inside a window of length Δ < D_i is constrained to finish
//    within that window. Therefore, demand constrained to complete within Δ requires Δ ≥ D_i.
//    DBF(Δ) = Σ_{i : Δ ≥ D_i} ⌈Δ / T_min_i⌉ × C_i
//
// 2. Horizon Boundary Edge Case: For intervals [t, H) extending to the scheduling horizon H,
//    the effective relative deadline cannot exceed the remaining horizon (H - t). Any job
//    released at t that executes within [t, H) has effective deadline D_i^eff = min(D_i, H - t).
//    However, a stream cannot demand more slots than the window length (Δ ≥ C_i).
//
SlotCount RtcEnvelopeGuard::arrival_curve(const std::vector<SporadicStreamSpec>& streams, SlotCount delta_t) {
    if (delta_t <= 0) return 0;
    SlotCount total_demand = 0;
    for (const auto& s : streams) {
        if (s.min_inter_arrival <= 0) continue;
        if (delta_t < s.relative_deadline) continue; // Demand bounded by relative deadline D_i
        SlotCount arrivals = (delta_t + s.min_inter_arrival - 1) / s.min_inter_arrival;
        total_demand += arrivals * s.duration;
    }
    return total_demand;
}

// Helper for horizon-boundary demand calculation
static SlotCount horizon_boundary_demand(const std::vector<SporadicStreamSpec>& streams, SlotCount delta_t) {
    if (delta_t <= 0) return 0;
    SlotCount total_demand = 0;
    for (const auto& s : streams) {
        if (s.min_inter_arrival <= 0) continue;
        if (delta_t < s.duration) continue; // A job of duration C_i cannot fit in window Δ < C_i
        SlotCount effective_D = std::min(s.relative_deadline, delta_t);
        if (delta_t < effective_D) continue;
        SlotCount arrivals = (delta_t + s.min_inter_arrival - 1) / s.min_inter_arrival;
        total_demand += arrivals * s.duration;
    }
    return total_demand;
}

// ============================================================================
// CHECK_GUARD: Sliding-window capacity protection verification
// ============================================================================
bool RtcEnvelopeGuard::check_guard(
    const Schedule& candidate_schedule,
    SlotIndex t_now,
    const std::vector<SporadicStreamSpec>& streams,
    const DependencyGraph& deps,
    std::string& rejection_reason
) {
    (void)deps;
    if (streams.empty()) return true;

    SlotCount horizon = candidate_schedule.horizon;

    // Precompute free-slot prefix counts. The original nested scan evaluated
    // every interval. Demand is monotone and capacity within a fixed-start
    // interval is also monotone, so only the first delta of each new demand
    // level can bind. This is an exact evaluation-order optimization: it does
    // not change the conservative demand bound or either safety condition.
    std::vector<SlotCount> free_prefix(static_cast<size_t>(horizon + 1), 0);
    for (SlotIndex slot = 0; slot < horizon; ++slot) {
        free_prefix[static_cast<size_t>(slot + 1)] =
            free_prefix[static_cast<size_t>(slot)] +
            (candidate_schedule.slots[static_cast<size_t>(slot)].is_free() ? 1 : 0);
    }

    SlotCount maximum_duration = 1;
    for (const auto& stream : streams) {
        maximum_duration = std::max(maximum_duration, stream.duration);
    }

    // A window contains a contiguous free block of maximum_duration iff the
    // next such block start lies no later than end - maximum_duration.
    const SlotIndex sentinel = horizon + 1;
    std::vector<SlotIndex> next_free_block(static_cast<size_t>(horizon + 1), sentinel);
    if (maximum_duration > 1) {
        SlotCount run = 0;
        std::vector<bool> block_start(static_cast<size_t>(horizon), false);
        for (SlotIndex slot = horizon - 1; slot >= 0; --slot) {
            if (candidate_schedule.slots[static_cast<size_t>(slot)].is_free()) ++run;
            else run = 0;
            if (run >= maximum_duration) block_start[static_cast<size_t>(slot)] = true;
        }
        SlotIndex next = sentinel;
        for (SlotIndex slot = horizon - 1; slot >= 0; --slot) {
            if (block_start[static_cast<size_t>(slot)]) next = slot;
            next_free_block[static_cast<size_t>(slot)] = next;
        }
    }

    std::vector<SlotCount> demand(static_cast<size_t>(horizon + 1), 0);
    std::vector<SlotCount> demand_change_deltas;
    SlotCount previous_demand = 0;
    for (SlotCount delta = 1; delta <= horizon; ++delta) {
        demand[static_cast<size_t>(delta)] = arrival_curve(streams, delta);
        if (demand[static_cast<size_t>(delta)] != previous_demand) {
            demand_change_deltas.push_back(delta);
            previous_demand = demand[static_cast<size_t>(delta)];
        }
    }

    auto interval_is_safe = [&](SlotIndex start, SlotIndex end, SlotCount required) -> bool {
        const SlotCount free_slots = free_prefix[static_cast<size_t>(end)] -
                                     free_prefix[static_cast<size_t>(start)];
        if (free_slots < required) {
            rejection_reason = "RTC Guard Violation: Interval [" + std::to_string(start) +
                               ", " + std::to_string(end) + ") has " + std::to_string(free_slots) +
                               " free slots, below required sporadic demand " + std::to_string(required);
            return false;
        }
        if (maximum_duration > 1 && required > 0) {
            const SlotIndex latest_block_start = end - maximum_duration;
            const SlotIndex next_block = next_free_block[static_cast<size_t>(start)];
            if (next_block > latest_block_start) {
                rejection_reason = "RTC Multi-Slot Violation: Interval [" + std::to_string(start) +
                                   ", " + std::to_string(end) + ") has no contiguous block of C=" +
                                   std::to_string(maximum_duration);
                return false;
            }
        }
        return true;
    };

    // Standard (non-horizon-ending) intervals at each demand increase.
    for (SlotCount delta : demand_change_deltas) {
        const SlotCount required = demand[static_cast<size_t>(delta)];
        for (SlotIndex start = t_now; start + delta < horizon; ++start) {
            if (!interval_is_safe(start, start + delta, required)) return false;
        }
    }

    // Horizon-ending intervals use the implementation's distinct conservative
    // boundary demand and therefore must always be evaluated separately.
    for (SlotIndex start = t_now; start < horizon; ++start) {
        const SlotCount delta = horizon - start;
        const SlotCount required = horizon_boundary_demand(streams, delta);
        if (!interval_is_safe(start, horizon, required)) return false;
    }

    return true;
}

// ============================================================================
// INDEPENDENT BRUTE-FORCE RTC REFERENCE ORACLE
// ============================================================================

struct LegalInstance {
    TaskID task_id;
    SlotIndex release;
    SlotIndex deadline;
    SlotCount duration;
};

static void generate_single_stream_sequences(
    const SporadicStreamSpec& str,
    SlotIndex t_now,
    SlotCount horizon,
    SlotIndex last_release,
    std::vector<LegalInstance>& current,
    std::vector<std::vector<LegalInstance>>& out
) {
    out.push_back(current);

    SlotIndex start_r = (last_release < 0) ? t_now : (last_release + str.min_inter_arrival);
    for (SlotIndex r = start_r; r < horizon; ++r) {
        SlotIndex d = r + str.relative_deadline;
        if (r + str.duration <= horizon) {
            current.push_back({str.task_id, r, d, str.duration});
            generate_single_stream_sequences(str, t_now, horizon, r, current, out);
            current.pop_back();
        }
    }
}

static void combine_stream_sequences(
    const std::vector<std::vector<std::vector<LegalInstance>>>& per_stream_seqs,
    size_t stream_idx,
    std::vector<LegalInstance>& current_combined,
    std::vector<std::vector<LegalInstance>>& out_all
) {
    if (stream_idx == per_stream_seqs.size()) {
        out_all.push_back(current_combined);
        return;
    }

    for (const auto& seq : per_stream_seqs[stream_idx]) {
        size_t old_size = current_combined.size();
        current_combined.insert(current_combined.end(), seq.begin(), seq.end());
        combine_stream_sequences(per_stream_seqs, stream_idx + 1, current_combined, out_all);
        current_combined.resize(old_size);
    }
}

static bool is_sequence_schedulable(
    const Schedule& base_sched,
    SlotIndex t_now,
    const std::vector<LegalInstance>& arrivals
) {
    if (arrivals.empty()) return true;

    std::vector<LegalInstance> sorted_arrivals = arrivals;
    std::sort(sorted_arrivals.begin(), sorted_arrivals.end(), [](const LegalInstance& a, const LegalInstance& b) {
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        return a.release < b.release;
    });

    std::function<bool(size_t, Schedule)> try_place = [&](size_t idx, Schedule sched) -> bool {
        if (idx == sorted_arrivals.size()) return true;

        const auto& inst = sorted_arrivals[idx];
        SlotIndex start_search = std::max(t_now, inst.release);
        SlotIndex max_start = std::min(sched.horizon - inst.duration, inst.deadline - inst.duration);

        for (SlotIndex s = start_search; s <= max_start; ++s) {
            if (sched.is_range_free(s, inst.duration)) {
                Schedule next_sched = sched;
                Job j(inst.task_id, static_cast<JobID>(9000 + static_cast<int64_t>(idx)), TaskType::Sporadic,
                      inst.release, inst.deadline - inst.release, inst.duration, -1, -1);
                next_sched.assign_job(j, s);
                if (try_place(idx + 1, next_sched)) return true;
            }
        }
        return false;
    };

    return try_place(0, base_sched);
}

bool IndependentRtcOracle::is_safe(
    const Schedule& candidate_schedule,
    SlotIndex t_now,
    const std::vector<SporadicStreamSpec>& streams
) {
    if (streams.empty()) return true;

    SlotCount horizon = candidate_schedule.horizon;

    std::vector<std::vector<std::vector<LegalInstance>>> per_stream_seqs;
    for (const auto& str : streams) {
        std::vector<std::vector<LegalInstance>> stream_seqs;
        std::vector<LegalInstance> current;
        generate_single_stream_sequences(str, t_now, horizon, -1, current, stream_seqs);
        per_stream_seqs.push_back(stream_seqs);
    }

    std::vector<std::vector<LegalInstance>> all_combined;
    std::vector<LegalInstance> combined_current;
    combine_stream_sequences(per_stream_seqs, 0, combined_current, all_combined);

    for (const auto& seq : all_combined) {
        if (!is_sequence_schedulable(candidate_schedule, t_now, seq)) {
            return false;
        }
    }

    return true;
}

bool RtcSmallStateOracle::is_future_sequence_safe(
    const Schedule& candidate_schedule,
    SlotIndex t_now,
    const std::vector<SporadicStreamSpec>& streams
) {
    return IndependentRtcOracle::is_safe(candidate_schedule, t_now, streams);
}

} // namespace bcss
