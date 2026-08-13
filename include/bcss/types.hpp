#ifndef BCSS_TYPES_HPP
#define BCSS_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace bcss {

using SlotIndex = std::int64_t;
using SlotCount = std::int64_t;
using TaskID = std::int64_t;
using JobID = std::int64_t;

enum class TaskType {
    Periodic,
    Sporadic,
    OneShot
};

inline std::string task_type_to_string(TaskType type) {
    switch (type) {
        case TaskType::Periodic: return "Periodic";
        case TaskType::Sporadic: return "Sporadic";
        case TaskType::OneShot:  return "OneShot";
    }
    return "Unknown";
}

struct Job {
    TaskID task_id{-1};
    JobID job_id{-1};
    TaskType type{TaskType::Periodic};

    SlotIndex release{0};               // Physical release r
    SlotCount relative_deadline{0};     // Relative deadline D
    SlotIndex absolute_deadline{0};     // Absolute deadline d = r + D
    SlotCount duration{1};              // Execution length C >= 1 (multi-slot support)
    SlotCount period{-1};               // P for periodic, -1 for non-periodic
    SlotCount min_inter_arrival{-1};    // T_min for sporadic, -1 for non-sporadic

    SlotIndex virtual_release{0};       // Derived r' for dependency pruning
    SlotIndex virtual_deadline{0};      // Derived d' for dependency pruning

    std::vector<JobID> precedence_parents{}; // Job IDs that must finish before this job starts (A -> B)

    SlotIndex original_start{-1};       // Initial baseline schedule start slot (-1 if unassigned)
    SlotIndex current_start{-1};        // Current active schedule start slot (-1 if unassigned)

    Job() = default;

    Job(TaskID t_id, JobID j_id, TaskType t_type, SlotIndex r, SlotCount D, SlotCount C = 1,
        SlotCount P = -1, SlotCount T_min = -1)
        : task_id(t_id), job_id(j_id), type(t_type), release(r), relative_deadline(D),
          absolute_deadline(r + D), duration(C), period(P), min_inter_arrival(T_min),
          virtual_release(r), virtual_deadline(r + D), original_start(-1), current_start(-1) {}

    bool is_assigned() const { return current_start >= 0; }

    SlotIndex effective_release() const {
        return virtual_release >= 0 ? virtual_release : release;
    }

    SlotIndex effective_deadline() const {
        return virtual_deadline > 0 ? virtual_deadline : absolute_deadline;
    }

    bool occupies_slot(SlotIndex s) const {
        return is_assigned() && (s >= current_start && s < current_start + duration);
    }

    bool operator==(const Job& other) const {
        return task_id == other.task_id && job_id == other.job_id && type == other.type &&
               release == other.release && relative_deadline == other.relative_deadline &&
               absolute_deadline == other.absolute_deadline && duration == other.duration &&
               period == other.period && min_inter_arrival == other.min_inter_arrival &&
               current_start == other.current_start;
    }
};

struct SlotAssignment {
    SlotIndex slot_index{-1};
    JobID job_id{-1};                  // -1 if FREE
    TaskType type{TaskType::Periodic};
    SlotIndex release{-1};
    SlotIndex deadline{-1};
    SlotCount period{-1};

    bool is_free() const { return job_id == -1; }
};

} // namespace bcss

#endif // BCSS_TYPES_HPP
