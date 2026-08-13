# Canonical Evaluation Data Schema

`results_final/raw/all_runs.csv` contains one row per deterministic manifest
run. The executable header is authoritative and is copied to
`schema_columns.txt` in the review bundle.

## Field groups

- Identity and pairing: `run_id`, experiment/scenario/algorithm, taskset and
  trace IDs, algorithm mode.
- Seed hierarchy: master, experiment, scenario, taskset, and trace seeds.
- Provenance: scheduler reference label, evaluation worktree fingerprint, five
  input/schedule SHA-256 fingerprints.
- Timing/model: slot quantum, horizon, hyperperiod, duration, warm-up, and
  measurement periods.
- Parameters: stream counts, requested and actual loads, profile, K, RTC,
  ablation flags, dependency level/edges, multi-slot regime, deadline ratios,
  and burst mode.
- Outcomes: explicit sporadic/one-shot/dynamic denominators and accepted/rejected
  counts, offline stream decisions, compliant arrivals, and contract violations.
- BCSS telemetry: mechanisms, actual-k distribution, displacement, candidates,
  RTC outcomes, search effort, and decision latencies.
- Independent safety checks: periodic/protected misses, dependency/K/past/hash/
  rollback violations, preparation result, and status.

## Validation rules

The validator enforces exact header order, finite numeric values, enums,
manifest/raw bijection, unique run IDs, manifest-to-runtime propagation, real
64-character lowercase SHA-256 values, zero arrivals at zero one-shot load,
`max_actual_k <= K`, and paired Class-A fingerprints.

Trace rows are aggregated first with keys:

`experiment + scenario_id + algorithm + taskset_id`

Only then are statistics computed across independent tasksets.
