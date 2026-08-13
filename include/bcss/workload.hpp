#ifndef BCSS_WORKLOAD_HPP
#define BCSS_WORKLOAD_HPP

#include "types.hpp"
#include "schedule.hpp"
#include "dependencies.hpp"
#include "rtc_guard.hpp"
#include <vector>
#include <random>
#include <string>

namespace bcss {

enum class WorkloadProfile {
    Quiet,
    Normal,
    Busy,
    Worst
};

struct WorkloadConfig {
    SlotCount horizon{64};
    double periodic_load{0.50};
    double sporadic_ratio{0.20};
    double oneshot_ratio{0.30};
    uint64_t seed{42};
    uint64_t trace_seed{0}; // 0 derives the trace deterministically from seed
    std::string tightness{"medium"}; // tight, medium, loose
    WorkloadProfile profile{WorkloadProfile::Normal};
    bool allow_multi_slot{true};
    SlotCount max_duration{3};
    std::string multislot_regime{"uniform"}; // c1, c2, mixed, heavy, uniform
    double dependency_density{0.15};
    SlotCount periodic_stream_count{70};
    SlotCount candidate_sporadic_stream_count{-1};
    double candidate_sporadic_utilization{-1.0};
    double sporadic_deadline_ratio{-1.0};
    double oneshot_deadline_ratio{-1.0};
    bool synchronized_arrivals{false};
};

struct GeneratedTaskset {
    WorkloadConfig config{};
    std::vector<Job> periodic_jobs{};
    Schedule baseline_schedule{};
    std::vector<SporadicStreamSpec> sporadic_streams{};
    std::vector<Job> dynamic_arrivals{};
    DependencyGraph dependencies{};
};

class WorkloadGenerator {
public:
    static GeneratedTaskset generate(const WorkloadConfig& config);
};

} // namespace bcss

#endif // BCSS_WORKLOAD_HPP
