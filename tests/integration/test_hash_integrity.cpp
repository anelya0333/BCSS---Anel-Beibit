#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Scenario 93: SHA-256 Hash Integrity & Rejection Properties
TEST(IntegrationTest, Scenario93_HashIntegrity) {
    Schedule s1(8);
    Schedule s2(8);

    EXPECT_EQ(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));

    Job j(1, 10, TaskType::Periodic, 0, 8, 1);
    s1.assign_job(j, 2);
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));

    // Rejection property test
    BcssScheduler scheduler(4, 0, false);
    Job tt(1, 10, TaskType::Periodic, 0, 1, 1);
    Schedule init(4); init.assign_job(tt, 0);
    scheduler.set_periodic_baseline({tt}, init);

    Job impossible(100, 1000, TaskType::OneShot, 0, 1, 1); // Needs slot 0 (occupied) with K=0
    BcssResult r = scheduler.admit_dynamic_job(impossible, 0);

    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.pre_schedule_hash, r.post_schedule_hash);
}
