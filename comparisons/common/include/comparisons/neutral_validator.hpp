#ifndef COMPARISONS_NEUTRAL_VALIDATOR_HPP
#define COMPARISONS_NEUTRAL_VALIDATOR_HPP

#include "comparisons/neutral_model.hpp"
#include <string>

namespace comparisons {

struct ValidationResult {
    bool valid{true};
    std::string reason{"VALID"};
};

class NeutralValidator {
public:
    static ValidationResult validate_schedule(
        const NeutralBaselineSchedule& sched,
        const std::vector<ComparisonJob>& all_jobs,
        SlotIndex t_now
    );
};

} // namespace comparisons

#endif // COMPARISONS_NEUTRAL_VALIDATOR_HPP
