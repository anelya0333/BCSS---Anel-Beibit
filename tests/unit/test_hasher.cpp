#include <gtest/gtest.h>
#include "bcss/hasher.hpp"
#include "bcss/scheduler.hpp"
#include <iostream>
#include <iomanip>

using namespace bcss;

// 1. Identical logical schedules constructed independently
TEST(HasherTest, IdenticalLogicalSchedulesConstructedIndependently) {
    Schedule s1(4);
    Schedule s2(4);
    EXPECT_EQ(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 2. Differing insertion order but identical logical schedule
TEST(HasherTest, DifferingInsertionOrderSameLogicalSchedule) {
    Schedule s1(8);
    Schedule s2(8);

    Job a(1, 10, TaskType::Periodic, 0, 8, 1);
    Job b(2, 20, TaskType::Periodic, 2, 8, 1);

    // s1: insert A then B
    s1.assign_job(a, 0);
    s1.assign_job(b, 2);

    // s2: insert B then A
    s2.assign_job(b, 2);
    s2.assign_job(a, 0);

    EXPECT_EQ(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 3. One-slot change
TEST(HasherTest, OneSlotChange) {
    Schedule s1(8);
    Schedule s2(8);
    Job j(1, 10, TaskType::Periodic, 0, 8, 1);
    s1.assign_job(j, 3);
    // s2 is empty
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 4. Job-ID change
TEST(HasherTest, JobIDChange) {
    Schedule s1(8);
    Schedule s2(8);
    Job j1(1, 10, TaskType::Periodic, 0, 8, 1);
    Job j2(1, 20, TaskType::Periodic, 0, 8, 1); // Different job_id
    s1.assign_job(j1, 3);
    s2.assign_job(j2, 3);
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 5. Allocation-start change
TEST(HasherTest, AllocationStartChange) {
    Schedule s1(8);
    Schedule s2(8);
    Job j(1, 10, TaskType::Periodic, 0, 8, 1);
    s1.assign_job(j, 2);
    s2.assign_job(j, 5);
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 6. Allocation-length change (C=1 vs C=2)
TEST(HasherTest, AllocationLengthChange) {
    Schedule s1(8);
    Schedule s2(8);
    Job j1(1, 10, TaskType::Periodic, 0, 8, 1); // C=1
    Job j2(1, 10, TaskType::Periodic, 0, 8, 2); // C=2
    s1.assign_job(j1, 0);
    s2.assign_job(j2, 0);
    // Slot 1 is free in s1 but occupied in s2
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 7. Reservation-state change (free vs occupied Periodic)
TEST(HasherTest, ReservationStateChange) {
    Schedule s1(4);
    Schedule s2(4);
    Job j(1, 10, TaskType::Periodic, 0, 4, 1);
    s2.assign_job(j, 0);
    EXPECT_NE(ScheduleHasher::compute_hash(s1), ScheduleHasher::compute_hash(s2));
}

// 8. Rejected operation: pre_hash == post_hash
TEST(HasherTest, RejectedOperationPreEqualsPostHash) {
    BcssScheduler scheduler(8, 0, false);
    Schedule baseline(8);
    // Fill all 8 slots with periodic jobs
    std::vector<Job> tt_jobs;
    for (SlotIndex s = 0; s < 8; ++s) {
        Job tt(s, s + 100, TaskType::Periodic, s, 8, 1);
        baseline.assign_job(tt, s);
        tt_jobs.push_back(tt);
    }
    scheduler.set_periodic_baseline(tt_jobs, baseline);

    // Try to admit job into fully occupied schedule with K=0
    Job impossible(99, 999, TaskType::OneShot, 0, 4, 1);
    BcssResult r = scheduler.admit_dynamic_job(impossible, 0);

    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.pre_schedule_hash, r.post_schedule_hash)
        << "Rejected admission must not modify schedule hash";
}

// 9. Accepted operation: candidate hash == committed hash
TEST(HasherTest, AcceptedOperationCandidateEqualsCommittedHash) {
    BcssScheduler scheduler(8, 2, false);
    Schedule baseline(8);
    scheduler.set_periodic_baseline({}, baseline);

    Job new_job(1, 100, TaskType::OneShot, 0, 4, 1);
    BcssResult r = scheduler.admit_dynamic_job(new_job, 0);

    EXPECT_TRUE(r.success);
    EXPECT_NE(r.pre_schedule_hash, r.post_schedule_hash);
    // Post-hash must match recomputed hash of committed schedule
    std::string recomputed = ScheduleHasher::compute_hash(scheduler.active_schedule);
    EXPECT_EQ(r.post_schedule_hash, recomputed)
        << "Candidate hash must equal committed schedule hash";
}

// 10. Hash length is exactly 64 hex chars
TEST(HasherTest, HashLengthIs64HexChars) {
    Schedule s(4);
    std::string h = ScheduleHasher::compute_hash(s);
    EXPECT_EQ(h.length(), 64u);
    for (char c : h) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Hash must be lowercase hex, got: " << c;
    }
}

// 11. Known test vector
TEST(HasherTest, KnownTestVector) {
    // Schedule(4) with all slots free.
    // Canonical serialization (big-endian):
    //   Version: 0x01
    //   Horizon: 0x0000000000000004
    //   Slot 0: index=0x0000000000000000, job_id=0xFFFFFFFFFFFFFFFF, type=0x00,
    //           release=0xFFFFFFFFFFFFFFFF, deadline=0xFFFFFFFFFFFFFFFF, period=0xFFFFFFFFFFFFFFFF
    //   Slot 1: index=0x0000000000000001, ... (same free pattern)
    //   Slot 2: index=0x0000000000000002, ...
    //   Slot 3: index=0x0000000000000003, ...
    //
    // Total bytes: 1 + 8 + 4*(8+8+1+8+8+8) = 1 + 8 + 4*41 = 173 bytes

    Schedule s(4);
    std::vector<uint8_t> canonical = s.serialize_canonical();
    EXPECT_EQ(canonical.size(), 173u);

    // Verify version byte
    EXPECT_EQ(canonical[0], 0x01);

    // Compute and print the known hash
    std::string hash = ScheduleHasher::compute_hash(s);
    std::cout << "\n=== KNOWN TEST VECTOR ===\n";
    std::cout << "Schedule: H=4, all slots free\n";
    std::cout << "Canonical serialization size: " << canonical.size() << " bytes\n";
    std::cout << "Canonical bytes (hex): ";
    for (size_t i = 0; i < canonical.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(canonical[i]);
    }
    std::cout << "\nSHA-256: " << hash << "\n";
    std::cout << "=========================\n\n";

    // Hard-coded expected value (deterministic across compilers).
    // Canonical: version=0x01, H=4, 4 free slots with job_id=-1, type=0x00, release/deadline/period=-1
    // SHA-256 of the 173-byte canonical serialization:
    const std::string expected_hash = "18292654c870be7808d8de5b2625e652823f0cb28bdbf1e029c87d3b7e084629";
    EXPECT_EQ(hash, expected_hash)
        << "Known test vector mismatch. If canonical serialization format changed, update this value.";
}
