# Slot Shifting — Offline Acceptance Test Clarification (Alkoudsi, Isovic, Fohler 2025)

## Citation & DOI
Mohammad Ibrahim Alkoudsi, Damir Isovic, and Gerhard Fohler, "Revisiting Slot-Shifting's Offline Acceptance Test for Sporadic Tasks: A Technical Note," Leibniz Transactions on Embedded Systems (LITES), 10(1), 2025. DOI: `10.4230/LITES.10.1.4`

---

## 1. Core Technical Clarification
- **The Problem in 2009 Formulation:** The 2009 offline test contained potential ambiguity/pessimism regarding the analysis interval for checking sporadic stream reservations across capacity intervals.
- **Clarified Analysis Method:**
  1. The offline acceptance check evaluates sporadic demand over intervals $\Delta$ bounded by the scheduling horizon $H$.
  2. Sporadic demand is calculated using the bounded demand function over interval $\Delta$:
     $$\text{DBF}_{sporadic}(\Delta) = \sum_{i : \Delta \ge D_i} \left\lceil \frac{\Delta}{T_{min\_i}} \right\rceil \cdot C_i$$
  3. A candidate sporadic stream is accepted offline **if and only if** for all capacity intervals $I_k$ in $[0, H)$, the available leeway $l(I_k) \ge \text{DBF}_{sporadic}(H - I_k.start)$.

---

## 2. Impact on Implementation
- Avoids the ambiguous infinite sliding window reservation.
- Uses exact horizon-bounded leeway verification for offline sporadic admission.
- Ensures 100% sound offline admission without false-safe decisions.
