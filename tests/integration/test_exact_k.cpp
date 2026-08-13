#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 85: Strict Recourse Bound K Enforcement
TEST(IntegrationTest, Scenario85_ExactKBound) {
    Schedule init_sched(5);
    Job p1(1, 10, TaskType::Periodic, 0, 5, 1); init_sched.assign_job(p1, 0);

    Job new_job(100, 1000, TaskType::OneShot, 0, 1, 1);

    // Bound K=1 allows moving 1 job
    BcssScheduler sched(5, 1, false);
    sched.set_periodic_baseline({p1}, init_sched);
    BcssResult r = sched.admit_dynamic_job(new_job, 0);
    EXPECT_TRUE(r.success);
    EXPECT_LE(r.actual_k, 1);
}
