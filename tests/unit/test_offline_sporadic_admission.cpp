#include <gtest/gtest.h>
#include "bcss/scheduler.hpp"

using namespace bcss;

// Example A: Utilization < 100% but deadline/window placement makes stream infeasible -> MUST reject
TEST(OfflineSporadicAdmissionTest, ExampleA_DeadlineWindowPlacementInfeasible) {
    BcssScheduler scheduler(10, 2, true);
    Schedule init_sched(10);
    // Periodic jobs occupy slots 0, 1, 2, 3 (Load = 4/10 = 40%)
    Job tt1(1, 101, TaskType::Periodic, 0, 10, 4);
    init_sched.assign_job(tt1, 0);
    scheduler.set_periodic_baseline({tt1}, init_sched);

    // New sporadic stream: T_min = 10, C = 2, D = 3
    // Utilization: 4/10 + 2/10 = 60% < 100%.
    // But slots 0..3 occupied, so window [0, 3) has 0 free slots for D=3.
    SporadicStreamSpec stream{201, 10, 2, 3};

    bool admitted = scheduler.admit_sporadic_stream_offline(stream);
    EXPECT_FALSE(admitted) << "OFFLINE REJECT: deadline/window constraint despite U=60%";
}

// Example B: Utilization < 100% but C=3 fragmentation makes stream infeasible -> MUST reject
TEST(OfflineSporadicAdmissionTest, ExampleB_FragmentationC3Infeasible) {
    BcssScheduler scheduler(12, 2, true);
    Schedule init_sched(12);
    std::vector<Job> tt_jobs;
    // Occupied: {0,1, 3,4, 6,7, 9,10}  Free: {2, 5, 8, 11} — 4 isolated free slots
    int jid = 100;
    for (SlotIndex base = 0; base < 12; base += 3) {
        Job tt_a(jid, jid, TaskType::Periodic, base, 12, 1);
        init_sched.assign_job(tt_a, base);
        tt_jobs.push_back(tt_a);
        jid++;
        Job tt_b(jid, jid, TaskType::Periodic, base + 1, 12, 1);
        init_sched.assign_job(tt_b, base + 1);
        tt_jobs.push_back(tt_b);
        jid++;
    }
    scheduler.set_periodic_baseline(tt_jobs, init_sched);

    // C=3 requires 3 contiguous free slots. Max contiguous = 1.
    SporadicStreamSpec stream{301, 12, 3, 12};

    bool admitted = scheduler.admit_sporadic_stream_offline(stream);
    EXPECT_FALSE(admitted) << "OFFLINE REJECT: C=3 fragmentation despite U=91.7%";
}

// Example C: Valid Feasible Stream -> ADMIT
TEST(OfflineSporadicAdmissionTest, ExampleC_ValidFeasibleStreamAdmitted) {
    // Completely empty schedule, generous parameters
    BcssScheduler scheduler(20, 2, true);
    Schedule init_sched(20);
    scheduler.set_periodic_baseline({}, init_sched);

    // Stream: T_min=20, C=1, D=20 (maximally relaxed)
    // Utilization: 0 + 1/20 = 5% < 100%.
    // Every window [t, t+20) has 20 free slots >= C=1. Trivially feasible.
    SporadicStreamSpec stream{203, 20, 1, 20};

    bool admitted = scheduler.admit_sporadic_stream_offline(stream);
    EXPECT_TRUE(admitted) << "OFFLINE ACCEPT: trivially feasible stream on empty schedule";
}
