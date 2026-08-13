#include <gtest/gtest.h>
#include "small_state_oracle.hpp"
#include "bcss/scheduler.hpp"

using namespace bcss;

TEST(OracleTest, ExhaustiveSmallStateVerification) {
    // Test production BCSS scheduler against exhaustive brute-force oracle across 50 small instances
    for (int instance = 0; instance < 50; ++instance) {
        SlotCount H = 6;
        BcssScheduler scheduler(H, 2, false);
        Schedule init_sched(H);

        Job p1(1, 10, TaskType::Periodic, 0, 6, 1); init_sched.assign_job(p1, 0);
        Job p2(2, 11, TaskType::Periodic, 0, 6, 1); init_sched.assign_job(p2, 2);
        scheduler.set_periodic_baseline({p1, p2}, init_sched);

        Job new_job(100, 1000, TaskType::OneShot, static_cast<SlotIndex>(instance % 4), 6, 1);

        OracleResult oracle_res = SmallStateOracle::solve_exhaustive(init_sched, {p1, p2}, new_job, scheduler.dependencies, 0, 2);
        BcssResult bcss_res = scheduler.admit_dynamic_job(new_job, 0);

        EXPECT_EQ(bcss_res.success, oracle_res.feasible);
        if (bcss_res.success && oracle_res.feasible) {
            EXPECT_EQ(bcss_res.actual_k, oracle_res.min_k);
        }
    }
}
