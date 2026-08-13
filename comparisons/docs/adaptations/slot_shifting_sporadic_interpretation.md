# Slot Shifting Adaptation & Interpretation

## 1. Original Paper Models
- **Original Aperiodic Integration (Fohler 1995):** Uses capacity intervals $I_k$, spare capacity $sc(I_k)$, and leeway $l(I_k)$.
- **Mixed Task Extension (Isovic & Fohler 2009):** Incorporates sporadic tasks offline by reserving worst-case activation slots $C/T_{min}$ across capacity intervals. Reclaims unused sporadic reservations dynamically.
- **Clarification Note (Alkoudsi, Isovic, Fohler 2025):** Resolves offline acceptance test analysis interval by evaluating sporadic demand bound function $\text{DBF}_{sporadic}(\Delta)$ over horizon $H$.

## 2. Common Experiment Model Mapping
- **Slot Resolution:** Maps 1 execution time unit to 1 discrete simulator slot.
- **Preemption Modes:**
  - `PAPER_NATIVE`: Slot-level preemption as specified in original papers.
  - `COMMON_COMMUNICATION`: Non-preemptive allocation requiring $C$ consecutive slots (matching BCSS communication model).
- **Features Disabled for Fairness:** No BCSS $K$ bound, no BCSS ranking $(k, \Delta_{max}, \Delta_{total})$, no BCSS RTC guard. Slot Shifting uses its own published leeway and capacity interval mechanisms.
