#ifndef BCSS_DEPENDENCIES_HPP
#define BCSS_DEPENDENCIES_HPP

#include "types.hpp"
#include "schedule.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace bcss {

class DependencyGraph {
public:
    std::unordered_map<JobID, std::vector<JobID>> adj_list{};       // parent -> children (A -> B)
    std::unordered_map<JobID, std::vector<JobID>> in_edges{};       // child -> parents (B <- A)
    std::unordered_set<JobID> all_nodes{};

    DependencyGraph() = default;

    bool add_dependency(JobID parent_id, JobID child_id);
    bool has_cycle() const;
    bool validate_dag(std::string& error_msg) const;

    // Computes virtual release r' and virtual deadline d' for all jobs
    void compute_virtual_bounds(std::vector<Job>& jobs) const;

    // Direct exact schedule verification: finish(A) <= start(B) for all A -> B
    bool verify_schedule_dependencies(
        const Schedule& sched,
        const std::vector<Job>& jobs,
        std::string& error_msg
    ) const;
};

} // namespace bcss

#endif // BCSS_DEPENDENCIES_HPP
