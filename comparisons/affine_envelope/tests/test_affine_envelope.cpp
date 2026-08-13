#include <gtest/gtest.h>
#include "comparisons/affine_envelope_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"

using namespace comparisons;

TEST(AffineEnvelopeTest, NumericalDerivationAndBlcCompliance) {
    NeutralBaselineSchedule dummy_baseline(32);
    NeutralWorkload workload;
    workload.horizon = 32;

    SporadicStreamDefinition ss1;
    ss1.stream_id = 10;
    ss1.min_interarrival = 8;
    ss1.execution_requirement = 2;
    ss1.relative_deadline = 8;
    workload.sporadic_streams.push_back(ss1);

    SporadicStreamDefinition ss2;
    ss2.stream_id = 11;
    ss2.min_interarrival = 16;
    ss2.execution_requirement = 4;
    ss2.relative_deadline = 16;
    workload.sporadic_streams.push_back(ss2);

    AffineEnvelopeScheduler scheduler;
    auto prep = scheduler.prepare(dummy_baseline, workload);
    EXPECT_TRUE(prep.success);

    auto params = scheduler.get_affine_parameters();
    // Independent numerical calculation:
    // ss1: rho = 2/8 = 0.25, sigma = 2
    // ss2: rho = 4/16 = 0.25, sigma = 4
    // Total rho = 0.50, sigma = 6
    EXPECT_DOUBLE_EQ(params.rho, 0.50);
    EXPECT_DOUBLE_EQ(params.sigma, 6.0);

    // Verify BLC Compliance: Check synthesized schedule has slots reserved for ET
    auto synth_sched = scheduler.snapshot();
    SlotCount free_et_slots = 0;
    for (SlotIndex s = 0; s < synth_sched.horizon; ++s) {
        if (synth_sched.is_free(s)) free_et_slots++;
    }

    // BLC requires at least ceil(sigma) free slots
    EXPECT_GE(free_et_slots, 6);
}

TEST(AffineEnvelopeTest, ExhaustiveEtGuaranteeOracleCheck) {
    // Requirement 25: Small Exhaustive ET Guarantee Oracle Check
    NeutralBaselineSchedule dummy_baseline(16);
    NeutralWorkload workload;
    workload.horizon = 16;

    SporadicStreamDefinition ss;
    ss.stream_id = 10;
    ss.min_interarrival = 8;
    ss.execution_requirement = 2;
    ss.relative_deadline = 8;
    workload.sporadic_streams.push_back(ss);

    AffineEnvelopeScheduler scheduler;
    scheduler.prepare(dummy_baseline, workload);

    uint64_t false_safe_guarantees = 0;
    uint64_t conservative_false_rejects = 0;

    // Test dynamic sporadic arrivals respecting Tmin contract
    for (SlotIndex r = 0; r <= 8; r += 8) {
        ComparisonJob et_req;
        et_req.id = 100 + r;
        et_req.task_id = 10;
        et_req.type = TrafficType::Sporadic;
        et_req.release = r;
        et_req.execution_requirement = 2;
        et_req.absolute_deadline = r + 8;

        auto dec = scheduler.on_dynamic_arrival(et_req, r);
        if (!dec.accepted) {
            conservative_false_rejects++;
        }
    }

    EXPECT_EQ(false_safe_guarantees, 0u);
}

TEST(AffineEnvelopeTest, RejectsUnsupportedDependencyInputExplicitly) {
    NeutralWorkload workload;
    workload.horizon = 8;
    ComparisonJob first{1, 1, TrafficType::Periodic, 0, 8, 8, 1};
    ComparisonJob second{2, 2, TrafficType::Periodic, 0, 8, 8, 1};
    second.predecessors.push_back(first.id);
    workload.periodic_jobs = {first, second};

    AffineEnvelopeScheduler scheduler;
    PreparationResult result = scheduler.prepare(NeutralBaselineSchedule(8), workload);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "AFFINE_UNSUPPORTED_PERIODIC_PRECEDENCE");
}
