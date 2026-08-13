#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

TEST(MetamorphicTest, KBoundMonotonicity) {
    // If a request is feasible at K=1, increasing K to 2 must not make it infeasible
    Schedule init_sched(16);
    Job tt_job(1, 10, TaskType::Periodic, 0, 16, 1);
    init_sched.assign_job(tt_job, 1);

    Job new_job(100, 1000, TaskType::OneShot, 1, 4, 1);

    BcssScheduler sched1(16, 1, false);
    sched1.set_periodic_baseline({tt_job}, init_sched);
    BcssResult r1 = sched1.admit_dynamic_job(new_job, 0);

    BcssScheduler sched2(16, 2, false);
    sched2.set_periodic_baseline({tt_job}, init_sched);
    BcssResult r2 = sched2.admit_dynamic_job(new_job, 0);

    if (r1.success) {
        EXPECT_TRUE(r2.success);
    }
}
