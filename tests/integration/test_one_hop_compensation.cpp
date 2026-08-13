#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 82: One-Hop Compensation (k=1)
TEST(IntegrationTest, Scenario82_OneHopCompensation) {
    BcssScheduler scheduler(8, 1, false);
    Schedule init_sched(8);

    // Release at slot 1 makes the job ineligible for reclamation into slot 0;
    // admitting the tight one-shot therefore requires displacing it later.
    Job tt_job(1, 10, TaskType::Periodic, 1, 7, 1);
    init_sched.assign_job(tt_job, 1); // Assigned at slot 1
    scheduler.set_periodic_baseline({tt_job}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 1, 1, 1); // Needs slot 1
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.decision_mechanism, "ACCEPT_COMPENSATION");
    EXPECT_EQ(r.actual_k, 1);

    BcssScheduler compensation_disabled(8, 1, false);
    compensation_disabled.enable_reclamation = false;
    compensation_disabled.enable_compensation = false;
    ASSERT_TRUE(compensation_disabled.set_periodic_baseline({tt_job}, init_sched));
    BcssResult disabled_result = compensation_disabled.admit_dynamic_job(new_job, 0);
    EXPECT_FALSE(disabled_result.success);
}
