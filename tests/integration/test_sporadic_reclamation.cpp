#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 81: Unused Sporadic-Capacity Reclamation & Strict Recourse Accounting
TEST(IntegrationTest, Scenario81_SporadicReclamation) {
    Schedule init_sched(16);
    Job tt_job(1, 10, TaskType::Periodic, 0, 16, 1); // Assigned at slot 4
    init_sched.assign_job(tt_job, 4);

    Job new_job(100, 1000, TaskType::OneShot, 4, 1, 1); // Needs slot 4 (r=4, d=5, C=1)

    // K = 0: Must REJECT because moving tt_job to slot 0 would change 1 pre-existing allocation (k=1 > K=0)
    BcssScheduler sched_k0(16, 0, true);
    sched_k0.set_periodic_baseline({tt_job}, init_sched);
    BcssResult r_k0 = sched_k0.admit_dynamic_job(new_job, 0);
    EXPECT_FALSE(r_k0.success);
    EXPECT_EQ(BcssScheduler::count_moved_jobs(init_sched, sched_k0.active_schedule, {tt_job}), 0);

    // K = 1: Must ACCEPT with k = 1 moved job (tt_job moved from slot 4 to slot 0)
    BcssScheduler sched_k1(16, 1, true);
    sched_k1.set_periodic_baseline({tt_job}, init_sched);
    BcssResult r_k1 = sched_k1.admit_dynamic_job(new_job, 0);
    EXPECT_TRUE(r_k1.success);
    EXPECT_EQ(r_k1.decision_mechanism, "ACCEPT_RECLAIM");
    EXPECT_EQ(r_k1.actual_k, 1); // Exactly 1 pre-existing job moved
    EXPECT_EQ(BcssScheduler::count_moved_jobs(init_sched, sched_k1.active_schedule, {tt_job}), 1);

    // Reclamation-only ablation still admits this case without compensation.
    BcssScheduler reclaim_only(16, 1, true);
    reclaim_only.enable_compensation = false;
    ASSERT_TRUE(reclaim_only.set_periodic_baseline({tt_job}, init_sched));
    BcssResult reclaim_result = reclaim_only.admit_dynamic_job(new_job, 0);
    EXPECT_TRUE(reclaim_result.success);
    EXPECT_EQ(reclaim_result.decision_mechanism, "ACCEPT_RECLAIM");
}
