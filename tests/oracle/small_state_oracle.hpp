#ifndef BCSS_SMALL_STATE_ORACLE_HPP
#define BCSS_SMALL_STATE_ORACLE_HPP

#include "bcss/types.hpp"
#include "bcss/schedule.hpp"
#include "bcss/dependencies.hpp"
#include "bcss/validator.hpp"
#include <vector>

namespace bcss {

struct OracleResult {
    bool feasible{false};
    Schedule best_schedule{};
    SlotCount min_k{0};
    SlotCount min_delta_max{0};
    SlotCount min_delta_total{0};
};

class SmallStateOracle {
public:
    static OracleResult solve_exhaustive(
        const Schedule& initial_schedule,
        const std::vector<Job>& initial_jobs,
        const Job& new_job,
        const DependencyGraph& deps,
        SlotIndex t_now,
        SlotCount max_K
    );
};

} // namespace bcss

#endif // BCSS_SMALL_STATE_ORACLE_HPP
