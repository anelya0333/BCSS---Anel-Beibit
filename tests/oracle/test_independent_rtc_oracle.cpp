#include <gtest/gtest.h>
#include "bcss/rtc_guard.hpp"
#include <iostream>
#include <vector>
#include <set>

using namespace bcss;

// ============================================================================
// MAIN EXHAUSTIVE SWEEP: Single-stream, multi-pattern
// ============================================================================
TEST(IndependentRtcOracleTest, ExhaustiveVerificationOverBoundedSmallInstances) {
    uint64_t total_cases = 0;
    uint64_t rtc_safe_oracle_safe = 0;
    uint64_t rtc_unsafe_oracle_unsafe = 0;
    uint64_t conservative_false_rejects = 0;
    uint64_t false_safe_decisions = 0;

    std::vector<SlotCount> horizons = {8, 10, 12};
    std::vector<SlotCount> t_mins = {2, 3, 4, 5};
    std::vector<SlotCount> durations = {1, 2};
    std::vector<SlotCount> deadlines = {1, 2, 3, 4, 5};

    DependencyGraph empty_deps;

    for (SlotCount H : horizons) {
        for (SlotCount t_min : t_mins) {
            for (SlotCount dur : durations) {
                for (SlotCount D : deadlines) {
                    if (dur > D) continue;

                    SporadicStreamSpec str1{101, t_min, dur, D};
                    std::vector<SporadicStreamSpec> streams = {str1};

                    std::vector<Schedule> test_schedules;
                    
                    // Pattern 1: Empty schedule
                    test_schedules.push_back(Schedule(H));

                    // Pattern 2: Even slots occupied
                    Schedule s_even(H);
                    for (SlotIndex s = 0; s < H; s += 2) {
                        Job tt(1, s + 10, TaskType::Periodic, s, H, 1);
                        s_even.assign_job(tt, s);
                    }
                    test_schedules.push_back(s_even);

                    // Pattern 3: Front half blocked
                    Schedule s_front(H);
                    for (SlotIndex s = 0; s < H / 2; ++s) {
                        Job tt(1, s + 10, TaskType::Periodic, s, H, 1);
                        s_front.assign_job(tt, s);
                    }
                    test_schedules.push_back(s_front);

                    for (const auto& sched : test_schedules) {
                        for (SlotIndex t_now = 0; t_now < H / 2; ++t_now) {
                            std::string reason;
                            bool rtc_res = RtcEnvelopeGuard::check_guard(sched, t_now, streams, empty_deps, reason);
                            bool oracle_res = IndependentRtcOracle::is_safe(sched, t_now, streams);

                            total_cases++;

                            if (rtc_res && oracle_res) {
                                rtc_safe_oracle_safe++;
                            } else if (!rtc_res && !oracle_res) {
                                rtc_unsafe_oracle_unsafe++;
                            } else if (!rtc_res && oracle_res) {
                                conservative_false_rejects++;
                            } else if (rtc_res && !oracle_res) {
                                false_safe_decisions++;
                                std::cerr << "FALSE-SAFE: H=" << H << " t_now=" << t_now 
                                          << " T_min=" << t_min << " C=" << dur << " D=" << D << std::endl;
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "\n======================================================\n";
    std::cout << "EXHAUSTIVE RTC ORACLE VERIFICATION (single-stream)\n";
    std::cout << "======================================================\n";
    std::cout << "H range: {8, 10, 12}\n";
    std::cout << "Tmin range: {2, 3, 4, 5}\n";
    std::cout << "C range: {1, 2}\n";
    std::cout << "D range: {1, 2, 3, 4, 5}\n";
    std::cout << "Schedule patterns: 3 (empty, even-occupied, front-blocked)\n";
    std::cout << "t_now range: 0..H/2\n\n";
    std::cout << "Concrete RTC cases checked: " << total_cases << "\n\n";
    std::cout << "RTC SAFE  + oracle SAFE:    " << rtc_safe_oracle_safe << "\n";
    std::cout << "RTC UNSAFE + oracle UNSAFE: " << rtc_unsafe_oracle_unsafe << "\n";
    std::cout << "Conservative false rejects: " << conservative_false_rejects << "\n";
    std::cout << "FALSE-SAFE decisions:       " << false_safe_decisions << "\n";
    std::cout << "======================================================\n\n";

    EXPECT_EQ(false_safe_decisions, 0u) << "CRITICAL: FALSE-SAFE must be 0";
}

// ============================================================================
// MULTI-STREAM EXHAUSTIVE SWEEP
// ============================================================================
TEST(IndependentRtcOracleTest, MultiStreamExhaustive) {
    uint64_t total = 0, ss = 0, uu = 0, fr = 0, fs = 0;
    DependencyGraph empty_deps;

    // Two sporadic streams with different parameters
    std::vector<std::pair<SporadicStreamSpec, SporadicStreamSpec>> stream_pairs = {
        {{101, 4, 1, 4}, {102, 3, 1, 3}},  // Both C=1
        {{101, 4, 2, 4}, {102, 5, 1, 5}},  // Mixed C=2 and C=1
        {{101, 3, 1, 2}, {102, 4, 2, 4}},  // Tight D on first, C=2 on second
    };

    for (SlotCount H : {8, 10, 12}) {
        for (const auto& [s1, s2] : stream_pairs) {
            std::vector<SporadicStreamSpec> streams = {s1, s2};

            // Empty schedule and fragmented schedule
            Schedule empty(H);
            Schedule frag(H);
            for (SlotIndex s = 0; s < H; s += 3) {
                Job tt(1, s + 10, TaskType::Periodic, s, H, 1);
                frag.assign_job(tt, s);
            }

            for (const auto& sched : {empty, frag}) {
                for (SlotIndex t_now = 0; t_now < H / 2; ++t_now) {
                    std::string reason;
                    bool rtc = RtcEnvelopeGuard::check_guard(sched, t_now, streams, empty_deps, reason);
                    bool oracle = IndependentRtcOracle::is_safe(sched, t_now, streams);
                    total++;
                    if (rtc && oracle) ss++;
                    else if (!rtc && !oracle) uu++;
                    else if (!rtc && oracle) fr++;
                    if (rtc && !oracle) {
                        fs++;
                        std::cerr << "MULTI-STREAM FALSE-SAFE at H=" << H << " t_now=" << t_now
                                  << " rtc=" << rtc << " oracle=" << oracle
                                  << " sched=" << (sched.horizon) << " pair={" << s1.min_inter_arrival << "," << s1.duration << "," << s1.relative_deadline << "}"
                                  << " and {" << s2.min_inter_arrival << "," << s2.duration << "," << s2.relative_deadline << "}\n";
                    }
                }
            }
        }
    }

    std::cout << "\n======================================================\n";
    std::cout << "MULTI-STREAM RTC ORACLE VERIFICATION\n";
    std::cout << "======================================================\n";
    std::cout << "Stream pairs tested: 3\n";
    std::cout << "H range: {8, 10, 12}\n";
    std::cout << "Concrete cases: " << total << "\n";
    std::cout << "RTC SAFE + oracle SAFE:     " << ss << "\n";
    std::cout << "RTC UNSAFE + oracle UNSAFE: " << uu << "\n";
    std::cout << "Conservative false rejects: " << fr << "\n";
    std::cout << "FALSE-SAFE:                 " << fs << "\n";
    std::cout << "======================================================\n\n";
    EXPECT_EQ(fs, 0u);
}

// ============================================================================
// EXPLICIT CASE: C > 1 with fragmented free capacity
// ============================================================================
TEST(IndependentRtcOracleTest, Case_CGreaterThan1_Fragmented) {
    // H=10, slots 0,2,4,6,8 occupied -> free: {1,3,5,7,9} (5 isolated slots)
    Schedule sched(10);
    for (SlotIndex s = 0; s < 10; s += 2) {
        Job tt(1, s + 10, TaskType::Periodic, s, 10, 1);
        sched.assign_job(tt, s);
    }
    // Stream: C=2, Tmin=5, D=5
    SporadicStreamSpec str{101, 5, 2, 5};
    DependencyGraph deps;
    std::string reason;

    bool rtc = RtcEnvelopeGuard::check_guard(sched, 0, {str}, deps, reason);
    bool oracle = IndependentRtcOracle::is_safe(sched, 0, {str});

    std::cout << "Case C>1 fragmented: RTC=" << rtc << " Oracle=" << oracle << "\n";
    // Oracle should find this unsafe (no 2 contiguous free slots)
    EXPECT_FALSE(oracle) << "No contiguous C=2 block available";
    // RTC should also reject (conservative check)
    if (rtc && !oracle) ADD_FAILURE() << "FALSE-SAFE on C>1 fragmented case";
}

// ============================================================================
// EXPLICIT CASE: Enough total free slots but no contiguous block
// ============================================================================
TEST(IndependentRtcOracleTest, Case_TotalFreeEnoughButNoContiguousBlock) {
    // H=8: occupied={0,2,4,6}, free={1,3,5,7}. Total free=4, but max contiguous=1
    Schedule sched(8);
    for (SlotIndex s = 0; s < 8; s += 2) {
        Job tt(1, s + 10, TaskType::Periodic, s, 8, 1);
        sched.assign_job(tt, s);
    }
    SporadicStreamSpec str{101, 8, 3, 8}; // C=3, needs 3 contiguous
    DependencyGraph deps;
    std::string reason;

    bool rtc = RtcEnvelopeGuard::check_guard(sched, 0, {str}, deps, reason);
    bool oracle = IndependentRtcOracle::is_safe(sched, 0, {str});

    std::cout << "Case total-free-enough-no-contiguous: RTC=" << rtc << " Oracle=" << oracle << "\n";
    EXPECT_FALSE(oracle) << "C=3 needs 3 contiguous, only isolated slots";
    if (rtc && !oracle) ADD_FAILURE() << "FALSE-SAFE";
}

// ============================================================================
// EXPLICIT CASE: Enough free capacity but only after the deadline
// ============================================================================
TEST(IndependentRtcOracleTest, Case_FreeCapacityOnlyAfterDeadline) {
    // H=10: slots 0..4 occupied, slots 5..9 free. D=4 -> deadline from r=0 is slot 4.
    Schedule sched(10);
    for (SlotIndex s = 0; s < 5; ++s) {
        Job tt(1, s + 10, TaskType::Periodic, s, 10, 1);
        sched.assign_job(tt, s);
    }
    // Stream arrives at t=0, deadline D=4 -> must complete by slot 4. But slots 0..4 occupied!
    SporadicStreamSpec str{101, 10, 1, 4};
    DependencyGraph deps;
    std::string reason;

    bool rtc = RtcEnvelopeGuard::check_guard(sched, 0, {str}, deps, reason);
    bool oracle = IndependentRtcOracle::is_safe(sched, 0, {str});

    std::cout << "Case free-after-deadline: RTC=" << rtc << " Oracle=" << oracle << "\n";
    EXPECT_FALSE(oracle) << "Free capacity exists only after deadline";
    if (rtc && !oracle) ADD_FAILURE() << "FALSE-SAFE";
}

// ============================================================================
// EXPLICIT CASE: Delta=0, Delta=Tmin-1, Delta=Tmin, Delta=Tmin+1
// ============================================================================
TEST(IndependentRtcOracleTest, BoundaryDeltaEvaluation) {
    Schedule sched(16);
    for (SlotIndex s = 0; s < 4; ++s) {
        Job tt(1, s + 10, TaskType::Periodic, s, 16, 1);
        sched.assign_job(tt, s);
    }
    SporadicStreamSpec str{101, 4, 2, 4};
    DependencyGraph deps;
    std::string reason;

    // Test at various t_now values to exercise different effective deltas
    for (SlotIndex t_now : {0, 1, 2, 3, 4, 5}) {
        bool rtc = RtcEnvelopeGuard::check_guard(sched, t_now, {str}, deps, reason);
        bool oracle = IndependentRtcOracle::is_safe(sched, t_now, {str});
        std::cout << "Boundary t_now=" << t_now << ": RTC=" << rtc << " Oracle=" << oracle << "\n";
        if (rtc && !oracle) ADD_FAILURE() << "FALSE-SAFE at t_now=" << t_now;
    }
}

// ============================================================================
// EXPLICIT CASE: Already outstanding sporadic work (partial schedule occupancy by sporadic)
// ============================================================================
TEST(IndependentRtcOracleTest, Case_OutstandingSporadicWork) {
    // H=12. Sporadic job already placed at slots 2,3. Stream wants more capacity.
    Schedule sched(12);
    Job existing_sp(201, 5000, TaskType::Sporadic, 0, 12, 2);
    sched.assign_job(existing_sp, 2); // Sporadic job at slots 2-3

    // Periodic at slots 6,7
    Job tt1(1, 10, TaskType::Periodic, 6, 12, 2);
    sched.assign_job(tt1, 6);

    // Stream: Tmin=4, C=2, D=6
    SporadicStreamSpec str{101, 4, 2, 6};
    DependencyGraph deps;
    std::string reason;

    bool rtc = RtcEnvelopeGuard::check_guard(sched, 0, {str}, deps, reason);
    bool oracle = IndependentRtcOracle::is_safe(sched, 0, {str});

    std::cout << "Case outstanding-sporadic: RTC=" << rtc << " Oracle=" << oracle << "\n";
    if (rtc && !oracle) ADD_FAILURE() << "FALSE-SAFE with outstanding sporadic work";
}

// ============================================================================
// AGGREGATE SUMMARY
// ============================================================================
TEST(IndependentRtcOracleTest, AggregateSummary) {
    // This test just prints a summary header for the test output
    std::cout << "\n======================================================\n";
    std::cout << "RTC ORACLE VERIFICATION AGGREGATE\n";
    std::cout << "======================================================\n";
    std::cout << "All explicit cases above cover:\n";
    std::cout << "  [1] C > 1 with fragmented capacity\n";
    std::cout << "  [2] Total free enough but no contiguous block\n";
    std::cout << "  [3] Free capacity only after deadline\n";
    std::cout << "  [4] Multiple sporadic streams\n";
    std::cout << "  [5] Already outstanding sporadic work\n";
    std::cout << "  [6] Delta boundary values (0, Tmin-1, Tmin, Tmin+1)\n";
    std::cout << "  [7] Exhaustive single-stream sweep (1620 cases)\n";
    std::cout << "  [8] Exhaustive multi-stream sweep\n";
    std::cout << "  Note: dependency-restricted cases are not yet tested\n";
    std::cout << "        (deps are passed to check_guard but unused)\n";
    std::cout << "======================================================\n\n";
    SUCCEED();
}
