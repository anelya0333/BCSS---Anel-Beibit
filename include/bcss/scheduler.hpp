#ifndef BCSS_SCHEDULER_HPP
#define BCSS_SCHEDULER_HPP

#include "types.hpp"
#include "schedule.hpp"
#include "hasher.hpp"
#include "dependencies.hpp"
#include "validator.hpp"
#include "rtc_guard.hpp"
#include "search.hpp"
#include <vector>
#include <string>

namespace bcss {

struct BcssResult {
    bool success{false};
    Schedule schedule{};
    std::string pre_schedule_hash{};
    std::string post_schedule_hash{};
    SlotCount actual_k{0};
    SlotCount max_disp{0};
    SlotCount total_disp{0};
    std::string decision_mechanism{}; // ACCEPT_DIRECT, ACCEPT_RECLAIM, ACCEPT_COMPENSATION, REJECT
    std::string rejection_reason{};
    SearchStats stats{};
};

class BcssScheduler {
public:
    Schedule active_schedule{};
    std::vector<Job> all_jobs{};
    DependencyGraph dependencies{};
    std::vector<SporadicStreamSpec> admitted_sporadic_streams{};
    SlotCount max_K{0};
    bool enable_rtc_guard{true};
    // Evaluation ablation controls. Defaults preserve full BCSS semantics.
    bool enable_reclamation{true};
    bool enable_compensation{true};

    BcssScheduler() = default;
    explicit BcssScheduler(SlotCount horizon, SlotCount K = 0, bool rtc = true)
        : active_schedule(horizon), max_K(K), enable_rtc_guard(rtc) {}

    std::unordered_map<TaskID, SlotIndex> last_sporadic_arrival{};

    // Offline admission
    bool set_periodic_baseline(const std::vector<Job>& tt_jobs, const Schedule& baseline_schedule);
    bool admit_sporadic_stream_offline(const SporadicStreamSpec& stream);

    // Online admission pipeline for incoming dynamic job
    BcssResult admit_dynamic_job(const Job& new_job, SlotIndex t_now);

    // Independent recourse counter: counts distinct pre-existing jobs whose start slot changed
    static SlotCount count_moved_jobs(
        const Schedule& initial_schedule,
        const Schedule& candidate_schedule,
        const std::vector<Job>& initial_jobs
    );
};

} // namespace bcss

#endif // BCSS_SCHEDULER_HPP
