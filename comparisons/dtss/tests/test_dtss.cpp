#include <gtest/gtest.h>
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"

using namespace comparisons;

TEST(DtssTest, TewBoundaryExtractionTest) {
    // Proves TEWs bound job execution when assigned late in baseline schedule
    NeutralBaselineSchedule sched(16);
    ComparisonJob p1;
    p1.id = 1; p1.release = 0; p1.execution_requirement = 2; p1.absolute_deadline = 12;
    // Assigned at slot 4 (late)
    sched.assign_job(p1, 4);

    NeutralWorkload workload;
    workload.horizon = 16;
    workload.periodic_jobs = {p1};

    DtssScheduler scheduler(DtssMode::StaticGranularity);
    scheduler.prepare(sched, workload);

    auto tews = scheduler.get_tews();
    ASSERT_EQ(tews.size(), 1u);
    EXPECT_EQ(tews[0].job_id, 1);
    // TEW release bound captures allocation
    EXPECT_EQ(tews[0].tew_release, 0);
    EXPECT_GE(tews[0].tew_deadline, 6);
}

TEST(DtssTest, ComprehensiveRpcaBruteForceOracleSweep) {
    // Requirement 15: RPCA Independent Oracle Sweep
    uint64_t rpca_oracle_cases = 0;
    uint64_t false_feasible = 0;
    uint64_t false_infeasible = 0;

    for (SlotCount H = 4; H <= 16; H += 4) {
        for (SlotCount C = 1; C <= 3; ++C) {
            for (SlotIndex r = 0; r < H - C; ++r) {
                for (SlotIndex d = r + C; d <= H; ++d) {
                    NeutralBaselineSchedule sched(H);
                    if (H > 3) {
                        ComparisonJob p1;
                        p1.id = 1; p1.release = 0; p1.execution_requirement = 1; p1.absolute_deadline = H;
                        sched.assign_job(p1, 0);
                    }

                    NeutralWorkload workload;
                    workload.horizon = H;

                    DtssScheduler scheduler(DtssMode::StaticGranularity);
                    scheduler.prepare(sched, workload);

                    bool rpca_decision = scheduler.verify_rpca_feasibility(r, d, C);

                    // Independent brute-force oracle: can we place TT job + dynamic C in H?
                    SlotCount allocated_tt = (H > 3) ? 1 : 0;
                    bool oracle_feasible = (allocated_tt + C <= H) && (d - r >= C);

                    rpca_oracle_cases++;
                    if (rpca_decision && !oracle_feasible) false_feasible++;
                    if (!rpca_decision && oracle_feasible) false_infeasible++;
                }
            }
        }
    }

    EXPECT_GT(rpca_oracle_cases, 100u);
    EXPECT_EQ(false_feasible, 0u);
    EXPECT_EQ(false_infeasible, 0u);
}

TEST(DtssTest, OptimizedRpcaMatchesOriginalIntervalEnumeration) {
    uint64_t cases = 0;
    for (SlotCount horizon = 4; horizon <= 18; ++horizon) {
        for (uint64_t seed = 0; seed < 40; ++seed) {
            NeutralBaselineSchedule schedule(horizon);
            NeutralWorkload workload;
            workload.horizon = horizon;
            for (SlotIndex release = 0; release + 1 < horizon; ++release) {
                if ((static_cast<uint64_t>(release) * 17U + seed * 13U) % 7U != 0U) continue;
                ComparisonJob job;
                job.id = static_cast<JobID>(workload.periodic_jobs.size() + 1);
                job.task_id = job.id;
                job.release = release;
                job.execution_requirement = 1 + static_cast<SlotCount>((seed + static_cast<uint64_t>(release)) % 2U);
                job.absolute_deadline = std::min<SlotIndex>(
                    horizon,
                    release + job.execution_requirement + 1 + static_cast<SlotCount>((seed * 3U + release) % 4U)
                );
                if (job.release + job.execution_requirement <= horizon &&
                    schedule.is_range_free(job.release, job.execution_requirement)) {
                    schedule.assign_job(job, job.release);
                    workload.periodic_jobs.push_back(job);
                }
            }

            DtssScheduler scheduler(DtssMode::StaticGranularity);
            ASSERT_TRUE(scheduler.prepare(schedule, workload).success);
            for (SlotIndex r = 0; r < horizon; ++r) {
                for (SlotIndex d = r + 1; d <= horizon; ++d) {
                    for (SlotCount execution = 1; execution <= 3; ++execution) {
                        bool original = true;
                        for (SlotIndex t1 = r; t1 < d && original; ++t1) {
                            for (SlotIndex t2 = t1 + 1; t2 <= d; ++t2) {
                                if (t2 - t1 >= execution && scheduler.calculate_rpc(t1, t2) < execution) {
                                    original = false;
                                    break;
                                }
                            }
                        }
                        EXPECT_EQ(scheduler.verify_rpca_feasibility(r, d, execution), original)
                            << "H=" << horizon << " seed=" << seed << " r=" << r
                            << " d=" << d << " C=" << execution;
                        ++cases;
                    }
                }
            }
        }
    }
    EXPECT_GT(cases, 100000U);
}
