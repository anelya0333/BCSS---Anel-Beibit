#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 87: Multi-Slot Jobs (C >= 3, C >= 2)
TEST(IntegrationTest, Scenario87_MultiSlotJobs) {
    BcssScheduler scheduler(10, 2, false);
    Schedule init_sched(10);

    Job tt_multi(1, 10, TaskType::Periodic, 0, 10, 3); // C = 3
    init_sched.assign_job(tt_multi, 0); // Occupies slots 0,1,2
    scheduler.set_periodic_baseline({tt_multi}, init_sched);

    Job new_multi(100, 1000, TaskType::OneShot, 0, 4, 2); // C = 2, needs slots 0,1
    BcssResult r = scheduler.admit_dynamic_job(new_multi, 0);

    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.actual_k, 1); // Moving 1 multi-slot job counts as k=1
}
