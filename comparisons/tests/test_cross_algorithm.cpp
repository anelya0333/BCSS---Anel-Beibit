#include <gtest/gtest.h>
#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/affine_envelope_scheduler.hpp"

using namespace comparisons;

TEST(CrossAlgorithmTest, ScenarioA_ObviousFreeCapacity) {
    // Obvious free capacity in [0, 16): all algorithms should accept
    NeutralBaselineSchedule sched(16);
    NeutralWorkload workload;
    workload.horizon = 16;

    ComparisonJob req;
    req.id = 100;
    req.release = 0;
    req.execution_requirement = 2;
    req.absolute_deadline = 8;
    req.type = TrafficType::OneShot;

    StaticDirectScheduler s_direct;
    s_direct.prepare(sched, workload);
    EXPECT_TRUE(s_direct.on_dynamic_arrival(req, 0).accepted);

    SlotShiftingScheduler s_shift;
    s_shift.prepare(sched, workload);
    EXPECT_TRUE(s_shift.on_dynamic_arrival(req, 0).accepted);

    DtssScheduler dtss;
    dtss.prepare(sched, workload);
    EXPECT_TRUE(dtss.on_dynamic_arrival(req, 0).accepted);

    AffineEnvelopeScheduler affine;
    affine.prepare(sched, workload);
    EXPECT_TRUE(affine.on_dynamic_arrival(req, 0).accepted);
}

TEST(CrossAlgorithmTest, ScenarioB_FullScheduleRejection) {
    // Completely full baseline schedule
    NeutralBaselineSchedule sched(8);
    ComparisonJob p1;
    p1.id = 1; p1.release = 0; p1.execution_requirement = 8; p1.absolute_deadline = 8;
    sched.assign_job(p1, 0);

    NeutralWorkload workload;
    workload.horizon = 8;
    workload.periodic_jobs = {p1};

    ComparisonJob req;
    req.id = 100;
    req.release = 0;
    req.execution_requirement = 2;
    req.absolute_deadline = 8;
    req.type = TrafficType::OneShot;

    StaticDirectScheduler s_direct;
    s_direct.prepare(sched, workload);
    EXPECT_FALSE(s_direct.on_dynamic_arrival(req, 0).accepted);

    SlotShiftingScheduler s_shift;
    s_shift.prepare(sched, workload);
    EXPECT_FALSE(s_shift.on_dynamic_arrival(req, 0).accepted);

    DtssScheduler dtss;
    dtss.prepare(sched, workload);
    EXPECT_FALSE(dtss.on_dynamic_arrival(req, 0).accepted);
}

TEST(CrossAlgorithmTest, ScenarioC_MovableTtSlack) {
    // Slack exists in schedule
    NeutralBaselineSchedule sched(16);
    ComparisonJob p1;
    p1.id = 1; p1.release = 0; p1.execution_requirement = 4; p1.absolute_deadline = 8;
    sched.assign_job(p1, 0);

    NeutralWorkload workload;
    workload.horizon = 16;
    workload.periodic_jobs = {p1};

    ComparisonJob req;
    req.id = 100;
    req.release = 4;
    req.execution_requirement = 4;
    req.absolute_deadline = 12;
    req.type = TrafficType::OneShot;

    StaticDirectScheduler s_direct;
    s_direct.prepare(sched, workload);
    EXPECT_TRUE(s_direct.on_dynamic_arrival(req, 4).accepted);
}
