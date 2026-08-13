#include <gtest/gtest.h>
#include "bcss/validator.hpp"

using namespace bcss;

TEST(ValidatorTest, ValidAndInvalidSchedules) {
    Schedule sched(10);
    DependencyGraph deps;
    Job j1(1, 10, TaskType::Periodic, 0, 5, 2); // r=0, d=5, C=2
    std::vector<Job> jobs = {j1};

    sched.assign_job(j1, 0);
    ValidationResult v1 = ScheduleValidator::verify_schedule(sched, jobs, deps, 0);
    EXPECT_TRUE(v1.valid);

    // Test out of deadline assignment
    Schedule bad_sched(10);
    bad_sched.assign_job(j1, 4); // start=4, finish=6 > deadline 5
    ValidationResult v2 = ScheduleValidator::verify_schedule(bad_sched, jobs, deps, 0);
    EXPECT_FALSE(v2.valid);
}
