#include <gtest/gtest.h>
#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"

using namespace comparisons;

TEST(StaticDirectTest, DirectAllocationSuccess) {
    NeutralBaselineSchedule sched(16);
    NeutralWorkload workload;
    workload.horizon = 16;

    StaticDirectScheduler scheduler;
    scheduler.prepare(sched, workload);

    ComparisonJob req;
    req.id = 100;
    req.release = 0;
    req.execution_requirement = 2;
    req.absolute_deadline = 8;
    req.type = TrafficType::OneShot;

    auto dec = scheduler.on_dynamic_arrival(req, 0);
    EXPECT_TRUE(dec.accepted);
    EXPECT_EQ(dec.jobs_moved, 0u);

    auto valid = NeutralValidator::validate_schedule(scheduler.snapshot(), {req}, 0);
    EXPECT_TRUE(valid.valid);
}

TEST(StaticDirectTest, NoFreeCapacityRejection) {
    NeutralBaselineSchedule sched(4);
    ComparisonJob fill_job;
    fill_job.id = 1;
    fill_job.release = 0;
    fill_job.execution_requirement = 4;
    fill_job.absolute_deadline = 4;
    sched.assign_job(fill_job, 0);

    NeutralWorkload workload;
    workload.horizon = 4;

    StaticDirectScheduler scheduler;
    scheduler.prepare(sched, workload);

    ComparisonJob req;
    req.id = 100;
    req.release = 0;
    req.execution_requirement = 1;
    req.absolute_deadline = 4;
    req.type = TrafficType::OneShot;

    auto dec = scheduler.on_dynamic_arrival(req, 0);
    EXPECT_FALSE(dec.accepted);
    EXPECT_EQ(dec.rejection_reason, "NO_CONTIGUOUS_FREE_SLOTS");
}

// Requirement 6: Static Direct Exhaustive Oracle Test
TEST(StaticDirectTest, ComprehensiveStaticOracleSweep) {
    uint64_t concrete_cases = 0;
    uint64_t false_accepts = 0;
    uint64_t false_rejects = 0;

    for (SlotCount H = 4; H <= 16; H += 4) {
        for (SlotCount C = 1; C <= 3; ++C) {
            for (SlotIndex r = 0; r < H - C; ++r) {
                for (SlotIndex d = r + C; d <= H; ++d) {
                    NeutralBaselineSchedule sched(H);
                    // Add periodic job occupying slot 2 if H > 2
                    if (H > 3) {
                        ComparisonJob pjob;
                        pjob.id = 1; pjob.release = 0; pjob.execution_requirement = 1; pjob.absolute_deadline = H;
                        sched.assign_job(pjob, 2);
                    }

                    NeutralWorkload workload;
                    workload.horizon = H;

                    StaticDirectScheduler scheduler;
                    scheduler.prepare(sched, workload);

                    ComparisonJob req;
                    req.id = 100;
                    req.release = r;
                    req.execution_requirement = C;
                    req.absolute_deadline = d;
                    req.type = TrafficType::OneShot;

                    auto dec = scheduler.on_dynamic_arrival(req, r);

                    // Brute force check: exists range [s, s+C) in [r, d) that is free?
                    bool oracle_possible = false;
                    for (SlotIndex s = r; s <= d - C; ++s) {
                        if (sched.is_range_free(s, C)) {
                            oracle_possible = true;
                            break;
                        }
                    }

                    concrete_cases++;
                    if (dec.accepted && !oracle_possible) false_accepts++;
                    if (!dec.accepted && oracle_possible) false_rejects++;
                }
            }
        }
    }

    EXPECT_GT(concrete_cases, 100u);
    EXPECT_EQ(false_accepts, 0u);
    EXPECT_EQ(false_rejects, 0u);
}
