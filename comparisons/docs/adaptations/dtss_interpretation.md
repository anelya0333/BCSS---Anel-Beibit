# DTSS Adaptation & Interpretation

## 1. Primary Mode Implementation
- **Primary Mode:** `DTSS_STATIC_GRANULARITY` (Fixed-slot static granularity mode). Uses discrete slot resolution matching the simulator quantum.
- **Target Execution Windows (TEWs):** $TEW_j = [r'_j, d'_j]$ derived offline for each static TT job $j$.
- **Remaining Processor Capacity Analysis (RPCA):** $RPC(t_1, t_2) = (t_2 - t_1) - \sum_{j : TEW_j \subseteq [t_1, t_2]} C_j \ge C_{dyn}$.

## 2. Adaptation Details
- **Skipping Configuration:** Disabled (`skipping_disabled = true`) for primary fairness comparison so DTSS cannot drop protected periodic/sporadic traffic.
- **Preemption:**
  - `PAPER_NATIVE`: Slot-level EDF execution within TEWs.
  - `COMMON_COMMUNICATION`: $C$ consecutive slots within TEWs.
- **Features Disabled for Fairness:** No BCSS $K$ bound, no BCSS compensation search. DTSS operates strictly via TEW flexibility and RPCA checks.
