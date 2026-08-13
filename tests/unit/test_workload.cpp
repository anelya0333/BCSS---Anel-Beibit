#include <gtest/gtest.h>
#include <set>
#include "bcss/workload.hpp"

using namespace bcss;

TEST(WorkloadTest, DeterministicGeneration) {
    WorkloadConfig config;
    config.horizon = 32;
    config.seed = 12345;

    GeneratedTaskset ts1 = WorkloadGenerator::generate(config);
    GeneratedTaskset ts2 = WorkloadGenerator::generate(config);

    EXPECT_EQ(ts1.periodic_jobs.size(), ts2.periodic_jobs.size());
    EXPECT_EQ(ts1.dynamic_arrivals.size(), ts2.dynamic_arrivals.size());
    EXPECT_EQ(ts1.baseline_schedule, ts2.baseline_schedule);
}

TEST(WorkloadTest, ZeroOneShotLoadProducesNoOneShotArrivals) {
    WorkloadConfig config;
    config.horizon = 256;
    config.seed = 7;
    config.oneshot_ratio = 0.0;

    GeneratedTaskset taskset = WorkloadGenerator::generate(config);
    const auto count = std::count_if(taskset.dynamic_arrivals.begin(), taskset.dynamic_arrivals.end(), [](const Job& job) {
        return job.type == TaskType::OneShot;
    });
    EXPECT_EQ(count, 0);
}

TEST(WorkloadTest, RuntimeProfilesChangeCompliantSporadicTraces) {
    auto sporadic_count = [](WorkloadProfile profile) {
        WorkloadConfig config;
        config.horizon = 512;
        config.seed = 99;
        config.profile = profile;
        GeneratedTaskset taskset = WorkloadGenerator::generate(config);
        return std::count_if(taskset.dynamic_arrivals.begin(), taskset.dynamic_arrivals.end(), [](const Job& job) {
            return job.type == TaskType::Sporadic;
        });
    };

    const auto quiet = sporadic_count(WorkloadProfile::Quiet);
    const auto normal = sporadic_count(WorkloadProfile::Normal);
    const auto busy = sporadic_count(WorkloadProfile::Busy);
    const auto worst = sporadic_count(WorkloadProfile::Worst);
    EXPECT_LT(quiet, normal);
    EXPECT_LE(normal, busy);
    EXPECT_LT(busy, worst);
}

TEST(WorkloadTest, PeriodicUtilizationAndMultiSlotParametersPropagate) {
    WorkloadConfig config;
    config.horizon = 1000;
    config.seed = 123;
    config.periodic_load = 0.70;
    config.allow_multi_slot = true;
    config.max_duration = 4;

    GeneratedTaskset taskset = WorkloadGenerator::generate(config);
    SlotCount occupied = 0;
    for (const auto& slot : taskset.baseline_schedule.slots) {
        if (!slot.is_free()) ++occupied;
    }
    EXPECT_NEAR(static_cast<double>(occupied) / static_cast<double>(config.horizon), 0.70, 0.004);
    EXPECT_TRUE(std::any_of(taskset.periodic_jobs.begin(), taskset.periodic_jobs.end(), [](const Job& job) {
        return job.duration == 4;
    }));
}

TEST(WorkloadTest, ThesisScaleDynamicJobIdsDoNotCollideWithPeriodicJobs) {
    WorkloadConfig config;
    config.horizon = 10000;
    config.seed = 20260811;
    config.trace_seed = 20260812;
    config.periodic_load = 0.70;
    config.oneshot_ratio = 0.001;
    config.candidate_sporadic_stream_count = 30;
    config.candidate_sporadic_utilization = 0.01;

    GeneratedTaskset taskset = WorkloadGenerator::generate(config);
    std::set<JobID> identifiers;
    for (const auto& job : taskset.periodic_jobs) {
        EXPECT_TRUE(identifiers.insert(job.job_id).second);
    }
    for (const auto& job : taskset.dynamic_arrivals) {
        EXPECT_TRUE(identifiers.insert(job.job_id).second)
            << "duplicate job ID " << job.job_id;
    }
}
