# Pre-Evaluation Verification Report

## Gate result

The authoritative campaign gate is [`preflight/PREFLIGHT_REPORT.md`](../preflight/PREFLIGHT_REPORT.md).
The full campaign is blocked unless that report contains `PREFLIGHT: PASS` for
the current evaluation fingerprint.

## What the gate proves

- requested TT utilization, runtime profile, K, RTC, one-shot load, deadline
  ratios, multi-slot regimes, and dependency levels change generated inputs and
  recorded runtime fields;
- the direct, reclamation, one-hop compensation, two-hop compensation, K-bound
  rejection, RTC rejection, Tmin violation, and rejection-hash paths execute;
- rejected BCSS admissions preserve a real SHA-256 schedule fingerprint;
- Class-A algorithms receive identical taskset, trace, scenario, and baseline
  fingerprints;
- deliberately difficult workloads produce distinct algorithm results;
- repeated traces are reduced to a smaller taskset-level statistical table.

## Corrections made before the gate

- quarantined the obsolete hard-coded evaluation launchers and all invalid old
  outputs;
- replaced template telemetry with real per-admission scheduler measurements;
- fixed scenario/seed propagation and exact zero one-shot generation;
- fixed thesis-scale periodic/dynamic job-ID collisions;
- repaired generator utilization/profile/deadline/multi-slot/dependency behavior;
- corrected BCSS reclamation state synchronization and added observability;
- optimized the conservative RTC and DTSS RPCA predicates with exhaustive
  equivalence-oracle tests;
- made unsupported comparison scopes fail explicitly.

## Limitations carried into evaluation

- the clean-slate scheduler sources are not contained in repository commit
  `89e3a0e`; the campaign therefore records a worktree evaluation fingerprint
  and per-source SHA-256 hashes;
- common-mode Slot Shifting and DTSS have no separately validated offline
  protected-sporadic admission stage;
- Affine is dependency-free, unit-slot, and Class B;
- the implemented scheduler is a finite one-hyperperiod model.

These limitations do not block empirical execution, but they prevent an
unqualified thesis-ready provenance claim.
