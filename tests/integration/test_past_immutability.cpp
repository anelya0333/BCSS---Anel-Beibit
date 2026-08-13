#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 89: Past Immutability at t_now = 5
TEST(IntegrationTest, Scenario89_PastImmutability) {
    BcssScheduler scheduler(10, 2, false);
    Schedule init_sched(10);

    Job p_past(1, 10, TaskType::Periodic, 0, 10, 4); // Assigned [3, 7)
    init_sched.assign_job(p_past, 3);
    scheduler.set_periodic_baseline({p_past}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 5, 1, 1); // r=5, d=6, needs slot 5
    // At t_now = 5, p_past started at slot 3 < 5, so p_past is IMMUTABLE and cannot be moved!
    BcssResult r = scheduler.admit_dynamic_job(new_job, 5);

    EXPECT_FALSE(r.success); // Rejected because slot 5 is occupied by immutable p_past
}
