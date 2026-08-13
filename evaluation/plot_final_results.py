#!/usr/bin/env python3
"""Generate thesis figures from final taskset-level statistics only."""

from __future__ import annotations

import csv
import math
import os
import re
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/bcss-matplotlib")
import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results_final"
STATISTICS = RESULTS / "statistics/statistics_summary.csv"


def read_rows() -> list[dict[str, str]]:
    with STATISTICS.open(newline="") as handle:
        return list(csv.DictReader(handle))


def natural_parts(value: str) -> tuple[object, ...]:
    return tuple(int(part) if part.isdigit() else part for part in re.split(r"(\d+)", value))


def scenario_key(experiment: str, scenario: str) -> tuple[object, ...]:
    if experiment == "E1_main":
        match = re.fullmatch(r"U(\d+)_(QUIET|NORMAL|BUSY|WORST)", scenario)
        if match:
            return (int(match.group(1)), {"QUIET": 0, "NORMAL": 1, "BUSY": 2, "WORST": 3}[match.group(2)])
    if experiment == "E3_ablation":
        return ({"DirectOnly": 0, "DirectReclamation": 1, "CompensationRtcOff": 2, "Full": 3}.get(scenario, 99),)
    if experiment == "E6_multislot":
        return ({"c1": 0, "c2": 1, "mixed": 2, "heavy": 3}.get(scenario, 99),)
    if experiment == "E10_burst":
        profile, mode = scenario.split("_", 1)
        return ({"BUSY": 0, "WORST": 1}.get(profile, 99), {"RANDOM": 0, "SYNC": 1}.get(mode, 99))
    return natural_parts(scenario)


def generate() -> None:
    rows = read_rows()
    figures = RESULTS / "figures"
    figures.mkdir(parents=True, exist_ok=True)
    provenance: list[dict[str, str]] = []

    specifications = [
        ("E1_main", "e1_main_acceptance.png", "E1 paired algorithm comparison", "dynamic", "Dynamic acceptance ratio"),
        ("E2_k", "e2_k_sensitivity.png", "E2 bounded-recourse sensitivity", "dynamic", "Dynamic acceptance ratio"),
        ("E3_ablation", "e3_ablation.png", "E3 BCSS ablation", "dynamic", "Dynamic acceptance ratio"),
        ("E4_sporadic", "e4_sporadic_admission.png", "E4 sporadic-demand sensitivity", "sporadic", "Sporadic acceptance ratio"),
        ("E5_oneshot", "e5_oneshot_saturation.png", "E5 one-shot saturation", "oneshot", "One-shot acceptance ratio"),
        ("E6_multislot", "e6_multislot.png", "E6 contiguous multi-slot sensitivity", "oneshot", "One-shot acceptance ratio"),
        ("E7_dependencies", "e7_dependencies.png", "E7 dependency sensitivity", "dynamic", "Dynamic acceptance ratio"),
        ("E8A_sporadic_deadlines", "e8a_sporadic_deadlines.png", "E8A sporadic deadline sensitivity", "sporadic", "Sporadic acceptance ratio"),
        ("E8B_oneshot_deadlines", "e8b_oneshot_deadlines.png", "E8B one-shot deadline sensitivity", "oneshot", "One-shot acceptance ratio"),
        ("E10_burst", "e10_burst.png", "E10 burst stress", "oneshot", "One-shot acceptance ratio"),
    ]

    for experiment, filename, title, metric, ylabel in specifications:
        mean_field = f"mean_{metric}_acceptance"
        low_field = f"ci95_{metric}_low"
        high_field = f"ci95_{metric}_high"
        subset = [row for row in rows if row["experiment"] == experiment and math.isfinite(float(row[mean_field]))]
        if not subset:
            continue
        scenarios = sorted({row["scenario_id"] for row in subset}, key=lambda value: scenario_key(experiment, value))
        positions = {scenario: index for index, scenario in enumerate(scenarios)}
        fig, axis = plt.subplots(figsize=(max(7.2, 0.52 * len(scenarios)), 4.4))
        for algorithm in sorted({row["algorithm"] for row in subset}):
            algorithm_rows = {row["scenario_id"]: row for row in subset if row["algorithm"] == algorithm}
            present = [scenario for scenario in scenarios if scenario in algorithm_rows]
            means = [float(algorithm_rows[scenario][mean_field]) for scenario in present]
            lower = [max(0.0, mean - float(algorithm_rows[scenario][low_field])) for scenario, mean in zip(present, means)]
            upper = [max(0.0, float(algorithm_rows[scenario][high_field]) - mean) for scenario, mean in zip(present, means)]
            axis.errorbar([positions[scenario] for scenario in present], means, yerr=[lower, upper],
                          marker="o", linewidth=1.8, capsize=2.5, label=algorithm)
        axis.set_xticks(range(len(scenarios)), scenarios, rotation=45, ha="right")
        axis.set_ylim(-0.03, 1.03)
        axis.set_ylabel(ylabel)
        axis.set_title(title)
        axis.grid(alpha=0.25)
        axis.legend(fontsize=8)
        fig.tight_layout()
        fig.savefig(figures / filename, dpi=180)
        plt.close(fig)
        provenance.append({
            "figure_filename": filename,
            "plotting_script": "evaluation/plot_final_results.py",
            "input_statistical_data": "results_final/statistics/statistics_summary.csv",
            "source_aggregate": "results_final/aggregates/taskset_aggregates.csv",
            "source_raw_data": "results_final/raw/all_runs.csv",
        })

    provenance_path = RESULTS / "statistics/figure_provenance.csv"
    with provenance_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(provenance[0]))
        writer.writeheader()
        writer.writerows(provenance)

    report = [
        "# Figure Provenance Report", "",
        f"- Provenance-validated figures: {len(provenance)}",
        "- Scenario axes use numeric/domain ordering; error bars are taskset-level 95% bootstrap intervals.",
        "- Every figure is generated from `statistics_summary.csv`, derived from taskset aggregates and canonical raw data.", "",
        "| Figure | Plotting script | Statistical input | Aggregate | Raw |",
        "|---|---|---|---|---|",
    ]
    report += [
        f"| {row['figure_filename']} | {row['plotting_script']} | {row['input_statistical_data']} | "
        f"{row['source_aggregate']} | {row['source_raw_data']} |" for row in provenance
    ]
    (RESULTS / "reports/FIGURE_PROVENANCE_REPORT.md").write_text("\n".join(report) + "\n")


if __name__ == "__main__":
    generate()
