# Evaluation Defect and Rerun Log

## Invalid archived campaign

- Classification: generator, parameter propagation, telemetry, aggregation,
  plotting/provenance.
- Root cause: scenario labels were not propagated and the engine emitted a
  fixed telemetry template and placeholder hashes.
- Affected data: all 270,750 prior raw rows and every derived aggregate, test,
  report, and figure; E8B was absent.
- Required rerun: complete replacement. No archived row is eligible for reuse.

## Corrected-pipeline defects found before final launch

1. Generator: dynamic job IDs began at 2000 and collided with thesis-scale
   periodic instances. This caused false periodic misses and corrupted runtime
   behavior. Fixed with a disjoint ID range and a H=10,000 regression test.
2. Generator: requested utilization, profiles, zero one-shot load, trace seeds,
   deadlines, multi-slot regimes, and dependencies were incompletely propagated.
   Fixed and covered by pre-flight checks.
3. Telemetry/scheduler integration: BCSS job allocation metadata was not kept in
   sync, suppressing genuine reclamation. Fixed with a forced-mechanism test.
4. Comparison performance: DTSS RPCA used exhaustive interval/TEW rescanning at
   H=10,000. Replaced by an exact range-query implementation and checked against
   exhaustive enumeration on more than 100,000 small cases.
5. Comparison scope: common-mode Slot Shifting could not substantiate the same
   protected-sporadic admission claim, and Affine did not support dependencies
   or non-unit TT jobs. These are now explicit limitations instead of silent
   claims.
6. Telemetry/statistics: Affine synthesis-infeasible tasksets were initially
   counted as runtime periodic deadline misses because the empty failed-synthesis
   snapshot was inspected as though preparation had succeeded. Synthesis
   infeasibility is now a separate co-design outcome, its acceptance ratio is
   undefined/excluded, and only successfully prepared schedules are checked for
   runtime misses. The entire fingerprinted campaign was rerun after this fix.

All pre-fix pre-flight/benchmark rows are invalidated automatically by the
evaluation fingerprint. The final campaign starts only from the final passing
fingerprint.
