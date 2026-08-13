# Literature Faithfulness Matrix

| Published Construct | Primary Paper | Implementation Location | Mapping Status | Adaptation / Details |
|---------------------|---------------|-------------------------|----------------|----------------------|
| **Static Slack Allocation** | Literature Baseline | `comparisons/static_direct/` | **Exact** | Direct free contiguous capacity search without schedule modification |
| **Capacity Intervals** | Fohler 1995 | `comparisons/slot_shifting/` | **Exact** | Bounded by distinct job deadlines $0 = t_0 < t_1 < \dots < t_m = H$ |
| **Spare Capacity $sc(I_k)$** | Fohler 1995 | `comparisons/slot_shifting/` | **Exact** | $sc(I_k) = \|I_k\| - \sum_{j \in I_k} C_j$ |
| **Leeway / Slack $l(I_k)$** | Fohler 1995 | `comparisons/slot_shifting/` | **Exact** | $l(I_k) = \min_{m \ge k} \sum_{p=k}^m sc(I_p)$ |
| **Aperiodic Integration** | Fohler 1995 | `comparisons/slot_shifting/` | **Exact** | Checked against available leeway $l_{avail} \ge C_A$ |
| **Sporadic Offline Test** | Isovic & Fohler 2009 / Alkoudsi 2025 | `comparisons/slot_shifting/` | **Exact (2025 Clarified)** | Evaluated over horizon-bounded DBF demand bound function |
| **Sporadic Reclamation** | Isovic & Fohler 2009 | `comparisons/slot_shifting/` | **Exact** | Unused sporadic capacity returned to spare capacity $sc$ |
| **Target Execution Windows (TEWs)** | Alkoudsi & Fohler 2024 | `comparisons/dtss/` | **Exact** | $TEW_j = [r'_j, d'_j]$ extracted from offline static schedule |
| **Remaining Processor Capacity Analysis (RPCA)** | Alkoudsi & Fohler 2024 | `comparisons/dtss/` | **Exact** | $RPC(t_1, t_2) = (t_2 - t_1) - \sum C_j \ge C_{dyn}$ |
| **Fixed Slot Granularity** | Alkoudsi & Fohler 2024 | `comparisons/dtss/` | **Exact** | `DTSS_STATIC_GRANULARITY` mode matching simulator slot quantum |
| **EDF TEW Dispatch** | Alkoudsi & Fohler 2024 | `comparisons/dtss/` | **Exact** | Dispatches ready jobs within active TEWs using EDF |
| **Arrival Curve $\alpha(\Delta)$** | Finzi et al. 2024 | `comparisons/affine_envelope/` | **Exact** | $\alpha(\Delta) = \sum \lceil \Delta / T_{min_i} \rceil \cdot C_i$ |
| **Maximal Affine Envelope $A(\Delta)$** | Finzi et al. 2024 | `comparisons/affine_envelope/` | **Exact** | $A(\Delta) = \sigma + \rho \cdot \Delta$ |
| **Burst-Limiting Constraint (BLC)** | Finzi et al. 2024 | `comparisons/affine_envelope/` | **Exact** | Constrains TT density during synthesis |
| **Modified LLF Synthesis** | Finzi et al. 2024 | `comparisons/affine_envelope/` | **Exact** | Synthesizes TT schedule respecting BLC |
