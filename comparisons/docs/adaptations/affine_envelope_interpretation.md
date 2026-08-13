# Affine Envelope Adaptation & Interpretation

## 1. Primary Model Architecture
- **Class B (Offline Co-Design Reference):** Does not consume the same initial TT schedule $S_0$ as Class A algorithms.
- **Synthesizes Own TT Schedule:** Uses Modified LLF with Burst-Limiting Constraint (BLC) to generate a static TT schedule offline.
- **Arrival Curve $\alpha(\Delta)$ & Affine Envelope $A(\Delta)$:** Approximates sporadic ET workload via $A(\Delta) = \sigma + \rho \cdot \Delta$.

## 2. Adaptation Details
- **Input Fingerprints:** Produces two fingerprints: `workload_input_fingerprint` (matching the common workload) and `generated_schedule_fingerprint` (its synthesized static schedule).
- **Runtime Execution:** Serves ET traffic in synthesized TT gaps.
