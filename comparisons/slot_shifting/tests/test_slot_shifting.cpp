#include <gtest/gtest.h>
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"

using namespace comparisons;

TEST(SlotShiftingTest, Fohler1995_NumericalExampleReproduction) {
    // Fohler 1995 Sec 4 Example: Horizon 12 with 2 periodic tasks establishing intervals
    NeutralBaselineSchedule sched(12);
    ComparisonJob p1;
    p1.id = 1; p1.release = 0; p1.execution_requirement = 2; p1.absolute_deadline = 4;
    sched.assign_job(p1, 0);

    ComparisonJob p2;
    p2.id = 2; p2.release = 4; p2.execution_requirement = 4; p2.absolute_deadline = 12;
    sched.assign_job(p2, 4);

    NeutralWorkload workload;
    workload.horizon = 12;
    workload.periodic_jobs = {p1, p2};

    SlotShiftingScheduler scheduler(SlotShiftingMode::CommonCommunication);
    auto prep = scheduler.prepare(sched, workload);
    EXPECT_TRUE(prep.success);

    auto intervals = scheduler.get_capacity_intervals();
    ASSERT_GE(intervals.size(), 2u);

    // Interval 1: [0, 4), C=2, spare_capacity = 2, leeway = 6
    // Interval 2: [4, 12), C=4, spare_capacity = 4, leeway = 4
    EXPECT_EQ(intervals[0].start, 0);
    EXPECT_EQ(intervals[0].end, 4);
    EXPECT_EQ(intervals[0].spare_capacity, 2);

    EXPECT_EQ(intervals[1].start, 4);
    EXPECT_EQ(intervals[1].end, 12);
    EXPECT_EQ(intervals[1].spare_capacity, 4);
}

TEST(SlotShiftingTest, CapacityConservationInvariant) {
    // Requirement 10: Invariant check for capacity conservation
    NeutralBaselineSchedule sched(16);
    ComparisonJob p1;
    p1.id = 1; p1.release = 0; p1.execution_requirement = 4; p1.absolute_deadline = 8;
    sched.assign_job(p1, 0);

    NeutralWorkload workload;
    workload.horizon = 16;
    workload.periodic_jobs = {p1};

    SlotShiftingScheduler scheduler(SlotShiftingMode::CommonCommunication);
    scheduler.prepare(sched, workload);

    auto intervals_before = scheduler.get_capacity_intervals();
    SlotCount total_spare_before = 0;
    for (const auto& ci : intervals_before) {
        total_spare_before += ci.spare_capacity;
    }

    ComparisonJob req;
    req.id = 100;
    req.release = 0;
    req.execution_requirement = 3;
    req.absolute_deadline = 12;
    req.type = TrafficType::OneShot;

    auto dec = scheduler.on_dynamic_arrival(req, 0);
    EXPECT_TRUE(dec.accepted);

    auto intervals_after = scheduler.get_capacity_intervals();
    SlotCount total_spare_after = 0;
    for (const auto& ci : intervals_after) {
        total_spare_after += ci.spare_capacity;
    }

    // Deducted spare capacity must match allocated duration
    EXPECT_EQ(total_spare_before - total_spare_after, req.execution_requirement);
}

TEST(SlotShiftingTest, SporadicOfflineAdmissionAndRuntimeGuarantee) {
    NeutralBaselineSchedule sched(32);
    NeutralWorkload workload;
    workload.horizon = 32;

    SporadicStreamDefinition stream;
    stream.stream_id = 10;
    stream.min_interarrival = 8;
    stream.execution_requirement = 2;
    stream.relative_deadline = 8;

    workload.sporadic_streams.push_back(stream);

    SlotShiftingScheduler scheduler(SlotShiftingMode::PaperNative);
    auto prep = scheduler.prepare(sched, workload);
    EXPECT_EQ(prep.admitted_sporadic_streams, 1u);

    // Runtime arrival for admitted stream
    ComparisonJob sjob;
    sjob.id = 200;
    sjob.task_id = 10;
    sjob.type = TrafficType::Sporadic;
    sjob.release = 0;
    sjob.execution_requirement = 2;
    sjob.relative_deadline = 8;
    sjob.absolute_deadline = 8;
    sjob.min_interarrival = 8;

    auto dec = scheduler.on_dynamic_arrival(sjob, 0);
    EXPECT_TRUE(dec.accepted);
}

TEST(SlotShiftingTest, RelocatesFutureJobWhenNoDirectGapExists) {
    NeutralBaselineSchedule schedule(8);
    NeutralWorkload workload;
    workload.horizon = 8;
    ComparisonJob periodic;
    periodic.id = 1;
    periodic.task_id = 1;
    periodic.type = TrafficType::Periodic;
    periodic.release = 0;
    periodic.relative_deadline = 8;
    periodic.absolute_deadline = 8;
    periodic.execution_requirement = 1;
    schedule.assign_job(periodic, 1);
    workload.periodic_jobs.push_back(periodic);

    SlotShiftingScheduler scheduler;
    ASSERT_TRUE(scheduler.prepare(schedule, workload).success);
    ComparisonJob request;
    request.id = 100;
    request.task_id = 100;
    request.type = TrafficType::OneShot;
    request.release = 1;
    request.relative_deadline = 1;
    request.absolute_deadline = 2;
    request.execution_requirement = 1;
    ComparisonDecision decision = scheduler.on_dynamic_arrival(request, 0);
    EXPECT_TRUE(decision.accepted);
    EXPECT_EQ(decision.decision_mechanism, "SLOT_SHIFTING_INTERVAL_RELOCATION");
    EXPECT_EQ(decision.jobs_moved, 1U);
}
