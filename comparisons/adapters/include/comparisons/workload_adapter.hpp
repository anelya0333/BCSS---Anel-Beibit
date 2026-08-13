#ifndef COMPARISONS_WORKLOAD_ADAPTER_HPP
#define COMPARISONS_WORKLOAD_ADAPTER_HPP

#include "comparisons/neutral_model.hpp"
#include "bcss/workload.hpp"
#include "bcss/schedule.hpp"

namespace comparisons {

class WorkloadAdapter {
public:
    static NeutralWorkload convert_taskset(const bcss::GeneratedTaskset& taskset);
    static NeutralBaselineSchedule convert_schedule(const bcss::Schedule& schedule);
    static ComparisonJob convert_job(const bcss::Job& job, const bcss::DependencyGraph& deps);
};

} // namespace comparisons

#endif // COMPARISONS_WORKLOAD_ADAPTER_HPP
