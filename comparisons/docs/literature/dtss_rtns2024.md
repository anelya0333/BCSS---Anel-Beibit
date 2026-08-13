# DTSS — Dynamic Task-Sets in Time-Triggered Real-Time Systems (Alkoudsi & Fohler RTNS 2024)

## Citation & DOI
Mohammad Ibrahim Alkoudsi and Gerhard Fohler, "Scheduling Dynamic Task-Sets in Time-Triggered Real-Time Systems," ACM RTNS 2024. DOI: `10.1145/3696355.3699699`

---

## 1. System & Core Abstractions
- **Target Execution Windows (TEWs):** Bounded timing intervals $TEW_j = [r'_j, d'_j]$ derived offline for each static TT job $j$.
  - $r'_j \ge r_j$ and $d'_j \le d_j$.
  - Executing job $j$ anywhere inside $TEW_j$ preserves all TT deadlines and dependencies.
- **Remaining Processor Capacity Analysis (RPCA):**
  - Measures remaining processor capacity over all sub-windows $[t_1, t_2] \subseteq [0, H)$:
    $$RPC(t_1, t_2) = (t_2 - t_1) - \sum_{j : TEW_j \subseteq [t_1, t_2]} C_j$$
  - Feasibility condition for dynamic job $J_{dyn} = (r_{dyn}, C_{dyn}, d_{dyn})$:
    $$\forall [t_1, t_2] \subseteq [r_{dyn}, d_{dyn}], \quad RPC(t_1, t_2) \ge C_{dyn}$$

---

## 2. Modes & Runtime Dispatch
- **Static-Granularity Mode (`DTSS_STATIC_GRANULARITY`):** Required primary mode. Fixed slot quantum matching the simulator discrete slot resolution.
- **Dynamic-Granularity Mode (`DTSS_DYNAMIC_GRANULARITY`):** Optional mode where slot sizes adapt dynamically.
- **Runtime Dispatch:** EDF among ready jobs whose current time $t \in TEW_j$.
- **Skipping Feature:** Disabled for primary fairness comparison (`skipping_disabled = true`).

---

## 3. Mapping to Neutral Simulator
- Static schedule $S_0$ is transformed into TEWs offline.
- RPCA evaluates dynamic job admission online.
- EDF dispatches ready jobs within TEWs.
