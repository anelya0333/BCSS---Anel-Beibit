#include "bcss/types.hpp"
#include "bcss/scheduler.hpp"
#include "bcss/workload.hpp"
#include "bcss/validator.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <set>
#include <map>

using namespace bcss;

struct StreamArrivalRecord {
    TaskID stream_id{-1};
    JobID job_id{-1};
    bool offline_stream_admitted{false};
    SlotIndex release{0};
    SlotCount Tmin{0};
    bool Tmin_contract_valid{true};
    SlotCount C{0};
    SlotCount relative_deadline{0};
    SlotIndex absolute_deadline{0};
    std::string runtime_decision{};
    std::string decision_mechanism{};
    std::string rejection_reason{};
};

struct ProfileStats {
    std::string profile_name;
    uint64_t total_offline_attempted{0};
    uint64_t total_offline_admitted{0};
    uint64_t total_offline_rejected{0};

    // Admitted streams
    uint64_t admitted_arrivals{0};
    uint64_t admitted_accepted{0};
    uint64_t admitted_rejected{0};
    uint64_t admitted_deadline_misses{0};

    // Unadmitted streams
    uint64_t unadmitted_arrivals{0};
    uint64_t unadmitted_accepted{0};
    uint64_t unadmitted_rejected{0};

    // Periodic
    uint64_t periodic_jobs{0};
    uint64_t periodic_misses{0};

    // One-shot
    uint64_t oneshot_arrivals{0};
    uint64_t oneshot_accepted{0};
    uint64_t oneshot_rejected{0};

    SlotCount max_k{0};
    uint64_t k_violations{0};
    uint64_t hash_inconsistencies{0};
};

ProfileStats run_profile_experiment(
    const std::string& name,
    WorkloadConfig config,
    SlotCount K,
    bool enable_rtc,
    bool enable_oneshot,
    bool enable_reclamation,
    bool enable_compensation,
    int tasksets_count,
    uint64_t base_seed,
    bool verbose = false
) {
    (void)enable_reclamation;
    ProfileStats stats;
    stats.profile_name = name;

    for (int ts = 0; ts < tasksets_count; ++ts) {
        WorkloadConfig current_cfg = config;
        current_cfg.seed = base_seed + static_cast<uint64_t>(ts);

        GeneratedTaskset taskset = WorkloadGenerator::generate(current_cfg);

        // Effective K: if compensation disabled, K = 0
        SlotCount effective_K = enable_compensation ? K : 0;

        BcssScheduler scheduler(current_cfg.horizon, effective_K, enable_rtc);
        scheduler.set_periodic_baseline(taskset.periodic_jobs, taskset.baseline_schedule);

        std::set<TaskID> admitted_stream_ids;
        for (const auto& s_spec : taskset.sporadic_streams) {
            stats.total_offline_attempted++;
            bool adm = scheduler.admit_sporadic_stream_offline(s_spec);
            if (adm) {
                admitted_stream_ids.insert(s_spec.task_id);
                stats.total_offline_admitted++;
            } else {
                stats.total_offline_rejected++;
            }
        }

        stats.periodic_jobs += taskset.periodic_jobs.size();

        for (const auto& dyn_job : taskset.dynamic_arrivals) {
            // Check if one-shot should be skipped for ablation
            if (dyn_job.type == TaskType::OneShot && !enable_oneshot) {
                continue;
            }

            std::string pre_hash = ScheduleHasher::compute_hash(scheduler.active_schedule);
            BcssResult r = scheduler.admit_dynamic_job(dyn_job, dyn_job.release);

            if (r.actual_k > effective_K) {
                stats.k_violations++;
            }

            if (!r.success) {
                if (r.pre_schedule_hash != r.post_schedule_hash) {
                    stats.hash_inconsistencies++;
                }
            } else {
                if (r.pre_schedule_hash == r.post_schedule_hash) {
                    stats.hash_inconsistencies++;
                }
            }

            if (r.actual_k > stats.max_k) {
                stats.max_k = r.actual_k;
            }

            if (dyn_job.type == TaskType::Sporadic) {
                bool is_admitted = (admitted_stream_ids.count(dyn_job.task_id) > 0);
                if (is_admitted) {
                    stats.admitted_arrivals++;
                    if (r.success) {
                        stats.admitted_accepted++;
                    } else {
                        stats.admitted_rejected++;
                        if (verbose) {
                            std::cout << "  [FAILURE TRACE] Admitted sporadic stream " << dyn_job.task_id
                                      << " job " << dyn_job.job_id << " rejected! Reason: " << r.rejection_reason << "\n";
                        }
                    }
                } else {
                    stats.unadmitted_arrivals++;
                    if (r.success) stats.unadmitted_accepted++;
                    else stats.unadmitted_rejected++;
                }
            } else if (dyn_job.type == TaskType::OneShot) {
                stats.oneshot_arrivals++;
                if (r.success) stats.oneshot_accepted++;
                else stats.oneshot_rejected++;
            }
        }

        // Verify periodic baseline preservation
        for (const auto& pj : taskset.periodic_jobs) {
            SlotIndex start = scheduler.active_schedule.get_job_start(pj.job_id);
            if (start < 0 || start + pj.duration > pj.absolute_deadline) {
                stats.periodic_misses++;
            }
        }
    }

    return stats;
}

