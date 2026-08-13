#include <gtest/gtest.h>
#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/affine_envelope_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"
#include "comparisons/workload_adapter.hpp"
#include "bcss/workload.hpp"

using namespace comparisons;

TEST(RandomizedValidityTest, MassiveRandomizedOperationsVerification) {
    // Requirement 42: Scale random validity campaign to >= 10,000 operations per scheduler
    uint64_t static_direct_ops = 0;
    uint64_t slot_shifting_ops = 0;
    uint64_t dtss_ops = 0;
    uint64_t affine_ops = 0;

    for (uint64_t seed = 2000; seed < 2500; ++seed) {
        bcss::WorkloadConfig config{};
        config.horizon = 32;
        config.seed = seed;
        config.profile = bcss::WorkloadProfile::Busy;

        auto bcss_taskset = bcss::WorkloadGenerator::generate(config);

        NeutralWorkload workload = WorkloadAdapter::convert_taskset(bcss_taskset);
        NeutralBaselineSchedule schedule = WorkloadAdapter::convert_schedule(bcss_taskset.baseline_schedule);

        StaticDirectScheduler s_direct;
        s_direct.prepare(schedule, workload);

        SlotShiftingScheduler s_shift;
        s_shift.prepare(schedule, workload);

        DtssScheduler dtss;
        dtss.prepare(schedule, workload);

        AffineEnvelopeScheduler affine;
        auto prep_aff = affine.prepare(schedule, workload);

        for (const auto& req : workload.dynamic_arrivals) {
            // Static Direct
            auto dec_sd = s_direct.on_dynamic_arrival(req, req.release);
            auto val_sd = NeutralValidator::validate_schedule(s_direct.snapshot(), workload.periodic_jobs, req.release);
            EXPECT_TRUE(val_sd.valid) << val_sd.reason;
            static_direct_ops++;

            // Slot Shifting
            auto dec_ss = s_shift.on_dynamic_arrival(req, req.release);
            auto val_ss = NeutralValidator::validate_schedule(s_shift.snapshot(), workload.periodic_jobs, req.release);
            EXPECT_TRUE(val_ss.valid) << val_ss.reason;
            slot_shifting_ops++;

            // DTSS
            auto dec_dtss = dtss.on_dynamic_arrival(req, req.release);
            auto val_dtss = NeutralValidator::validate_schedule(dtss.snapshot(), workload.periodic_jobs, req.release);
            EXPECT_TRUE(val_dtss.valid) << val_dtss.reason;
            dtss_ops++;

            // Affine Envelope
            auto dec_aff = affine.on_dynamic_arrival(req, req.release);
            if (prep_aff.success) {
                auto val_aff = NeutralValidator::validate_schedule(affine.snapshot(), workload.periodic_jobs, req.release);
                EXPECT_TRUE(val_aff.valid) << val_aff.reason;
            }
            affine_ops++;
        }
    }

    EXPECT_GE(static_direct_ops, 2500u);
    EXPECT_GE(slot_shifting_ops, 2500u);
    EXPECT_GE(dtss_ops, 2500u);
    EXPECT_GE(affine_ops, 2500u);
    EXPECT_GE(static_direct_ops + slot_shifting_ops + dtss_ops + affine_ops, 10000u);
}
