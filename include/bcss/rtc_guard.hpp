#ifndef BCSS_RTC_GUARD_HPP
#define BCSS_RTC_GUARD_HPP

#include "types.hpp"
#include "schedule.hpp"
#include "dependencies.hpp"
#include <vector>
#include <string>

namespace bcss {

struct SporadicStreamSpec {
    TaskID task_id{-1};
    SlotCount min_inter_arrival{1}; // T_min
    SlotCount duration{1};           // C >= 1
    SlotCount relative_deadline{1};  // D
};

class RtcEnvelopeGuard {
public:
    // Calculates conservative arrival curve alpha(delta_t) using integer ceil division
    static SlotCount arrival_curve(const std::vector<SporadicStreamSpec>& streams, SlotCount delta_t);

    // Sliding-window capacity protection verification over future intervals
    static bool check_guard(
        const Schedule& candidate_schedule,
        SlotIndex t_now,
        const std::vector<SporadicStreamSpec>& streams,
        const DependencyGraph& deps,
        std::string& rejection_reason
    );
};

// Independent Brute-Force RTC Reference Oracle (Section 2)
// Enumerates ALL legal sporadic arrival sequences respecting T_min over [t_now, H).
// For each legal sequence, verifies whether jobs are EDF/backtrack schedulable on candidate_schedule.
class IndependentRtcOracle {
public:
    static bool is_safe(
        const Schedule& candidate_schedule,
        SlotIndex t_now,
        const std::vector<SporadicStreamSpec>& streams
    );
};

// Brute-force small-state reference checker
class RtcSmallStateOracle {
public:
    static bool is_future_sequence_safe(
        const Schedule& candidate_schedule,
        SlotIndex t_now,
        const std::vector<SporadicStreamSpec>& streams
    );
};

} // namespace bcss

#endif // BCSS_RTC_GUARD_HPP
