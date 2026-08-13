#include <gtest/gtest.h>
#include "bcss/types.hpp"
#include <algorithm>

using namespace bcss;

// Scenario 90: Simultaneous Dynamic Request Sorting (Earliest Absolute Deadline First)
TEST(IntegrationTest, Scenario90_SimultaneousRequestSorting) {
    Job X(1, 100, TaskType::OneShot, 0, 20, 1); // d = 20
    Job Y(2, 101, TaskType::OneShot, 0, 12, 1); // d = 12
    Job Z(3, 102, TaskType::OneShot, 0, 15, 1); // d = 15

    std::vector<Job> requests = {X, Y, Z};
    std::sort(requests.begin(), requests.end(), [](const Job& a, const Job& b) {
        if (a.absolute_deadline != b.absolute_deadline) return a.absolute_deadline < b.absolute_deadline;
        return a.job_id < b.job_id;
    });

    EXPECT_EQ(requests[0].job_id, 101); // Y (d=12)
    EXPECT_EQ(requests[1].job_id, 102); // Z (d=15)
    EXPECT_EQ(requests[2].job_id, 100); // X (d=20)
}
