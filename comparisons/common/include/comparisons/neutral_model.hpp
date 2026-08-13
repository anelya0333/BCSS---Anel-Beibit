#ifndef COMPARISONS_NEUTRAL_MODEL_HPP
#define COMPARISONS_NEUTRAL_MODEL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

namespace comparisons {

using SlotIndex = int64_t;
using SlotCount = int64_t;
using JobID = int64_t;
using TaskID = int64_t;

enum class TrafficType {
    Periodic,
    Sporadic,
    OneShot
};

struct ComparisonJob {
    JobID id{-1};
    TaskID task_id{-1};
    TrafficType type{TrafficType::Periodic};

    SlotIndex release{0};
    SlotCount relative_deadline{0};
    SlotIndex absolute_deadline{0};
    SlotCount execution_requirement{1}; // Duration C

    std::optional<SlotCount> period;
    std::optional<SlotCount> min_interarrival;

    std::vector<JobID> predecessors;

    SlotIndex current_start{-1};

    bool is_assigned() const { return current_start >= 0; }
};

struct SlotAssignment {
    SlotIndex slot_index{-1};
    JobID job_id{-1};
    TaskID task_id{-1};
    TrafficType type{TrafficType::Periodic};
    SlotIndex release{-1};
    SlotIndex deadline{-1};

    bool is_free() const { return job_id == -1; }
};

struct NeutralBaselineSchedule {
    SlotCount horizon{0};
    std::vector<SlotAssignment> slots;

    NeutralBaselineSchedule() = default;
    explicit NeutralBaselineSchedule(SlotCount h) : horizon(h), slots(static_cast<size_t>(h)) {
        for (SlotIndex s = 0; s < h; ++s) {
            slots[static_cast<size_t>(s)].slot_index = s;
        }
    }

    bool is_free(SlotIndex s) const {
        if (s < 0 || s >= horizon) return false;
        return slots[static_cast<size_t>(s)].is_free();
    }

    bool is_range_free(SlotIndex start, SlotCount duration) const {
        if (start < 0 || start + duration > horizon) return false;
        for (SlotIndex s = start; s < start + duration; ++s) {
            if (!slots[static_cast<size_t>(s)].is_free()) return false;
        }
        return true;
    }

    void assign_job(const ComparisonJob& job, SlotIndex start) {
        for (SlotIndex s = start; s < start + job.execution_requirement; ++s) {
            auto& slot = slots[static_cast<size_t>(s)];
            slot.job_id = job.id;
            slot.task_id = job.task_id;
            slot.type = job.type;
            slot.release = job.release;
            slot.deadline = job.absolute_deadline;
        }
    }

    void clear_range(SlotIndex start, SlotCount duration) {
        for (SlotIndex s = start; s < start + duration; ++s) {
            auto& slot = slots[static_cast<size_t>(s)];
            slot.job_id = -1;
            slot.task_id = -1;
            slot.type = TrafficType::Periodic;
            slot.release = -1;
            slot.deadline = -1;
        }
    }

    SlotIndex get_job_start(JobID jid) const {
        for (SlotIndex s = 0; s < horizon; ++s) {
            if (slots[static_cast<size_t>(s)].job_id == jid) return s;
        }
        return -1;
    }
};

struct SporadicStreamDefinition {
    TaskID stream_id{-1};
    SlotCount min_interarrival{1};
    SlotCount execution_requirement{1};
    SlotCount relative_deadline{1};
};

struct NeutralWorkload {
    SlotCount horizon{32};
    std::vector<ComparisonJob> periodic_jobs;
    std::vector<SporadicStreamDefinition> sporadic_streams;
    std::vector<ComparisonJob> dynamic_arrivals;
};

std::string compute_input_fingerprint(
    const NeutralWorkload& workload,
    const NeutralBaselineSchedule& schedule
);

} // namespace comparisons

#endif // COMPARISONS_NEUTRAL_MODEL_HPP
