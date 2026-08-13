#ifndef BCSS_METRICS_HPP
#define BCSS_METRICS_HPP

#include "types.hpp"
#include "scheduler.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace bcss {

struct ExperimentMetrics {
    uint64_t total_periodic_jobs{0};
    uint64_t periodic_deadline_misses{0};

    uint64_t total_sporadic_streams{0};
    uint64_t sporadic_runtime_arrivals{0};
    uint64_t sporadic_accepted_jobs{0};
    uint64_t sporadic_rejected_jobs{0};

    uint64_t oneshot_arrivals{0};
    uint64_t oneshot_accepted{0};
    uint64_t oneshot_rejected{0};

    uint64_t direct_allocations{0};
    uint64_t sporadic_reclamations{0};
    uint64_t compensation_allocations{0};

    SlotCount total_moved_jobs{0};
    SlotCount max_observed_k{0};
    SlotCount max_observed_delta_max{0};
    SlotCount total_observed_delta_total{0};

    uint64_t rtc_checks{0};
    uint64_t rtc_passes{0};
    uint64_t rtc_rejections{0};

    uint64_t hash_consistency_failures{0};

    uint64_t total_search_nodes{0};
    uint64_t total_pruned_nodes{0};
    uint64_t total_search_time_ns{0};
};

class MetricsLogger {
public:
    static bool export_summary_csv(const std::string& filepath, const std::vector<ExperimentMetrics>& metrics_list);
    static bool export_events_csv(const std::string& filepath, const std::vector<std::string>& event_logs);
};

} // namespace bcss

#endif // BCSS_METRICS_HPP
