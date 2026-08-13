# Comparison Implementation Report

This report describes the implementations actually evaluated. It does not
claim feature parity with every capability of the cited original methods.

## Evaluation scope

| Algorithm | Evaluation class | Common initial schedule | Implemented runtime behavior | Offline sporadic admission |
|---|---|---:|---|---|
| StaticDirect | A | Yes | Contiguous free capacity only | None |
| Slot Shifting | A | Yes | Capacity-interval/leeway checks and future-job relocation in non-preemptive common mode | None in common mode; implemented only in paper-native mode |
| DTSS | A | Yes | TEW/RPCA filter followed by contiguous free-slot placement | None |
| BCSS | A | Yes | Direct, reclamation, bounded compensation, RTC filter | BCSS offline gate |
| Affine Envelope | B | No; synthesizes its own TT schedule | Affine-envelope/BLC synthesis and service gaps | Co-design model |

Class-A rows receive identical neutral tasksets, traces, dependencies, timing
requirements, and baseline fingerprints. Affine is reported separately because
it synthesizes a different TT schedule.

## Deliberate boundaries

- BCSS-specific K, recourse ranking, and RTC rules are not injected into the
  external baselines.
- StaticDirect never relocates an existing job.
- Slot Shifting common mode preserves contiguous multi-slot communication
  allocations, but does not claim protected sporadic offline admission. Its
  paper-native mode retains its separate offline-admission test.
- DTSS uses its implemented TEW/RPCA predicate. The exact predicate is evaluated
  with an optimized range-query algorithm that is regression-tested against the
  original exhaustive interval enumeration.
- Affine rejects dependency-bearing or non-unit-slot TT inputs rather than
  silently evaluating an unsupported configuration.

## Known comparability limitations

StaticDirect, DTSS, and common-mode Slot Shifting do not provide a separately
validated offline protection contract equivalent to BCSS. Their sporadic
arrival acceptance remains useful as offered-work behavior, but it must not be
described as the same protected-service guarantee. Affine is a Class-B
co-design reference and is not a paired same-schedule claim.

## Verification basis

The comparison suite includes direct-placement checks, Slot Shifting capacity
and relocation cases, an exhaustive small-instance DTSS RPCA equivalence
oracle, Affine scope/synthesis tests, randomized validity checks, and paired
SHA-256 input-fingerprint checks. Current test counts and outcomes are recorded
by the final CTest log rather than frozen in this document.
