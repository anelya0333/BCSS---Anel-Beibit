#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"
#include "bcss/workload.hpp"

using namespace bcss;

TEST(PropertyTest, InvariantVerificationOverRandomizedTasksets) {
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        WorkloadConfig config;
        config.horizon = 32;
        config.seed = seed;
        config.periodic_load = 0.40;

        GeneratedTaskset taskset = WorkloadGenerator::generate(config);
        BcssScheduler scheduler(config.horizon, 2, true);
        scheduler.set_periodic_baseline(taskset.periodic_jobs, taskset.baseline_schedule);

        for (const auto& dyn_job : taskset.dynamic_arrivals) {
            // Snapshot schedule BEFORE this admission decision
            Schedule pre_schedule = scheduler.active_schedule;
            std::vector<Job> pre_jobs = scheduler.all_jobs;
            std::string pre_hash = ScheduleHasher::compute_hash(pre_schedule);

            BcssResult r = scheduler.admit_dynamic_job(dyn_job, 0);

            if (r.success) {
                // Invariants on ACCEPT
                EXPECT_LE(r.actual_k, 2);
                EXPECT_NE(r.pre_schedule_hash, r.post_schedule_hash);

                // Per-decision moved jobs count: compare pre-admission vs post-admission
                // K bounds the number of pre-existing jobs displaced PER DECISION, not cumulative
                SlotCount moved = BcssScheduler::count_moved_jobs(pre_schedule, scheduler.active_schedule, pre_jobs);
                EXPECT_LE(moved, 2) << "seed=" << seed << " job_id=" << dyn_job.job_id
                                     << " mechanism=" << r.decision_mechanism;

                ValidationResult vr = ScheduleValidator::verify_schedule(scheduler.active_schedule, scheduler.all_jobs, scheduler.dependencies, 0);
                EXPECT_TRUE(vr.valid) << vr.error_message;
            } else {
                // Invariants on REJECT: pre_hash == post_hash
                EXPECT_EQ(r.pre_schedule_hash, r.post_schedule_hash);
                EXPECT_EQ(pre_hash, ScheduleHasher::compute_hash(scheduler.active_schedule));
            }
        }
    }
}