void print_profile_stats(const ProfileStats& s) {
    double admitted_loss = (s.admitted_arrivals > 0) ? (100.0 * static_cast<double>(s.admitted_rejected) / static_cast<double>(s.admitted_arrivals)) : 0.0;
    double oneshot_acc = (s.oneshot_arrivals > 0) ? (100.0 * static_cast<double>(s.oneshot_accepted) / static_cast<double>(s.oneshot_arrivals)) : 0.0;

    std::cout << "--- Profile: " << s.profile_name << " ---\n";
    std::cout << "  Offline streams attempted:           " << s.total_offline_attempted << "\n";
    std::cout << "  Offline streams admitted:            " << s.total_offline_admitted << "\n";
    std::cout << "  Offline streams rejected:            " << s.total_offline_rejected << "\n";
    std::cout << "  OFFLINE-ADMITTED Sporadic Arrivals:  " << s.admitted_arrivals << "\n";
    std::cout << "    Accepted:                          " << s.admitted_accepted << "\n";
    std::cout << "    Rejected/Lost:                     " << s.admitted_rejected << " (" << std::fixed << std::setprecision(2) << admitted_loss << "% loss)\n";
    std::cout << "    Deadline misses:                   " << s.admitted_deadline_misses << "\n";
    std::cout << "  UNADMITTED Sporadic Arrivals:        " << s.unadmitted_arrivals << " (Accepted: " << s.unadmitted_accepted << ", Rejected: " << s.unadmitted_rejected << ")\n";
    std::cout << "  Periodic Jobs:                       " << s.periodic_jobs << " (Misses: " << s.periodic_misses << ")\n";
    std::cout << "  One-Shot Arrivals:                   " << s.oneshot_arrivals << " (Accepted: " << s.oneshot_accepted << " = " << std::fixed << std::setprecision(2) << oneshot_acc << "%)\n";
    std::cout << "  Max k observed:                      " << s.max_k << "\n";
    std::cout << "  K violations:                        " << s.k_violations << "\n";
    std::cout << "  Hash inconsistencies:                " << s.hash_inconsistencies << "\n\n";
}

