<div align="center">

<img src="assets/bcss-night-pink-banner.svg" alt="BCSS Simulator — bounded recourse, protected time" width="100%">

<img src="assets/bcss-title.svg" alt="BCSS Simulator" width="560">

**✦ A deterministic C++20 research simulator for online admission with bounded schedule recourse ✦**

![C++20](https://img.shields.io/badge/C%2B%2B-20-FF4FA3?style=for-the-badge&logo=cplusplus&logoColor=white)
![CMake 3.28+](https://img.shields.io/badge/CMake-3.28%2B-C026D3?style=for-the-badge&logo=cmake&logoColor=white)
![Tests](https://img.shields.io/badge/CTest-76%20passing-EC4899?style=for-the-badge&logo=checkmarx&logoColor=white)
![Status](https://img.shields.io/badge/status-research%20prototype-8B5CF6?style=for-the-badge)

</div>

BCSS studies how a time-triggered schedule can admit sporadic and one-shot work without allowing an admission decision to rewrite an unbounded part of the existing schedule. Each decision may move at most `K` previously scheduled jobs, must preserve the executed past, and is committed only after feasibility, dependency, and optional real-time-calculus (RTC) checks pass.

This repository contains the simulator, independent verification oracles, property and integration tests, comparison schedulers, benchmarks, and the tooling used for the bounded evaluation campaign.

## Why BCSS?

A static time-triggered schedule is predictable, but it can waste slack and react poorly to unexpected work. An unconstrained online scheduler may accept more work, but can create large and difficult-to-analyze schedule changes. BCSS explores the middle ground:

- **Direct allocation** uses contiguous free capacity without moving existing jobs (`k = 0`).
- **Reclamation** moves one eligible future job into earlier unused capacity (`k = 1`).
- **Bounded compensation** follows displacement chains while keeping the number of moved pre-existing jobs at or below `K`.
- **RTC protection** rejects candidate states that do not retain enough future service for offline-admitted sporadic streams.
- **Atomic decisions** either commit a fully validated schedule or leave the active schedule unchanged.

The online admission path is:

```text
incoming dynamic job
        │
        ├── validate release, deadline, duration, and T_min contract
        │
        ├── direct contiguous placement ────────────────────────┐
        │                                                       │
        ├── unused-capacity reclamation (when K >= 1)  ─────────┤
        │                                                       ├── validate
        └── bounded compensation search (up to K moved jobs)  ──┤   dependencies
                                                                │   RTC envelope
                                                                │   past immutability
                                                                ▼
                                                         commit or rollback
```

Compensation candidates are ranked lexicographically by:

```text
(number of moved jobs, maximum displacement, total displacement)
```

This favors the smallest schedule change first, then the least severe displacement.

## System model

The implementation uses a finite, discrete-time horizon and non-preemptive, contiguous job executions. It models three traffic classes:

| Traffic class | Role | Important parameters |
|---|---|---|
| Periodic | Precomputed time-triggered baseline | release, deadline, execution time, period |
| Sporadic | Dynamic jobs belonging to an offline-admitted stream | release, deadline, execution time, minimum inter-arrival time `T_min` |
| One-shot | Aperiodic dynamic requests | release, deadline, execution time |

Jobs may have precedence parents. The scheduler propagates dependency windows and rejects schedules that violate release times, deadlines, contiguous execution, precedence, the recourse bound, or past immutability. Schedule states have canonical SHA-256 fingerprints, making successful commits and rejected rollbacks independently checkable.

## Repository map

```text
.
├── CMakeLists.txt          # Targets, dependencies, warnings, and test registration
├── CMakePresets.json       # Debug, release, sanitizer, coverage, and benchmark builds
├── include/bcss/           # Public BCSS data structures and APIs
├── src/                    # Scheduler, guards, validators, workload generator, and CLI
├── tests/
│   ├── unit/               # Component-level behavior
│   ├── integration/        # Handcrafted end-to-end scheduling scenarios
│   ├── oracle/             # Exhaustive small-state and independent RTC checks
│   └── property/           # Invariants, metamorphic checks, and malformed-input fuzzing
├── benchmarks/             # Google Benchmark microbenchmarks
├── comparisons/            # Neutral model and comparison scheduler implementations
└── evaluation/             # Campaign design, execution, validation, and plotting tools
```

Generated builds, campaign outputs, caches, logs, archives, and review bundles are intentionally local-only. The root `.gitignore` is an allowlist: a new top-level public directory must be explicitly added there before Git will stage it.

## Requirements

- A C++20 compiler (GCC 13+ or a recent Clang is recommended)
- CMake 3.28 or newer
- OpenSSL development headers and crypto library
- Python 3.10+ for evaluation tooling
- Matplotlib for plot generation
- Internet access during the first CMake configure, because GoogleTest `v1.15.2` and Google Benchmark `v1.9.0` are fetched from their pinned upstream releases

On Debian or Ubuntu, the native dependencies are typically provided by `build-essential`, `libssl-dev`, `python3`, and `python3-matplotlib`. Install CMake separately if the distribution package is older than 3.28.

## Quick start

```bash
git clone git@github.com:anelya0333/bcss-simulator.git
cd bcss-simulator

cmake --preset debug
cmake --build --preset debug -j
ctest --test-dir build/debug --output-on-failure

./build/debug/bcss_cli
```

The CLI runs deterministic quiet, normal, and busy workload profiles followed by a controlled mechanism-ablation experiment.

## Build presets

| Preset | Purpose | Output directory |
|---|---|---|
| `debug` | Assertions and developer testing | `build/debug` |
| `release` | Optimized simulator and evaluation workers | `build/release` |
| `asan-ubsan` | AddressSanitizer and UndefinedBehaviorSanitizer | `build/sanitizers` |
| `coverage` | GCC/Clang coverage instrumentation | `build/coverage` |
| `benchmark` | `-O3` benchmark build | `build/benchmark` |

Every preset follows the same pattern:

```bash
cmake --preset release
cmake --build --preset release -j
```

## Verification

The CTest suite currently discovers **76 tests** across five complementary layers:

- unit tests for schedule state, hashing, dependency propagation, validation, RTC checks, search, workload generation, and offline sporadic admission;
- integration scenarios for direct allocation, reclamation, compensation, multi-hop displacement, exact `K`, cycle prevention, multi-slot jobs, rollback, and simultaneous requests;
- exhaustive small-state checks against an independent feasibility oracle;
- an independent RTC oracle over bounded future arrival sequences;
- property, metamorphic, malformed-input, randomized comparison, and fairness-fingerprint tests.

Run the standard suite:

```bash
ctest --test-dir build/debug --output-on-failure
```

Run it under sanitizers:

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan -j
ctest --test-dir build/sanitizers --output-on-failure
```

List or filter discovered tests:

```bash
ctest --test-dir build/debug -N
ctest --test-dir build/debug -R 'Rtc|Oracle' --output-on-failure
```

## Executables

After a debug build, the main targets are:

| Target | Path | Purpose |
|---|---|---|
| `bcss_cli` | `build/debug/bcss_cli` | Deterministic simulator demonstration |
| `bcss_benchmarks` | `build/debug/bcss_benchmarks` | Allocation, compensation, hashing, and RTC microbenchmarks |
| `comparison_runner` | `build/debug/comparisons/comparison_runner` | Comparison-scheduler smoke runner |
| `evaluation_worker` | `build/debug/comparisons/evaluation_worker` | One-process-per-run campaign worker |
| `preflight_mechanisms` | `build/debug/comparisons/preflight_mechanisms` | Deterministic mechanism probes |

For example:

```bash
./build/benchmark/bcss_benchmarks
./build/debug/comparisons/comparison_runner
```

## Using the library

`bcss_core` is a static library with headers under `include/bcss`. A minimal admission flow looks like this:

```cpp
#include "bcss/scheduler.hpp"

using namespace bcss;

Schedule baseline(32);
Job periodic(1, 10, TaskType::Periodic, 0, 20, 1, 32);
baseline.assign_job(periodic, 4);

BcssScheduler scheduler(/* horizon = */ 32, /* K = */ 2, /* RTC = */ true);
if (!scheduler.set_periodic_baseline({periodic}, baseline)) {
    return 1;
}

Job request(100, 1000, TaskType::OneShot, 0, 12, 2);
BcssResult result = scheduler.admit_dynamic_job(request, /* t_now = */ 0);

if (result.success) {
    // result.schedule is committed; result.actual_k reports moved existing jobs.
} else {
    // The active schedule is unchanged and the hashes expose rollback integrity.
}
```

`BcssResult` also reports the decision mechanism, displacement metrics, rejection reason, search effort, candidate counts, RTC outcomes, and search latency.

## Comparison scope

The comparison layer provides Static Direct, Slot Shifting, DTSS, BCSS, and an Affine Envelope reference behind a neutral workload model. Static Direct, Slot Shifting, DTSS, and BCSS can receive matched tasksets, traces, and baseline fingerprints. The Affine implementation synthesizes its own time-triggered schedule and is therefore a separate co-design reference rather than a paired same-schedule baseline.

The implementations deliberately do not inject BCSS-specific `K`, ranking, or RTC rules into other algorithms. Their protected-sporadic semantics are not identical, so one-shot acceptance is the cleanest directly shared comparison denominator; pooled dynamic acceptance should not be presented as a universal leaderboard.

See `comparisons/COMPARISON_IMPLEMENTATION_REPORT.md` and `comparisons/docs/comparability_matrix.md` for the precise boundaries.

## Reproducible evaluation

Build both the debug probes and release worker first:

```bash
cmake --preset debug
cmake --build --preset debug -j
cmake --preset release
cmake --build --preset release -j
```

Then use the campaign driver:

```bash
# Fast correctness and mechanism gate
python3 evaluation/final_evaluation.py preflight --workers 4

# Generate the campaign design and manifest
python3 evaluation/final_evaluation.py design --workers 4

# Execute or resume the complete campaign (computationally expensive)
python3 evaluation/final_evaluation.py campaign --workers 10

# Revalidate existing raw rows and regenerate statistics/reports
python3 evaluation/final_evaluation.py validate --workers 10

# Generate plots from validated results
python3 evaluation/plot_final_results.py
```

Seeds, manifests, scenario inputs, baseline schedules, final schedules, and evaluated source files are fingerprinted for traceability. Campaign outputs are written to local generated-output directories and are ignored by Git, keeping routine evaluation runs out of source-control diffs.

The evaluated source snapshot is tagged `bcss-final-evaluated-2026-08-11`. Its recorded freeze suite passed 76/76 tests. That is bounded experimental evidence for the evaluated finite-horizon workloads—not a proof of universal schedulability or a safety certification.

## Design invariants

Every candidate transition is checked for:

1. valid horizon, releases, deadlines, durations, and contiguous allocation;
2. preservation of every pre-existing scheduled job;
3. immutability of slots before `t_now`;
4. satisfaction of precedence dependencies;
5. movement of no more than `K` distinct pre-existing jobs;
6. preservation of admitted sporadic capacity when the RTC guard is enabled;
7. atomic rollback on rejection, observable through equal pre/post schedule hashes.

These checks are implemented and heavily tested, but the project remains a research simulator. Production real-time deployment requires platform-specific timing analysis, WCET evidence, and independent assurance beyond this repository.

---

<div align="center">

**Amde with love** 🎀

</div>
