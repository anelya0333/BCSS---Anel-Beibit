#ifndef BCSS_SEARCH_HPP
#define BCSS_SEARCH_HPP

#include "bcss/types.hpp"
#include "bcss/schedule.hpp"
#include <vector>
#include <cstdint>

namespace bcss {

struct PathCandidate {
    std::vector<JobID> jobs;    // Ordered list of jobs placed in path (0: new_job, 1..n: displaced jobs)
    std::vector<SlotIndex> starts; // Assigned start slot for each job in `jobs`
    std::vector<JobID> pending; // Queue of displaced jobs waiting for new slot placement

    SlotCount k{0};          // Exact number of distinct moved pre-existing jobs
    SlotCount max_disp{0};   // Maximum displacement delta_max = max |b'_j - b_j|
    SlotCount total_disp{0}; // Total displacement delta_total = sum |b'_j - b_j|

    // Lexicographical 3-Stage Candidate Ranking Key: (k, max_disp, total_disp)
    bool operator<(const PathCandidate& other) const {
        if (k != other.k) return k < other.k;
        if (max_disp != other.max_disp) return max_disp < other.max_disp;
        return total_disp < other.total_disp;
    }
};

struct SearchStats {
    uint64_t nodes_expanded{0};
    uint64_t candidates_generated{0};
    uint64_t candidates_found{0};
    uint64_t candidates_feasible{0};
    uint64_t paths_pruned{0};
    uint64_t candidates_rejected_by_validator{0};
    uint64_t candidates_rejected_by_rtc{0};
    uint64_t rtc_checks{0};
    uint64_t rtc_unsafe{0};
    uint64_t max_search_depth{0};
    uint64_t search_time_ns{0};
};

} // namespace bcss

#endif // BCSS_SEARCH_HPP
