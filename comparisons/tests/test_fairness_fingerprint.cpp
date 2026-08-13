#include <gtest/gtest.h>
#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/workload_adapter.hpp"
#include "bcss/workload.hpp"

using namespace comparisons;

TEST(FairnessFingerprintTest, LargeFingerprintComparisonSweep) {
    // Requirement 31: Fingerprint comparison sweep over generated neutral scenarios
    uint64_t fingerprint_comparisons = 0;
    uint64_t mismatches = 0;

    for (uint64_t seed = 1000; seed < 1050; ++seed) {
        bcss::WorkloadConfig config{};
        config.horizon = 32;
        config.seed = seed;
        config.profile = bcss::WorkloadProfile::Normal;

        auto bcss_taskset = bcss::WorkloadGenerator::generate(config);

        NeutralWorkload workload = WorkloadAdapter::convert_taskset(bcss_taskset);
        NeutralBaselineSchedule schedule = WorkloadAdapter::convert_schedule(bcss_taskset.baseline_schedule);

        StaticDirectScheduler s_direct;
        s_direct.prepare(schedule, workload);
        std::string fp_direct = s_direct.metrics().input_fingerprint;

        SlotShiftingScheduler s_shift;
        s_shift.prepare(schedule, workload);
        std::string fp_shift = s_shift.metrics().input_fingerprint;

        DtssScheduler dtss;
        dtss.prepare(schedule, workload);
        std::string fp_dtss = dtss.metrics().input_fingerprint;

        fingerprint_comparisons += 2;
        if (fp_direct != fp_shift) mismatches++;
        if (fp_direct != fp_dtss) mismatches++;
    }

    EXPECT_GT(fingerprint_comparisons, 50u);
    EXPECT_EQ(mismatches, 0u);
}

TEST(FairnessFingerprintTest, AdapterImmutabilityCheck) {
    // Requirement 32: Verify adapter does NOT mutate neutral input
    bcss::WorkloadConfig config{};
    config.horizon = 32;
    config.seed = 999;
    auto bcss_taskset = bcss::WorkloadGenerator::generate(config);

    NeutralWorkload workload_before = WorkloadAdapter::convert_taskset(bcss_taskset);
    NeutralBaselineSchedule schedule_before = WorkloadAdapter::convert_schedule(bcss_taskset.baseline_schedule);

    std::string hash_before = compute_input_fingerprint(workload_before, schedule_before);

    // Call adapters
    NeutralWorkload workload_after = WorkloadAdapter::convert_taskset(bcss_taskset);
    NeutralBaselineSchedule schedule_after = WorkloadAdapter::convert_schedule(bcss_taskset.baseline_schedule);

    std::string hash_after = compute_input_fingerprint(workload_after, schedule_after);

    EXPECT_EQ(hash_before, hash_after);
}
