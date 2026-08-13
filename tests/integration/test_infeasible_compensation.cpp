#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 83: Infeasible Compensation -> Rejection & Hash Unchanged
TEST(IntegrationTest, Scenario83_InfeasibleCompensation) {
    BcssScheduler scheduler(4, 1, false);
    Schedule init_sched(4);

    // Fill slots 0..3 with periodic jobs having tight deadlines
    Job j0(1, 10, TaskType::Periodic, 0, 1, 1); init_sched.assign_job(j0, 0);
    Job j1(2, 11, TaskType::Periodic, 1, 1, 1); init_sched.assign_job(j1, 1);
    Job j2(3, 12, TaskType::Periodic, 2, 1, 1); init_sched.assign_job(j2, 2);
    Job j3(4, 13, TaskType::Periodic, 3, 1, 1); init_sched.assign_job(j3, 3);

    scheduler.set_periodic_baseline({j0, j1, j2, j3}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 0, 4, 1);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.decision_mechanism, "REJECT");
    EXPECT_EQ(r.pre_schedule_hash, r.post_schedule_hash);
}
