# Slot Shifting — Mixed / Sporadic Task Extension (Isovic & Fohler 2009)

## Citation & DOI
Damir Isovic and Gerhard Fohler, "Handling Mixed Sets of Tasks in Combined Offline and Online Scheduled Real-Time Systems," Real-Time Systems, 43(3), 2009. DOI: `10.1007/s11241-009-9088-3`

---

## 1. System Model
- **Task Classes:** Periodic (TT baseline), Sporadic ($T_{min}, C, D$), Aperiodic (one-shot).
- **Offline Sporadic Admission:** Sporadic streams are analyzed offline using maximum activation frequency ($1/T_{min}$) and worst-case execution time $C_{WCET}$.
- **Reserved Slots:** Offline analysis reserves capacity in intervals to guarantee admitted sporadic streams.

---

## 2. Runtime Sporadic Reclamation
- At runtime, exact actual arrivals $r_k$ and actual execution times $C_{actual} \le C_{WCET}$ are observed.
- If a sporadic stream does not arrive at its earliest possible time $r_k + T_{min}$, or if it executes for $C_{actual} < C_{WCET}$, the unneeded reserved capacity is **reclaimed** into spare capacity $sc$ and becomes available for lower-priority aperiodic jobs.

---

## 3. Mapping to BCSS Simulator
- **Offline Admission Gate:** Evaluates stream feasibility over schedule intervals.
- **Runtime Tracking:** Tracks $r_k$ per stream to enforce $r_{k+1} - r_k \ge T_{min}$ contract and release reclaimed capacity when arrivals are spaced wider than $T_{min}$.
