# Slot Shifting — Original Aperiodic Integration (Fohler 1995)

## Citation & DOI
Gerhard Fohler, "Joint Scheduling of Distributed Complex Periodic and Hard Aperiodic Tasks in Statically Scheduled Systems," IEEE Real-Time Systems Symposium (RTSS), 1995. DOI: `10.1109/REAL.1995.495205`

---

## 1. System & Task Model
- **Offline Periodic Tasks:** Pre-scheduled in a static time-triggered table over a hyperperiod $H$.
- **Online Aperiodic Requests:** Hard/firm aperiodic requests $J_A = (r_A, C_A, d_A)$ arriving dynamically online.
- **Resource Model:** Discrete time slots $[t, t+1)$. Single or distributed nodes.

---

## 2. Offline Calculations
1. **Capacity Intervals:** Bounded by distinct job deadlines in the static schedule $0 = t_0 < t_1 < \dots < t_m = H$.
2. **Spare Capacity $sc(I_k)$:** Number of free slots in interval $I_k$ not assigned to any periodic job:
   $$sc(I_k) = |I_k| - \sum_{j \in I_k} C_j$$
3. **Leeway / Total Available Slack $l(I_k)$:** Minimum cumulative spare capacity from $I_k$ to the horizon:
   $$l(I_k) = \min_{m \ge k} \sum_{p=k}^m sc(I_p)$$

---

## 3. Online State & Runtime Algorithm
- When an aperiodic job $J_A$ arrives at release $r_A$ with deadline $d_A$ and duration $C_A$:
  - Locate capacity intervals overlapping $[r_A, d_A)$.
  - Calculate available leeway $l_{avail}$ across those intervals.
  - If $l_{avail} \ge C_A$: **ACCEPT** job $J_A$ and deduct $C_A$ from spare capacity $sc$ and leeway $l$ along affected intervals.
  - If $l_{avail} < C_A$: **REJECT** job $J_A$.
- **Dispatch Policy:** Earliest Deadline First (EDF) among ready periodic and accepted aperiodic jobs using spare capacity.

---

## 4. Mapping to Neutral Simulator
- **Slot Granularity:** Discrete simulator slots $[s, s+1)$.
- **Preemption:** `PAPER_NATIVE` mode allows slot-level preemption. `COMMON_COMMUNICATION` mode requires $C$ contiguous slots.
