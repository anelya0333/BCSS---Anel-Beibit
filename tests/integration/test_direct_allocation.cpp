#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 80: Direct Available Capacity (k=0)
TEST(IntegrationTest, Scenario80_DirectAllocation) {
    BcssScheduler scheduler(16, 2, true);
    Schedule init_sched(16);
    Job tt_job(1, 10, TaskType::Periodic, 0, 4, 2); // Occupies slots 0,1
    init_sched.assign_job(tt_job, 0);
    scheduler.set_periodic_baseline({tt_job}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 2, 6, 2); // Feasible in free slots 2,3
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.decision_mechanism, "ACCEPT_DIRECT");
    EXPECT_EQ(r.actual_k, 0);
    EXPECT_NE(r.pre_schedule_hash, r.post_schedule_hash);
}
