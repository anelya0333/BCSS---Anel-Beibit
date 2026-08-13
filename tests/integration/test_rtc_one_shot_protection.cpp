#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 91: RTC One-Shot Protection Scenario
TEST(IntegrationTest, Scenario91_RtcOneShotProtection) {
    BcssScheduler scheduler(10, 1, true); // RTC Guard ENABLED
    Schedule init_sched(10);
    scheduler.set_periodic_baseline({}, init_sched);

    SporadicStreamSpec spec{1, 5, 2, 5}; // Requires 2 slots every 5 slots
    scheduler.admit_sporadic_stream_offline(spec);

    Job new_job(100, 1000, TaskType::OneShot, 0, 10, 8); // Needs 8 free slots, leaving only 2
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    // Rejected because 8 slots leaves insufficient capacity for sporadic envelope!
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.pre_schedule_hash, r.post_schedule_hash);
    EXPECT_GT(r.stats.rtc_checks, 0U);
    EXPECT_GT(r.stats.rtc_unsafe, 0U);
}
