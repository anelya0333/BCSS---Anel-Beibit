#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

TEST(SchedulerTest, BaselineAndAdmissionPipeline) {
    BcssScheduler scheduler(16, 2, true);
    Schedule init_sched(16);
    Job tt_job(1, 10, TaskType::Periodic, 0, 8, 1);
    init_sched.assign_job(tt_job, 0);

    EXPECT_TRUE(scheduler.set_periodic_baseline({tt_job}, init_sched));

    Job new_job(100, 1000, TaskType::OneShot, 1, 4, 1);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.decision_mechanism, "ACCEPT_DIRECT");
    EXPECT_NE(r.pre_schedule_hash, r.post_schedule_hash);
}
