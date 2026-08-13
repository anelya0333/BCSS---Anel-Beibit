# Common Traffic Class Mapping

| Common Traffic Class | Static Direct | Slot Shifting | DTSS | BCSS | Affine Envelope |
|----------------------|---------------|---------------|------|------|-----------------|
| **PERIODIC (TT)** | Native (Immutable) | Native (Flexible shifting) | Native (TEW bounded) | Native (Immutable / Compensated) | Native (Synthesized via LLF+BLC) |
| **SPORADIC** | Native (Slack only) | Native (Isovic 2009 / 2025 Offline Test) | Native (RPCA Window Check) | Native (Offline Gate + RTC Guard) | Native (ET Model) |
| **ONE_SHOT** | Native (Slack only) | Native (Fohler 1995 Aperiodic Leeway) | Native (RPCA Window Check) | Native (K-Recourse Stealing) | Native (ET Service Gaps) |

---

# Algorithm Comparability Matrix

| Property | Static Direct | Slot Shifting | DTSS | BCSS | Affine Envelope |
|----------|---------------|---------------|------|------|-----------------|
| **Comparison Class** | Class A (Same Baseline) | Class A (Same Baseline) | Class A (Same Baseline) | Class A (Target) | Class B (Offline Co-Design Ref) |
| **Same $S_0$ Baseline** | Yes | Yes | Yes | Yes | No (Synthesizes own $S_0$) |
| **Runtime Relocation** | No | Yes (Leeway Shift) | Yes (TEW Shift) | Yes (Bounded $K$ Stealing) | No (Static $S_0$ Gaps) |
| **Sporadic Guarantee** | No (Best effort) | Yes (Offline Test) | Yes (RPCA Window) | Yes (Offline Gate + RTC) | Yes (Affine Envelope) |
| **Bounded Moved Jobs $K$** | N/A ($K=0$) | No ($K$ unbound) | No ($K$ unbound) | Yes ($K \le 2$) | N/A |
| **Native Preemption** | Non-preemptive | Preemptive (Paper) / Non-preempt (Adapt) | Preemptive (Paper) / Non-preempt (Adapt) | Non-preemptive | Non-preemptive |
