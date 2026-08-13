#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 88: Precedence Dependency Verification (A -> B)
TEST(IntegrationTest, Scenario88_PrecedenceDependencies) {
    BcssScheduler scheduler(10, 2, false);
    Schedule init_sched(10);

    Job pA(1, 10, TaskType::Periodic, 0, 10, 2); // C=2, start=0, finish=2
    Job pB(2, 11, TaskType::Periodic, 0, 10, 2); // C=2, start=2, finish=4
    init_sched.assign_job(pA, 0);
    init_sched.assign_job(pB, 2);

    scheduler.dependencies.add_dependency(10, 11); // pA -> pB
    scheduler.set_periodic_baseline({pA, pB}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 4, 8, 2);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
}
