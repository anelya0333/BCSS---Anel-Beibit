#ifndef BCSS_VALIDATOR_HPP
#define BCSS_VALIDATOR_HPP

#include "types.hpp"
#include "schedule.hpp"
#include "dependencies.hpp"
#include <string>
#include <vector>

namespace bcss {

struct ValidationResult {
    bool valid{false};
    std::string error_message{};
    SlotCount moved_jobs_count{0};
};

struct ValidationConfig {
    SlotIndex t_now{0};
    SlotCount max_K{0};
    bool require_new_job{true};
};

class ScheduleValidator {
public:
    // Core full-schedule static feasibility check
    static ValidationResult verify_schedule(
        const Schedule& schedule,
        const std::vector<Job>& all_jobs,
        const DependencyGraph& deps,
        SlotIndex t_now
    );

    // Differential schedule transition check (initial_schedule -> candidate_schedule)
    static ValidationResult verify_transition(
        const Schedule& initial_schedule,
        const Schedule& candidate_schedule,
        const std::vector<Job>& initial_jobs,
        const Job* new_job,
        const DependencyGraph& deps,
        const ValidationConfig& config
    );
};

} // namespace bcss

#endif // BCSS_VALIDATOR_HPP
