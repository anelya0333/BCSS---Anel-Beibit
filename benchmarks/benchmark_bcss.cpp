#include <benchmark/benchmark.h>
#include "bcss/scheduler.hpp"
#include "bcss/hasher.hpp"
#include "bcss/rtc_guard.hpp"

using namespace bcss;

static void BM_DirectAllocation(benchmark::State& state) {
    BcssScheduler scheduler(64, 2, false);
    Schedule init_sched(64);
    Job tt_job(1, 10, TaskType::Periodic, 0, 30, 1);
    init_sched.assign_job(tt_job, 0);
    scheduler.set_periodic_baseline({tt_job}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 5, 10, 1);

    for (auto _ : state) {
        BcssResult r = scheduler.admit_dynamic_job(new_job, 0);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_DirectAllocation);

static void BM_BoundedCompensationSearch(benchmark::State& state) {
    BcssScheduler scheduler(32, 2, false);
    Schedule init_sched(32);
    Job p1(1, 10, TaskType::Periodic, 0, 32, 1); init_sched.assign_job(p1, 0);
    Job p2(2, 11, TaskType::Periodic, 0, 32, 1); init_sched.assign_job(p2, 1);
    scheduler.set_periodic_baseline({p1, p2}, init_sched);

    Job new_job(100, 1000, TaskType::OneShot, 0, 4, 1);

    for (auto _ : state) {
        BcssResult r = scheduler.admit_dynamic_job(new_job, 0);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_BoundedCompensationSearch);

static void BM_ScheduleHashingSHA256(benchmark::State& state) {
    Schedule sched(64);
    for (int i = 0; i < 30; ++i) {
        Job j(i, i + 10, TaskType::Periodic, i * 2, 64, 1);
        sched.assign_job(j, i * 2);
    }

    for (auto _ : state) {
        std::string hash = ScheduleHasher::compute_hash(sched);
        benchmark::DoNotOptimize(hash);
    }
}
BENCHMARK(BM_ScheduleHashingSHA256);

static void BM_RtcEnvelopeGuardCheck(benchmark::State& state) {
    Schedule sched(64);
    std::vector<SporadicStreamSpec> streams = {
        {1, 10, 1, 10},
        {2, 20, 2, 20}
    };
    DependencyGraph deps;
    std::string err;

    for (auto _ : state) {
        bool safe = RtcEnvelopeGuard::check_guard(sched, 0, streams, deps, err);
        benchmark::DoNotOptimize(safe);
    }
}
BENCHMARK(BM_RtcEnvelopeGuardCheck);

BENCHMARK_MAIN();
