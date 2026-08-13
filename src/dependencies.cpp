#include "bcss/dependencies.hpp"
#include <queue>
#include <algorithm>
#include <iostream>

namespace bcss {

bool DependencyGraph::add_dependency(JobID parent_id, JobID child_id) {
    if (parent_id == child_id) {
        return false; // Self-dependency invalid
    }
    adj_list[parent_id].push_back(child_id);
    in_edges[child_id].push_back(parent_id);
    all_nodes.insert(parent_id);
    all_nodes.insert(child_id);
    return true;
}

bool DependencyGraph::has_cycle() const {
    std::unordered_map<JobID, int> state; // 0: Unvisited, 1: Visiting, 2: Visited
    for (JobID node : all_nodes) {
        state[node] = 0;
    }

    auto dfs = [&](auto self, JobID u) -> bool {
        state[u] = 1; // Visiting
        auto it = adj_list.find(u);
        if (it != adj_list.end()) {
            for (JobID v : it->second) {
                if (state[v] == 1) return true; // Cycle detected!
                if (state[v] == 0) {
                    if (self(self, v)) return true;
                }
            }
        }
        state[u] = 2; // Visited
        return false;
    };

    for (JobID node : all_nodes) {
        if (state[node] == 0) {
            if (dfs(dfs, node)) return true;
        }
    }
    return false;
}

bool DependencyGraph::validate_dag(std::string& error_msg) const {
    if (has_cycle()) {
        error_msg = "Cyclic dependency graph detected";
        return false;
    }
    return true;
}

void DependencyGraph::compute_virtual_bounds(std::vector<Job>& jobs) const {
    std::unordered_map<JobID, Job*> job_map;
    for (auto& j : jobs) {
        job_map[j.job_id] = &j;
        j.virtual_release = j.release;
        j.virtual_deadline = j.absolute_deadline;
    }

    // Forward pass: Virtual release r'_B = max(r_B, max_{A -> B}(start(A) + C_A))
    for (auto& j : jobs) {
        auto it = in_edges.find(j.job_id);
        if (it != in_edges.end()) {
            for (JobID parent_id : it->second) {
                auto p_it = job_map.find(parent_id);
                if (p_it != job_map.end() && p_it->second->is_assigned()) {
                    SlotIndex parent_finish = p_it->second->current_start + p_it->second->duration;
                    j.virtual_release = std::max(j.virtual_release, parent_finish);
                }
            }
        }
    }

    // Backward pass: Virtual deadline d'_A = min(d_A, min_{A -> B}(start(B) - C_A))
    for (auto& j : jobs) {
        auto it = adj_list.find(j.job_id);
        if (it != adj_list.end()) {
            for (JobID child_id : it->second) {
                auto c_it = job_map.find(child_id);
                if (c_it != job_map.end() && c_it->second->is_assigned()) {
                    SlotIndex child_start = c_it->second->current_start;
                    j.virtual_deadline = std::min(j.virtual_deadline, child_start);
                }
            }
        }
    }
}

bool DependencyGraph::verify_schedule_dependencies(
    const Schedule& sched,
    const std::vector<Job>& jobs,
    std::string& error_msg
) const {
    std::unordered_map<JobID, const Job*> job_map;
    for (const auto& j : jobs) {
        job_map[j.job_id] = &j;
    }

    for (const auto& kv : adj_list) {
        JobID parent_id = kv.first;
        SlotIndex parent_start = sched.get_job_start(parent_id);
        if (parent_start < 0) continue; // Parent not scheduled

        auto p_it = job_map.find(parent_id);
        if (p_it == job_map.end()) continue;
        SlotCount parent_duration = p_it->second->duration;
        SlotIndex parent_finish = parent_start + parent_duration;

        for (JobID child_id : kv.second) {
            SlotIndex child_start = sched.get_job_start(child_id);
            if (child_start < 0) continue; // Child not scheduled

            if (child_start < parent_finish) {
                error_msg = "Precedence dependency violation: Child job " + std::to_string(child_id) +
                            " starts at slot " + std::to_string(child_start) +
                            " before parent job " + std::to_string(parent_id) +
                            " finishes at slot " + std::to_string(parent_finish);
                return false;
            }
        }
    }

    return true;
}

} // namespace bcss
