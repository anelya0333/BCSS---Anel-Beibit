#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 84: Multi-Hop Compensation (K=2 accepted vs K=1 rejected)
TEST(IntegrationTest, Scenario84_MultiHopCompensation) {
    // Chain repair: NewJob (slot 0) -> P1 (slot 0 -> slot 1) -> P2 (slot 1 -> slot 2) -> FREE
    Schedule init_sched(6);
    Job p1(1, 10, TaskType::Periodic, 0, 2, 1); init_sched.assign_job(p1, 0); // d=2 forces p1 to slot 1 if displaced
    Job p2(2, 11, TaskType::Periodic, 0, 6, 1); init_sched.assign_job(p2, 1); // d=6 allows p2 to move to slot 2

    Job new_job(100, 1000, TaskType::OneShot, 0, 1, 1); // Needs slot 0 (r=0, d=1)

    // K=2: Allowed
    BcssScheduler scheduler2(6, 2, false);
    scheduler2.set_periodic_baseline({p1, p2}, init_sched);
    BcssResult r2 = scheduler2.admit_dynamic_job(new_job, 0);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(r2.actual_k, 2);

    // K=1: Rejected
    BcssScheduler scheduler1(6, 1, false);
    scheduler1.set_periodic_baseline({p1, p2}, init_sched);
    BcssResult r1 = scheduler1.admit_dynamic_job(new_job, 0);
    EXPECT_FALSE(r1.success);
}
