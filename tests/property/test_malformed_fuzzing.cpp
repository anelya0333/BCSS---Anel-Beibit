#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

TEST(FuzzTest, MalformedAndExtremeInputHandling) {
    BcssScheduler scheduler(16, 2, true);

    // Negative release / deadline
    Job bad1(-1, -5, TaskType::OneShot, -10, 0, 1);
    BcssResult r1 = scheduler.admit_dynamic_job(bad1, 0);
    EXPECT_FALSE(r1.success);

    // C > H
    Job bad2(2, 999, TaskType::OneShot, 0, 16, 20);
    BcssResult r2 = scheduler.admit_dynamic_job(bad2, 0);
    EXPECT_FALSE(r2.success);

    // Zero duration
    Job bad3(3, 998, TaskType::OneShot, 0, 5, 0);
    BcssResult r3 = scheduler.admit_dynamic_job(bad3, 0);
    EXPECT_FALSE(r3.success);
}
