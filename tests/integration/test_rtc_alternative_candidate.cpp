#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 92: RTC Alternative Candidate Filtering
TEST(IntegrationTest, Scenario92_RtcAlternativeCandidate) {
    BcssScheduler scheduler(10, 2, true);
    Schedule init_sched(10);
    scheduler.set_periodic_baseline({}, init_sched);

    SporadicStreamSpec spec{1, 10, 1, 10};
    scheduler.admit_sporadic_stream_offline(spec);

    Job new_job(100, 1000, TaskType::OneShot, 0, 10, 1);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
}
