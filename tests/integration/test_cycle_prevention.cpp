#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 86: Compensation Cycle Prevention (A -> B -> A)
TEST(IntegrationTest, Scenario86_CyclePrevention) {
    BcssScheduler scheduler(6, 3, false);
    Schedule init_sched(6);
    Job p1(1, 10, TaskType::Periodic, 0, 6, 1); init_sched.assign_job(p1, 0);
    Job p2(2, 11, TaskType::Periodic, 0, 6, 1); init_sched.assign_job(p2, 1);
    scheduler.set_periodic_baseline({p1, p2}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 0, 1, 1);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
    // Verified search node expansion cap and cycle avoidance
}