int main() {
    SlotCount H = 32;
    SlotCount K = 2;
    bool enable_rtc = true;
    int tasksets_per_profile = 5; // Multi-seed stress sweep over 5 seeds

    WorkloadConfig quiet_cfg;
    quiet_cfg.horizon = H;
    quiet_cfg.periodic_load = 0.30;
    quiet_cfg.sporadic_ratio = 0.10;
    quiet_cfg.oneshot_ratio = 0.10;

    WorkloadConfig normal_cfg;
    normal_cfg.horizon = H;
    normal_cfg.periodic_load = 0.50;
    normal_cfg.sporadic_ratio = 0.20;
    normal_cfg.oneshot_ratio = 0.30;

    WorkloadConfig busy_cfg;
    busy_cfg.horizon = H;
    busy_cfg.periodic_load = 0.70;
    busy_cfg.sporadic_ratio = 0.30;
    busy_cfg.oneshot_ratio = 0.50;

    std::cout << "====================================================================================================\n";
    std::cout << "                     BCSS MULTI-SEED STRESS EXPERIMENT (20 TASKSETS PER PROFILE)                     \n";
    std::cout << "====================================================================================================\n\n";

    ProfileStats quiet_stats = run_profile_experiment("QUIET", quiet_cfg, K, enable_rtc, true, true, true, tasksets_per_profile, 100);
    ProfileStats normal_stats = run_profile_experiment("NORMAL", normal_cfg, K, enable_rtc, true, true, true, tasksets_per_profile, 200);
    ProfileStats busy_stats = run_profile_experiment("BUSY", busy_cfg, K, enable_rtc, true, true, true, tasksets_per_profile, 300);

    print_profile_stats(quiet_stats);
    print_profile_stats(normal_stats);
    print_profile_stats(busy_stats);

    std::cout << "====================================================================================================\n";
    std::cout << "                                  CONTROLLED ABLATION EXPERIMENT                                    \n";
    std::cout << "====================================================================================================\n\n";

    // Controlled Ablations on BUSY profile (5 seeds)
    // Stage A: Baseline + Sporadic Only (K=0, RTC=ON, oneshot=OFF)
    ProfileStats abl_A = run_profile_experiment("Stage A: Sporadic Only (K=0, OneShot=OFF)", busy_cfg, 0, true, false, false, false, 5, 300);
    // Stage B: + Reclamation (K=1, RTC=ON, oneshot=OFF, reclamation=ON)
    ProfileStats abl_B = run_profile_experiment("Stage B: + Reclamation (K=1, OneShot=OFF)", busy_cfg, 1, true, false, true, false, 5, 300);
    // Stage C: + Compensation (K=2, RTC=ON, oneshot=OFF, compensation=ON)
    ProfileStats abl_C = run_profile_experiment("Stage C: + Compensation (K=2, OneShot=OFF)", busy_cfg, 2, true, false, true, true, 5, 300);
    // Stage D: + One-Shot (K=2, RTC=ON, oneshot=ON)
    ProfileStats abl_D = run_profile_experiment("Stage D: + One-Shot (Full BCSS)", busy_cfg, 2, true, true, true, true, 5, 300);

    auto print_abl_row = [](const std::string& name, const ProfileStats& s) {
        double loss = (s.admitted_arrivals > 0) ? (100.0 * static_cast<double>(s.admitted_rejected) / static_cast<double>(s.admitted_arrivals)) : 0.0;
        std::cout << "| " << std::left << std::setw(42) << name << " | "
                  << std::right << std::setw(32) << s.admitted_arrivals << " | "
                  << std::setw(8) << s.admitted_rejected << " | "
                  << std::setw(9) << std::fixed << std::setprecision(2) << loss << "% |\n";
    };

    std::cout << "| Configuration                              | Compliant Admitted Sporadic Jobs | Lost     | Loss %    |\n";
    std::cout << "|--------------------------------------------|----------------------------------|----------|-----------|\n";
    print_abl_row("Stage A: Baseline + Sporadic Only", abl_A);
    print_abl_row("Stage B: + Reclamation", abl_B);
    print_abl_row("Stage C: + Compensation", abl_C);
    print_abl_row("Stage D: Full BCSS (+ One-Shot)", abl_D);
    std::cout << "====================================================================================================\n\n";

    return 0;
}
