# Affine Envelope — TT/ET Integration (Finzi, Craciunas, Boyer RTAS 2024)

## Citation & DOI
Anaïs Finzi, Silviu S. Craciunas, and Marc Boyer, "Integrating Sporadic Events in Time-triggered Systems via Affine Envelope Approximations," IEEE RTAS 2024. DOI: `10.1109/RTAS61025.2024.00010` (also arXiv:2204.10264).

---

## 1. System & Offline Co-Design Model
- **Offline Co-Design Reference:** Unlike runtime-only baselines, this approach **synthesizes its own TT baseline schedule** offline to guarantee sporadic ET schedulability.
- **ET Arrival Curve $\alpha(\Delta)$:** Upper bound on sporadic ET arrivals over window $\Delta$:
  $$\alpha(\Delta) = \sum_{i} \left\lceil \frac{\Delta}{T_{min\_i}} \right\rceil \cdot C_i$$
- **Maximal Affine Envelope $A(\Delta)$:** Linear upper-bound approximation $A(\Delta) = \sigma + \rho \cdot \Delta$.
- **Burst-Limiting Constraint (BLC):** Bounds maximum TT execution density in any sliding window $\Delta$ so ET traffic receives guaranteed service.
- **Modified LLF Synthesis:** Modified Least Laxity First algorithm synthesizes the TT static schedule while respecting BLC constraints.

---

## 2. Classification
- **Class B (Offline Co-Design Reference):** Does not consume the same initial TT schedule as BCSS/Static/SlotShifting/DTSS because schedule synthesis is part of its method.
- Generates its own TT schedule and reports `generated_schedule_fingerprint`.
