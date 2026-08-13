#ifndef BCSS_SCHEDULE_HPP
#define BCSS_SCHEDULE_HPP

#include "types.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <iostream>

namespace bcss {

class Schedule {
public:
    SlotCount horizon{0};
    std::vector<SlotAssignment> slots{};
    std::unordered_map<JobID, SlotIndex> job_to_start{};

    Schedule() = default;

    explicit Schedule(SlotCount h) : horizon(h), slots(static_cast<size_t>(h)) {
        if (h <= 0) {
            throw std::invalid_argument("Schedule horizon must be positive");
        }
        for (SlotIndex s = 0; s < h; ++s) {
            slots[static_cast<size_t>(s)].slot_index = s;
            slots[static_cast<size_t>(s)].job_id = -1;
        }
    }

    bool is_valid_slot(SlotIndex s) const {
        return s >= 0 && s < horizon;
    }

    bool is_range_free(SlotIndex start, SlotCount duration) const {
        if (start < 0 || start + duration > horizon) return false;
        for (SlotIndex s = start; s < start + duration; ++s) {
            if (!slots[static_cast<size_t>(s)].is_free()) return false;
        }
        return true;
    }

    bool assign_job(const Job& j, SlotIndex start) {
        if (!is_range_free(start, j.duration)) return false;
        for (SlotIndex s = start; s < start + j.duration; ++s) {
            auto& slot = slots[static_cast<size_t>(s)];
            slot.job_id = j.job_id;
            slot.type = j.type;
            slot.release = j.release;
            slot.deadline = j.absolute_deadline;
            slot.period = j.period;
        }
        job_to_start[j.job_id] = start;
        return true;
    }

    bool remove_job(JobID j_id, SlotCount duration) {
        auto it = job_to_start.find(j_id);
        if (it == job_to_start.end()) return false;
        SlotIndex start = it->second;
        for (SlotIndex s = start; s < start + duration; ++s) {
            auto& slot = slots[static_cast<size_t>(s)];
            slot.job_id = -1;
            slot.type = TaskType::Periodic;
            slot.release = -1;
            slot.deadline = -1;
            slot.period = -1;
        }
        job_to_start.erase(it);
        return true;
    }

    SlotIndex get_job_start(JobID j_id) const {
        auto it = job_to_start.find(j_id);
        return (it != job_to_start.end()) ? it->second : -1;
    }

    // Canonical versioned byte serialization for SHA-256 schedule hashing
    std::vector<uint8_t> serialize_canonical() const;

    bool operator==(const Schedule& other) const {
        if (horizon != other.horizon) return false;
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].job_id != other.slots[i].job_id) return false;
        }
        return true;
    }

    bool operator!=(const Schedule& other) const {
        return !(*this == other);
    }
};

} // namespace bcss

#endif // BCSS_SCHEDULE_HPP
