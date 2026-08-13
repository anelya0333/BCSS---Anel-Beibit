#include <gtest/gtest.h>
#include "bcss/rtc_guard.hpp"
#include <algorithm>
#include <random>

using namespace bcss;

TEST(RtcGuardTest, ArrivalCurveCalculation) {
    std::vector<SporadicStreamSpec> streams = {
        {1, 10, 1, 10}, // T_min=10, C=1, D=10
        {2, 20, 2, 20}  // T_min=20, C=2, D=20
    };

    // Demand bound function DBF(Δ) = Σ_{i : Δ ≥ D_i} ⌈Δ / T_min_i⌉ × C_i
    EXPECT_EQ(RtcEnvelopeGuard::arrival_curve(streams, 0), 0);
    // Δ=5 < D1(10), D2(20) -> 0
    EXPECT_EQ(RtcEnvelopeGuard::arrival_curve(streams, 5), 0);
    // Δ=10 >= D1(10), < D2(20) -> 1*1 = 1
    EXPECT_EQ(RtcEnvelopeGuard::arrival_curve(streams, 10), 1);
    // Δ=20 >= D1(10), >= D2(20) -> 2*1 + 1*2 = 4
    EXPECT_EQ(RtcEnvelopeGuard::arrival_curve(streams, 20), 4);
    // Δ=25 -> 3*1 + 2*2 = 7
    EXPECT_EQ(RtcEnvelopeGuard::arrival_curve(streams, 25), 7);
}

namespace {

SlotCount slow_boundary_demand(const std::vector<SporadicStreamSpec>& streams, SlotCount delta) {
    SlotCount total = 0;
    for (const auto& stream : streams) {
        if (stream.min_inter_arrival <= 0 || delta < stream.duration) continue;
        const SlotCount arrivals = (delta + stream.min_inter_arrival - 1) / stream.min_inter_arrival;
        total += arrivals * stream.duration;
    }
    return total;
}

bool slow_guard(const Schedule& schedule, SlotIndex t_now, const std::vector<SporadicStreamSpec>& streams) {
    if (streams.empty()) return true;
    for (SlotIndex start = t_now; start < schedule.horizon; ++start) {
        SlotCount free_slots = 0;
        SlotCount current_run = 0;
        SlotCount maximum_run = 0;
        for (SlotIndex end = start + 1; end <= schedule.horizon; ++end) {
            if (schedule.slots[static_cast<size_t>(end - 1)].is_free()) {
                ++free_slots;
                ++current_run;
                maximum_run = std::max(maximum_run, current_run);
            } else {
                current_run = 0;
            }
            const SlotCount delta = end - start;
            const SlotCount demand = end == schedule.horizon
                ? slow_boundary_demand(streams, delta)
                : RtcEnvelopeGuard::arrival_curve(streams, delta);
            if (free_slots < demand) return false;
            for (const auto& stream : streams) {
                if (stream.duration > 1 && demand > 0 && maximum_run < stream.duration) return false;
            }
        }
    }
    return true;
}

} // namespace

TEST(RtcGuardTest, OptimizedGuardMatchesOriginalIntervalScan) {
    std::mt19937_64 rng(0xBC55U);
    DependencyGraph dependencies;
    for (SlotCount horizon : {8, 12, 20, 32}) {
        for (int sample = 0; sample < 250; ++sample) {
            Schedule schedule(horizon);
            for (SlotIndex slot = 0; slot < horizon; ++slot) {
                if ((rng() % 100U) < 55U) {
                    schedule.slots[static_cast<size_t>(slot)].job_id = 1000 + slot;
                }
            }
            std::vector<SporadicStreamSpec> streams = {
                {1, 3 + static_cast<SlotCount>(rng() % 7U), 1, 1 + static_cast<SlotCount>(rng() % 6U)},
                {2, 5 + static_cast<SlotCount>(rng() % 9U), 2, 2 + static_cast<SlotCount>(rng() % 7U)}
            };
            for (auto& stream : streams) {
                stream.relative_deadline = std::max(stream.relative_deadline, stream.duration);
            }
            const SlotIndex t_now = static_cast<SlotIndex>(rng() % static_cast<uint64_t>(horizon));
            std::string reason;
            EXPECT_EQ(
                RtcEnvelopeGuard::check_guard(schedule, t_now, streams, dependencies, reason),
                slow_guard(schedule, t_now, streams)
            ) << "H=" << horizon << " sample=" << sample << " t_now=" << t_now;
        }
    }
}
