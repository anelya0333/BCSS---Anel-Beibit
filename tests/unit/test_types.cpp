#include <gtest/gtest.h>
#include "bcss/types.hpp"

using namespace bcss;

TEST(TypesTest, JobConstructorAndDeadline) {
    Job j(1, 100, TaskType::Periodic, 5, 10, 2, 20, -1);
    EXPECT_EQ(j.task_id, 1);
    EXPECT_EQ(j.job_id, 100);
    EXPECT_EQ(j.type, TaskType::Periodic);
    EXPECT_EQ(j.release, 5);
    EXPECT_EQ(j.relative_deadline, 10);
    EXPECT_EQ(j.absolute_deadline, 15);
    EXPECT_EQ(j.duration, 2);
    EXPECT_FALSE(j.is_assigned());
}

TEST(TypesTest, SlotOccupancyCheck) {
    Job j(1, 100, TaskType::OneShot, 0, 10, 3);
    j.current_start = 4;
    EXPECT_TRUE(j.occupies_slot(4));
    EXPECT_TRUE(j.occupies_slot(5));
    EXPECT_TRUE(j.occupies_slot(6));
    EXPECT_FALSE(j.occupies_slot(3));
    EXPECT_FALSE(j.occupies_slot(7));
}
