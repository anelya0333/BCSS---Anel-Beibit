#include <gtest/gtest.h>
#include "bcss/schedule.hpp"

using namespace bcss;

TEST(ScheduleTest, ConstructorAndFreeRange) {
    Schedule sched(16);
    EXPECT_EQ(sched.horizon, 16);
    EXPECT_TRUE(sched.is_range_free(0, 16));
    EXPECT_FALSE(sched.is_range_free(0, 17));
}

TEST(ScheduleTest, AssignAndRemoveJob) {
    Schedule sched(10);
    Job j(1, 10, TaskType::Periodic, 0, 10, 3);
    EXPECT_TRUE(sched.assign_job(j, 2));
    EXPECT_FALSE(sched.is_range_free(1, 3)); // Overlaps slot 2,3,4
    EXPECT_EQ(sched.get_job_start(10), 2);

    EXPECT_TRUE(sched.remove_job(10, 3));
    EXPECT_TRUE(sched.is_range_free(0, 10));
    EXPECT_EQ(sched.get_job_start(10), -1);
}
