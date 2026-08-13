#include "bcss/metrics.hpp"
#include <iostream>

namespace bcss {

bool MetricsLogger::export_summary_csv(const std::string& filepath, const std::vector<ExperimentMetrics>& metrics_list) {
    std::ofstream csv(filepath);
    if (!csv.is_open()) return false;

    csv << "periodic_jobs,periodic_misses,sporadic_streams,sporadic_arrivals,sporadic_accepted,"
        << "sporadic_rejected,oneshot_arrivals,oneshot_accepted,oneshot_rejected,"
        << "direct_allocs,reclaims,compensations,moved_jobs,max_k,max_delta_max,total_delta_total,"
        << "rtc_checks,rtc_passes,rtc_rejections,hash_failures,search_nodes,pruned_nodes,search_time_ns\n";

    for (const auto& m : metrics_list) {
        csv << m.total_periodic_jobs << "," << m.periodic_deadline_misses << ","
            << m.total_sporadic_streams << "," << m.sporadic_runtime_arrivals << ","
            << m.sporadic_accepted_jobs << "," << m.sporadic_rejected_jobs << ","
            << m.oneshot_arrivals << "," << m.oneshot_accepted << "," << m.oneshot_rejected << ","
            << m.direct_allocations << "," << m.sporadic_reclamations << "," << m.compensation_allocations << ","
            << m.total_moved_jobs << "," << m.max_observed_k << "," << m.max_observed_delta_max << ","
            << m.total_observed_delta_total << "," << m.rtc_checks << "," << m.rtc_passes << ","
            << m.rtc_rejections << "," << m.hash_consistency_failures << "," << m.total_search_nodes << ","
            << m.total_pruned_nodes << "," << m.total_search_time_ns << "\n";
    }

    csv.close();
    return true;
}

bool MetricsLogger::export_events_csv(const std::string& filepath, const std::vector<std::string>& event_logs) {
    std::ofstream csv(filepath);
    if (!csv.is_open()) return false;

    csv << "event_log\n";
    for (const auto& line : event_logs) {
        csv << "\"" << line << "\"\n";
    }

    csv.close();
    return true;
}

} // namespace bcss
