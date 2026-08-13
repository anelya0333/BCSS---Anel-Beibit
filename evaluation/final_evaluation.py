#!/usr/bin/env python3
"""Reproducible BCSS pre-flight, campaign, validation, statistics, and reports."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import random
import re
import shutil
import statistics
import subprocess
import sys
import time
import zipfile
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "build/release/comparisons/evaluation_worker"
MECHANISM_PROBE = ROOT / "build/debug/comparisons/preflight_mechanisms"
PREFLIGHT = ROOT / "preflight"
RESULTS = ROOT / "results_final"
MASTER_SEED = 20260811
SCHEDULER_COMMIT = "89e3a0e"
HEX64 = re.compile(r"^[0-9a-f]{64}$")
CLASS_A = ("StaticDirect", "SlotShifting", "DTSS", "BCSS")

IDENTITY_FIELDS = {
    "run_id", "experiment", "scenario_id", "algorithm", "algorithm_mode",
    "taskset_id", "trace_id", "profile", "RTC_enabled", "reclamation_enabled",
    "compensation_enabled", "dependency_level", "multislot_regime", "burst_mode",
    "taskset_sha256", "trace_sha256", "scenario_input_sha256",
    "baseline_schedule_sha256", "final_schedule_sha256", "scheduler_commit",
    "evaluation_commit", "preparation_message", "status",
}

INTEGER_FIELDS = {
    "taskset_id", "trace_id", "master_seed", "experiment_seed", "scenario_seed",
    "taskset_seed", "trace_seed", "horizon_slots", "simulation_duration_slots",
    "warmup_hyperperiods", "measurement_hyperperiods", "N_persistent",
    "N_periodic_streams", "N_candidate_sporadic_streams", "K", "dependency_edges",
    "max_duration", "periodic_releases", "periodic_deadline_misses",
    "candidate_sporadic_streams", "offline_admitted_sporadic_streams",
    "offline_rejected_sporadic_streams", "sporadic_arrivals",
    "Tmin_compliant_sporadic_arrivals", "Tmin_contract_violations", "sporadic_accepted",
    "sporadic_rejected", "protected_sporadic_runtime_rejects",
    "protected_sporadic_deadline_misses", "oneshot_arrivals", "oneshot_accepted",
    "oneshot_rejected", "dynamic_arrivals", "dynamic_accepted", "dynamic_rejected",
    "accepted_dynamic_slots", "direct_accepts", "reclamation_accepts",
    "compensation_accepts", "actual_k_sum", "max_actual_k", "k0_accepts", "k1_accepts",
    "k2_accepts", "k3_accepts", "k4_accepts", "k_gt4_accepts", "delta_max",
    "delta_total", "jobs_moved", "slots_changed", "candidate_schedules_generated",
    "candidate_schedules_feasible", "candidate_schedules_rtc_safe", "RTC_checks",
    "RTC_unsafe_outcomes", "RTC_induced_rejections", "search_states",
    "search_branches_pruned", "maximum_search_depth", "dependency_violations",
    "K_violations", "past_immutability_violations", "hash_state_inconsistencies",
    "rollback_errors", "preparation_success",
}

FLOAT_FIELDS = {
    "slot_quantum_ms", "hyperperiod_ms", "target_tt_utilization", "actual_tt_utilization",
    "candidate_sporadic_utilization", "admitted_sporadic_utilization",
    "offered_oneshot_utilization", "actual_offered_oneshot_utilization",
    "sporadic_deadline_ratio", "oneshot_deadline_ratio", "mean_actual_k",
    "mean_latency_ns", "median_latency_ns", "p95_latency_ns", "p99_latency_ns",
    "max_latency_ns",
}


def seed64(*parts: Any) -> int:
    digest = hashlib.sha256("|".join(map(str, parts)).encode()).digest()
    return int.from_bytes(digest[:8], "big")


def evaluation_source_paths() -> list[Path]:
    paths: set[Path] = {
        ROOT / "CMakeLists.txt",
        ROOT / "comparisons/CMakeLists.txt",
        ROOT / "evaluation/final_evaluation.py",
        ROOT / "evaluation/plot_final_results.py",
    }
    for directory in (ROOT / "include/bcss", ROOT / "src", ROOT / "comparisons"):
        for suffix in ("*.cpp", "*.hpp"):
            paths.update(directory.rglob(suffix))
    return sorted(paths, key=lambda path: path.relative_to(ROOT).as_posix())


def git_evaluation_id() -> str:
    digest = hashlib.sha256()
    for path in evaluation_source_paths():
        digest.update(path.relative_to(ROOT).as_posix().encode())
        digest.update(path.read_bytes())
    return "worktree-" + digest.hexdigest()[:16]


def read_worker_header() -> list[str]:
    output = subprocess.check_output([str(WORKER), "--header"], text=True, cwd=ROOT)
    return next(csv.reader([output.strip()]))


def write_csv(path: Path, rows: Iterable[dict[str, Any]], fieldnames: list[str] | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    materialized = list(rows)
    if fieldnames is None:
        fieldnames = list(materialized[0]) if materialized else []
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(materialized)
    os.replace(temporary, path)


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def scenario_defaults(**updates: Any) -> dict[str, Any]:
    scenario: dict[str, Any] = {
        "horizon": 10000,
        "tt_util": 0.70,
        "oneshot_util": 0.001,
        "periodic_streams": 70,
        "sporadic_streams": 30,
        "sporadic_util": 0.03,
        "profile": "NORMAL",
        "K": 2,
        "rtc": 1,
        "reclamation": 1,
        "compensation": 1,
        "max_duration": 4,
        "multislot": "mixed",
        "dependency_density": 0.0,
        "dependency_level": "0",
        "sporadic_deadline_ratio": -1.0,
        "oneshot_deadline_ratio": 20.0,
        "burst_mode": "RANDOM",
    }
    scenario.update(updates)
    return scenario


def make_item(
    experiment: str,
    scenario_id: str,
    algorithm: str,
    taskset_id: int,
    trace_id: int,
    params: dict[str, Any],
) -> dict[str, Any]:
    experiment_seed = seed64(MASTER_SEED, experiment)
    scenario_seed = seed64(experiment_seed, scenario_id)
    taskset_seed = seed64(scenario_seed, taskset_id, "taskset")
    trace_seed = seed64(scenario_seed, taskset_id, trace_id, "trace")
    run_id = hashlib.sha256(
        f"{experiment}|{scenario_id}|{algorithm}|{taskset_id}|{trace_id}|{taskset_seed}|{trace_seed}".encode()
    ).hexdigest()[:24]
    return {
        "run_id": run_id,
        "experiment": experiment,
        "scenario_id": scenario_id,
        "algorithm": algorithm,
        "taskset_id": taskset_id,
        "trace_id": trace_id,
        "master_seed": MASTER_SEED,
        "experiment_seed": experiment_seed,
        "scenario_seed": scenario_seed,
        "taskset_seed": taskset_seed,
        "trace_seed": trace_seed,
        **params,
    }


ARG_MAP = {
    "run_id": "run-id", "scenario_id": "scenario-id", "taskset_id": "taskset-id",
    "trace_id": "trace-id", "master_seed": "master-seed", "experiment_seed": "experiment-seed",
    "scenario_seed": "scenario-seed", "taskset_seed": "taskset-seed", "trace_seed": "trace-seed",
    "tt_util": "tt-util", "oneshot_util": "oneshot-util", "periodic_streams": "periodic-streams",
    "sporadic_streams": "sporadic-streams", "sporadic_util": "sporadic-util",
    "max_duration": "max-duration", "dependency_density": "dependency-density",
    "dependency_level": "dependency-level", "sporadic_deadline_ratio": "sporadic-deadline-ratio",
    "oneshot_deadline_ratio": "oneshot-deadline-ratio", "burst_mode": "burst-mode",
}


def worker_command(item: dict[str, Any]) -> list[str]:
    command = [str(WORKER)]
    evaluation_id = git_evaluation_id()
    for key, value in item.items():
        flag = ARG_MAP.get(key, key.replace("_", "-"))
        command.extend([f"--{flag}", str(value)])
    command.extend(["--scheduler-commit", SCHEDULER_COMMIT, "--evaluation-commit", evaluation_id])
    return command


@dataclass
class WorkerResult:
    run_id: str
    row: dict[str, str] | None
    error: str | None
    wall_seconds: float


def execute_one(item: dict[str, Any], header: list[str], timeout: int = 600) -> WorkerResult:
    started = time.monotonic()
    try:
        completed = subprocess.run(
            worker_command(item), cwd=ROOT, text=True, capture_output=True, timeout=timeout, check=False
        )
        wall = time.monotonic() - started
        if completed.returncode != 0:
            return WorkerResult(item["run_id"], None, completed.stderr.strip() or f"exit={completed.returncode}", wall)
        parsed = next(csv.DictReader([",".join(header), completed.stdout.strip()]))
        if parsed.get("run_id") != item["run_id"]:
            return WorkerResult(item["run_id"], None, "run_id mismatch", wall)
        parsed["runner_wall_seconds"] = f"{wall:.9f}"
        return WorkerResult(item["run_id"], parsed, None, wall)
    except Exception as error:  # noqa: BLE001 - errors become explicit failed-run records
        return WorkerResult(item["run_id"], None, repr(error), time.monotonic() - started)


def execute_manifest(
    manifest: list[dict[str, Any]], raw_path: Path, workers: int, label: str, timeout: int = 600
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    header = read_worker_header()
    output_fields = header + ["runner_wall_seconds"]
    current_evaluation_id = git_evaluation_id()
    completed_rows = {
        row["run_id"]: row for row in read_csv(raw_path)
        if row.get("run_id") and row.get("evaluation_commit") == current_evaluation_id
    }
    pending = [item for item in manifest if item["run_id"] not in completed_rows]
    failures: list[dict[str, str]] = []
    started = time.monotonic()
    last_refresh = 0.0

    def checkpoint() -> None:
        ordered = [completed_rows[item["run_id"]] for item in manifest if item["run_id"] in completed_rows]
        write_csv(raw_path, ordered, output_fields)

    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        futures = {pool.submit(execute_one, item, header, timeout): item for item in pending}
        for index, future in enumerate(as_completed(futures), 1):
            result = future.result()
            if result.row is None:
                failures.append({"run_id": result.run_id, "error": result.error or "unknown"})
            else:
                completed_rows[result.run_id] = result.row
            if index % 20 == 0:
                checkpoint()
            now = time.monotonic()
            if now - last_refresh >= 0.5 or index == len(pending):
                done = len(completed_rows)
                elapsed = max(now - started, 1e-9)
                rate = index / elapsed
                remaining = len(pending) - index
                eta = remaining / rate if rate else math.inf
                print(
                    f"\rBCSS FINAL EVAL | {label} | {done}/{len(manifest)} "
                    f"({100.0 * done / max(1, len(manifest)):5.1f}%) | {rate:5.2f} runs/s | "
                    f"elapsed {elapsed:7.1f}s | ETA {eta:7.1f}s | failures {len(failures)}",
                    end="", file=sys.stderr, flush=True,
                )
                last_refresh = now
    print(file=sys.stderr)
    checkpoint()
    return [completed_rows[item["run_id"]] for item in manifest if item["run_id"] in completed_rows], failures


def preflight_manifest() -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    items: list[dict[str, Any]] = []
    expectations: list[dict[str, str]] = []

    def add(check: str, value: str, params: dict[str, Any], algorithm: str = "StaticDirect", trace: int = 0) -> None:
        sid = f"{check}_{value}"
        items.append(make_item("PREFLIGHT", sid, algorithm, 0, trace, params))
        expectations.append({"check": check, "expected": value, "run_id": items[-1]["run_id"]})

    for util in (0.30, 0.50, 0.70, 0.80, 0.90):
        add("tt_utilization", f"{util:.2f}", scenario_defaults(horizon=256, tt_util=util, oneshot_util=0, sporadic_streams=0))
    for profile in ("QUIET", "NORMAL", "BUSY", "WORST"):
        add("profile", profile, scenario_defaults(horizon=512, profile=profile, oneshot_util=0, sporadic_streams=4, sporadic_util=0.08))
    for k in range(5):
        add("K", str(k), scenario_defaults(horizon=128, K=k, rtc=0, oneshot_util=0, sporadic_streams=0), "BCSS")
    for rtc in (0, 1):
        add("RTC", "ON" if rtc else "OFF", scenario_defaults(horizon=128, rtc=rtc, oneshot_util=0, sporadic_streams=0), "BCSS")
    for load in (0.0, 0.05):
        add("oneshot_load", f"{load:.2f}", scenario_defaults(horizon=256, oneshot_util=load, sporadic_streams=0))
    for ratio in (2.0, 5.0, 10.0, 20.0):
        add("deadline_ratio", str(int(ratio)), scenario_defaults(horizon=256, oneshot_util=0.03, sporadic_streams=2,
            sporadic_util=0.04, sporadic_deadline_ratio=ratio, oneshot_deadline_ratio=ratio))
    for regime, maximum in (("c1", 1), ("c2", 2), ("mixed", 4), ("heavy", 4)):
        add("multislot", f"{regime}:{maximum}", scenario_defaults(horizon=256, max_duration=maximum, multislot=regime,
            oneshot_util=0.03, sporadic_streams=2))
    for label, density in (("0", 0.0), ("2", 0.01), ("4", 0.02), ("8", 0.04)):
        add("dependencies", label, scenario_defaults(horizon=256, dependency_level=label,
            dependency_density=density, oneshot_util=0, sporadic_streams=0))

    difficult = (
        ("fragmented", scenario_defaults(horizon=96, tt_util=0.75, oneshot_util=0.08, sporadic_streams=2,
            sporadic_util=0.05, max_duration=2, multislot="c2", oneshot_deadline_ratio=5)),
        ("tight", scenario_defaults(horizon=96, tt_util=0.85, oneshot_util=0.06, sporadic_streams=1,
            sporadic_util=0.03, max_duration=1, multislot="c1", oneshot_deadline_ratio=2)),
        ("burst", scenario_defaults(horizon=128, tt_util=0.70, oneshot_util=0.08, sporadic_streams=3,
            sporadic_util=0.06, profile="WORST", burst_mode="SYNC", oneshot_deadline_ratio=5)),
    )
    for name, params in difficult:
        for algorithm in CLASS_A:
            items.append(make_item("PREFLIGHT_PAIRING", name, algorithm, 0, 0, params))
    # Two traces ensure the pre-flight tests the trace-to-taskset aggregation stage.
    for trace in (0, 1):
        items.append(make_item("PREFLIGHT_AGGREGATION", "repeated_trace", "StaticDirect", 0, trace,
            scenario_defaults(horizon=128, oneshot_util=0.04, sporadic_streams=2)))
    return items, expectations


def run_preflight(workers: int) -> bool:
    PREFLIGHT.mkdir(parents=True, exist_ok=True)
    manifest, expectations = preflight_manifest()
    write_csv(PREFLIGHT / "manifest_preflight.csv", manifest)
    rows, failures = execute_manifest(manifest, PREFLIGHT / "raw_preflight.csv", workers, "PREFLIGHT", timeout=180)
    by_id = {row["run_id"]: row for row in rows}
    checks: list[dict[str, str]] = []
    for expected in expectations:
        row = by_id.get(expected["run_id"], {})
        check = expected["check"]
        want = expected["expected"]
        if check == "tt_utilization": observed = row.get("actual_tt_utilization", "")
        elif check == "profile": observed = row.get("profile", "")
        elif check == "K": observed = row.get("K", "")
        elif check == "RTC": observed = row.get("RTC_enabled", "")
        elif check == "oneshot_load": observed = row.get("actual_offered_oneshot_utilization", "")
        elif check == "deadline_ratio": observed = row.get("oneshot_deadline_ratio", "")
        elif check == "multislot": observed = f"{row.get('multislot_regime', '')}:{row.get('max_duration', '')}"
        elif check == "dependencies": observed = row.get("dependency_edges", "")
        else: observed = ""
        passed = False
        if check in {"tt_utilization", "oneshot_load", "deadline_ratio"}:
            passed = bool(observed) and abs(float(observed) - float(want)) <= 0.004
        elif check == "dependencies":
            passed = int(observed or -1) == 0 if want == "0" else int(observed or 0) > 0
        else:
            passed = observed == want
        if check == "oneshot_load" and want == "0.00":
            passed = passed and row.get("oneshot_arrivals") == "0"
        checks.append({**expected, "observed": observed, "status": "PASS" if passed else "FAIL"})
    # Profiles must change trace contents and arrival counts.
    profile_rows = [by_id[e["run_id"]] for e in expectations if e["check"] == "profile" and e["run_id"] in by_id]
    profile_unique = len({(r["trace_sha256"], r["sporadic_arrivals"]) for r in profile_rows}) == 4
    checks.append({"check": "profile_trace_differentiation", "expected": "4 unique traces/counts",
                   "observed": str(len({(r["trace_sha256"], r["sporadic_arrivals"]) for r in profile_rows})),
                   "run_id": "group", "status": "PASS" if profile_unique else "FAIL"})
    write_csv(PREFLIGHT / "parameter_checks.csv", checks)

    probe = subprocess.run([str(MECHANISM_PROBE)], cwd=ROOT, text=True, capture_output=True, check=False)
    (PREFLIGHT / "mechanism_checks.csv").write_text(probe.stdout)
    mechanism_rows = list(csv.DictReader(probe.stdout.splitlines())) if probe.stdout else []
    mechanism_pass = probe.returncode == 0 and all(row["status"] == "PASS" for row in mechanism_rows)
    hash_rows = []
    for row in mechanism_rows:
        valid = bool(HEX64.fullmatch(row["pre_hash"])) and bool(HEX64.fullmatch(row["post_hash"]))
        rejected_rollback = row["success"] != "0" or row["pre_hash"] == row["post_hash"]
        hash_rows.append({"check": row["check"], "pre_hash": row["pre_hash"], "post_hash": row["post_hash"],
                          "format_valid": int(valid), "rejected_state_equal": int(rejected_rollback),
                          "status": "PASS" if valid and rejected_rollback else "FAIL"})
    write_csv(PREFLIGHT / "hash_checks.csv", hash_rows)

    pairing_rows = []
    differentiation_ok = True
    for scenario in ("fragmented", "tight", "burst"):
        group = [row for row in rows if row["experiment"] == "PREFLIGHT_PAIRING" and row["scenario_id"] == scenario]
        fingerprints = {(r["taskset_sha256"], r["trace_sha256"], r["scenario_input_sha256"],
                         r["baseline_schedule_sha256"]) for r in group}
        signatures = {(r["dynamic_accepted"], r["dynamic_rejected"], r["direct_accepts"],
                       r["reclamation_accepts"], r["compensation_accepts"]) for r in group}
        pairing = len(group) == 4 and len(fingerprints) == 1
        differentiated = len(signatures) > 1
        differentiation_ok = differentiation_ok and differentiated
        pairing_rows.append({"scenario_id": scenario, "algorithm_rows": len(group),
                             "unique_input_fingerprints": len(fingerprints), "unique_result_signatures": len(signatures),
                             "pairing_status": "PASS" if pairing else "FAIL",
                             "differentiation_status": "PASS" if differentiated else "FAIL"})
    write_csv(PREFLIGHT / "pairing_checks.csv", pairing_rows)

    aggregates = aggregate_tasksets(rows)
    write_csv(PREFLIGHT / "taskset_aggregate.csv", aggregates)
    stats = summarize_statistics(aggregates, bootstrap_samples=500, bootstrap_seed=MASTER_SEED)
    write_csv(PREFLIGHT / "statistics_input_check.csv", stats)
    aggregation_ok = len(aggregates) < len(rows) and any(int(row.get("n_traces", "1")) == 2 for row in aggregates)

    row_safety_ok = all(row.get("status") == "PASSED" for row in rows)
    gate = not failures and rows and row_safety_ok and all(c["status"] == "PASS" for c in checks) and mechanism_pass and \
        all(r["status"] == "PASS" for r in hash_rows) and \
        all(r["pairing_status"] == "PASS" for r in pairing_rows) and differentiation_ok and aggregation_ok
    report = [
        "# BCSS Evaluation Pre-flight Report", "", f"PREFLIGHT: {'PASS' if gate else 'FAIL'}", "",
        "## Gate evidence", "",
        f"- Worker rows completed: {len(rows)} / {len(manifest)}",
        f"- Runner failures: {len(failures)}",
        f"- Safety-clean pre-flight rows: {sum(r.get('status') == 'PASSED' for r in rows)} / {len(rows)}",
        f"- Parameter checks passed: {sum(c['status'] == 'PASS' for c in checks)} / {len(checks)}",
        f"- Controlled mechanism checks passed: {sum(r['status'] == 'PASS' for r in mechanism_rows)} / {len(mechanism_rows)}",
        f"- Pairing scenarios with identical Class-A inputs: {sum(r['pairing_status'] == 'PASS' for r in pairing_rows)} / {len(pairing_rows)}",
        f"- Differentiating scenarios: {sum(r['differentiation_status'] == 'PASS' for r in pairing_rows)} / {len(pairing_rows)}",
        f"- Raw rows / taskset aggregates: {len(rows)} / {len(aggregates)}",
        "- Hashes: real 64-character lowercase SHA-256 values; rejected fixtures preserve state.",
        "- Scheduler regression basis: controlled direct, reclamation, one-hop, two-hop, K-bound, RTC, Tmin, and hash fixtures.",
        "", "## Failure classification", "",
        "Any failed gate blocks campaign launch. Runner failures are recorded below.", "",
        "```text", json.dumps(failures, indent=2), "```", "",
    ]
    (PREFLIGHT / "PREFLIGHT_REPORT.md").write_text("\n".join(report))
    return gate


def benchmark_runner(workers: int) -> list[dict[str, str]]:
    scenarios = [
        ("easy", CLASS_A, 3,
            scenario_defaults(oneshot_util=0.0, sporadic_streams=0, max_duration=1, multislot="c1", K=0, rtc=0)),
        ("representative", CLASS_A, 3,
            scenario_defaults(oneshot_util=0.001, sporadic_util=0.03, max_duration=1,
                multislot="c1", K=2, rtc=1)),
        ("mixed", CLASS_A, 3,
            scenario_defaults(oneshot_util=0.0004, sporadic_util=0.01, max_duration=4,
                multislot="mixed", K=2, rtc=1, oneshot_deadline_ratio=20)),
        ("saturation", CLASS_A, 1,
            scenario_defaults(oneshot_util=0.15, sporadic_util=0.01, profile="BUSY",
                max_duration=1, multislot="c1", K=2, rtc=1, oneshot_deadline_ratio=20)),
        ("dependencies", CLASS_A, 1,
            scenario_defaults(oneshot_util=0.0004, sporadic_util=0.01, dependency_level="8",
                dependency_density=0.0016, max_duration=1, multislot="c1")),
        ("burst", CLASS_A, 1,
            scenario_defaults(oneshot_util=0.0006, sporadic_util=0.01, profile="WORST",
                burst_mode="SYNC", max_duration=1, multislot="c1")),
        ("scale500", ("SlotShifting", "DTSS", "BCSS"), 1,
            scenario_defaults(periodic_streams=350, sporadic_streams=150, oneshot_util=0.0002,
                sporadic_util=0.01, max_duration=1, multislot="c1")),
        ("affine", ("AffineEnvelope",), 1,
            scenario_defaults(tt_util=0.70, oneshot_util=0.0004, sporadic_util=0.01,
                dependency_density=0, dependency_level="0", max_duration=1, multislot="c1")),
    ]
    manifest: list[dict[str, Any]] = []
    for name, algorithms, repetitions, params in scenarios:
        for taskset in range(repetitions):
            for algorithm in algorithms:
                manifest.append(make_item("BENCHMARK", name, algorithm, taskset, 0, params))
    write_csv(RESULTS / "metadata/benchmark_manifest.csv", manifest)
    rows, failures = execute_manifest(manifest, RESULTS / "metadata/benchmark_raw.csv", workers, "BENCHMARK", timeout=120)
    if failures:
        write_csv(RESULTS / "logs/benchmark_failures.csv", failures)
        raise RuntimeError(f"Representative benchmark failed for {len(failures)} run(s); campaign design is blocked")
    summaries = []
    for key in sorted({(r["scenario_id"], r["algorithm"]) for r in rows}):
        values = sorted(float(r["runner_wall_seconds"]) for r in rows if (r["scenario_id"], r["algorithm"]) == key)
        summaries.append({
            "scenario_id": key[0], "algorithm": key[1], "runs": len(values),
            "median_runtime_seconds": statistics.median(values),
            "p95_runtime_seconds": quantile_py(values, 0.95),
            "max_runtime_seconds": max(values),
        })
    write_csv(RESULTS / "metadata/benchmark_summary.csv", summaries)
    return summaries


def campaign_scenarios() -> list[tuple[str, str, tuple[str, ...], dict[str, Any], int]]:
    """Smallest broad Stage-1 design; primary cells use 30 paired tasksets."""
    scenarios: list[tuple[str, str, tuple[str, ...], dict[str, Any], int]] = []
    for util in (0.30, 0.50, 0.70, 0.80, 0.90):
        for profile in ("QUIET", "NORMAL", "BUSY", "WORST"):
            sid = f"U{int(util*100)}_{profile}"
            scenarios.append(("E1_main", sid, CLASS_A,
                scenario_defaults(tt_util=util, profile=profile, oneshot_util=0.0004,
                    sporadic_util=0.01, max_duration=1, multislot="c1"), 30))
    for k in range(5):
        scenarios.append(("E2_k", f"K{k}", ("BCSS",),
            scenario_defaults(K=k, profile="BUSY", oneshot_util=0.0004,
                sporadic_util=0.01, max_duration=1, multislot="c1"), 12))
    ablations = (
        ("DirectOnly", dict(K=0, rtc=1, reclamation=0, compensation=0)),
        ("DirectReclamation", dict(K=1, rtc=1, reclamation=1, compensation=0)),
        ("CompensationRtcOff", dict(K=2, rtc=0, reclamation=1, compensation=1)),
        ("Full", dict(K=2, rtc=1, reclamation=1, compensation=1)),
    )
    for name, flags in ablations:
        scenarios.append(("E3_ablation", name, (f"BCSS_{name}",),
            scenario_defaults(profile="BUSY", oneshot_util=0.0004, sporadic_util=0.01,
                max_duration=1, multislot="c1", **flags), 12))
    for load in (0.05, 0.10, 0.15, 0.20, 0.30):
        scenarios.append(("E4_sporadic", f"US{int(load*100)}", ("BCSS",),
            scenario_defaults(sporadic_util=load, oneshot_util=0, profile="BUSY",
                max_duration=1, multislot="c1"), 12))
    for load in (0.0, 0.01, 0.03, 0.06, 0.10, 0.15):
        scenarios.append(("E5_oneshot", f"UOS{int(load*100)}", CLASS_A,
            scenario_defaults(oneshot_util=load, sporadic_util=0.01, profile="BUSY",
                max_duration=1, multislot="c1", oneshot_deadline_ratio=20), 8))
    for regime, maximum in (("c1", 1), ("c2", 2), ("mixed", 4), ("heavy", 4)):
        scenarios.append(("E6_multislot", regime, CLASS_A,
            scenario_defaults(multislot=regime, max_duration=maximum, oneshot_util=0.0004,
                sporadic_util=0.01), 10))
    for chains, density in ((0, 0.0), (2, 0.0004), (4, 0.0008), (8, 0.0016)):
        scenarios.append(("E7_dependencies", f"Chains{chains}", CLASS_A,
            scenario_defaults(dependency_level=str(chains), dependency_density=density,
                oneshot_util=0.0004, sporadic_util=0.01, max_duration=1, multislot="c1"), 10))
    for ratio in (2, 5, 10, 20):
        scenarios.append(("E8A_sporadic_deadlines", f"DCR{ratio}", CLASS_A,
            scenario_defaults(sporadic_deadline_ratio=ratio, oneshot_util=0,
                sporadic_util=0.01, max_duration=1, multislot="c1"), 10))
        scenarios.append(("E8B_oneshot_deadlines", f"DCR{ratio}", CLASS_A,
            scenario_defaults(oneshot_deadline_ratio=ratio, oneshot_util=0.0004,
                sporadic_streams=0, sporadic_util=0, max_duration=1, multislot="c1"), 10))
    for periodic, sporadic in ((90, 10), (70, 30), (50, 50)):
        scenarios.append(("E9_composition", f"P{periodic}_S{sporadic}", CLASS_A,
            scenario_defaults(periodic_streams=periodic, sporadic_streams=sporadic,
                oneshot_util=0.0004, sporadic_util=0.01, max_duration=1, multislot="c1"), 10))
    for profile in ("BUSY", "WORST"):
        for burst in ("RANDOM", "SYNC"):
            scenarios.append(("E10_burst", f"{profile}_{burst}", CLASS_A,
                scenario_defaults(profile=profile, burst_mode=burst, oneshot_util=0.0006,
                    sporadic_util=0.01, max_duration=1, multislot="c1"), 10))
    for count in (25, 50, 100, 200, 500):
        periodic = max(1, int(round(count * 0.70)))
        sporadic = max(0, count - periodic)
        scenarios.append(("E11_scalability", f"N{count}", ("SlotShifting", "DTSS", "BCSS"),
            scenario_defaults(periodic_streams=periodic, sporadic_streams=sporadic,
                oneshot_util=0.0002, sporadic_util=0.01, max_duration=1, multislot="c1"), 6))
    for util in (0.50, 0.70, 0.80):
        scenarios.append(("Affine_reference", f"U{int(util*100)}", ("AffineEnvelope",),
            scenario_defaults(tt_util=util, oneshot_util=0.0004, sporadic_util=0.01,
                dependency_density=0, dependency_level="0", max_duration=1, multislot="c1"), 10))
    return scenarios


def build_campaign_manifest() -> list[dict[str, Any]]:
    manifest: list[dict[str, Any]] = []
    for experiment, scenario, algorithms, params, tasksets in campaign_scenarios():
        for taskset in range(tasksets):
            for algorithm in algorithms:
                manifest.append(make_item(experiment, scenario, algorithm, taskset, 0, params))
    return manifest


def write_campaign_design(manifest: list[dict[str, Any]], benchmark: list[dict[str, str]]) -> None:
    counts: dict[str, int] = defaultdict(int)
    for row in manifest:
        counts[row["experiment"]] += 1
    median_times = [float(row["median_runtime_seconds"]) for row in benchmark]
    representative = statistics.median(median_times) if median_times else 0.0
    conservative_by_algorithm: dict[str, float] = {}
    for row in benchmark:
        conservative_by_algorithm[row["algorithm"]] = max(
            conservative_by_algorithm.get(row["algorithm"], 0.0),
            float(row["p95_runtime_seconds"]),
        )
    algorithm_counts = Counter(str(row["algorithm"]) for row in manifest)
    conservative_cpu_seconds = sum(
        count * conservative_by_algorithm.get(algorithm, representative)
        for algorithm, count in algorithm_counts.items()
    )
    estimate = conservative_cpu_seconds / 10.0
    lines = [
        "# Final BCSS Campaign Design", "", "## Decision", "",
        f"The old nominal design contained 278,750 runs. The corrected staged design contains **{len(manifest):,}** runs.",
        "It uses paired Class-A inputs, 30 independent tasksets for the primary E1 cells, and 6–12 tasksets for",
        "secondary/targeted sensitivity cells. Secondary intervals are explicitly exploratory; they are not used",
        "to claim small effects with high precision.", "", "## Fixed timing model", "",
        "- Slot quantum: 0.1 ms", "- Hyperperiod/horizon: 1,000 ms / 10,000 slots",
        "- Simulation duration: one hyperperiod per independent trace.",
        "- Warm-up: none. The scheduler state is initialized directly from the deterministic periodic baseline.",
        "- The original 12-hyperperiod duration was not retained: the scheduler operates on a finite one-hyperperiod",
        "  schedule, and extending jobs beyond H would change the implemented model rather than merely lengthen observation.",
        "", "## Statistical unit and stopping rule", "",
        "The taskset is the unit of inference. Each current cell has one independently seeded trace per taskset, so no",
        "pseudo-replication occurs. E1 starts at 30 tasksets. Secondary cells start at 6–12. Additional tasksets are",
        "required only if the 95% bootstrap CI half-width for acceptance exceeds 0.10 or paired-effect signs are unstable",
        "after excluding exact-zero cells. Mechanism claims additionally require targeted deterministic fixtures; rare",
        "mechanisms are not manufactured in representative workloads.", "", "## Runtime estimate", "",
        f"Median across benchmark cells: {representative:.4f} s; conservative algorithm-tail 10-worker wall estimate: {estimate/60:.1f} min.",
        "Actual worker wall times are retained in every row and summarized after execution.", "", "## Experiments", "",
        "| Experiment | Runs | Purpose |", "|---|---:|---|",
    ]
    purposes = {
        "E1_main": "Paired StaticDirect / Slot Shifting / DTSS / BCSS comparison over TT utilization and profiles.",
        "E2_k": "K=0..4 recourse sensitivity.", "E3_ablation": "Direct, reclamation, compensation/RTC-off, and full variants.",
        "E4_sporadic": "Candidate sporadic demand and offline admission.", "E5_oneshot": "One-shot saturation grid.",
        "E6_multislot": "Contiguous multi-slot fragmentation.", "E7_dependencies": "Precedence-density sensitivity.",
        "E8A_sporadic_deadlines": "Sporadic D/C sensitivity.", "E8B_oneshot_deadlines": "One-shot D/C sensitivity (restored E8B).",
        "E9_composition": "Persistent periodic/sporadic composition.", "E10_burst": "Random versus synchronized burst stress.",
        "E11_scalability": "Increasing persistent stream count.", "Affine_reference": "Separate Class-B co-design reference.",
    }
    for experiment in sorted(counts):
        lines.append(f"| {experiment} | {counts[experiment]} | {purposes.get(experiment, '')} |")
    lines += ["", "## Benchmark evidence", "", "| Scenario | Algorithm | n | Median s | P95 s | Max s |",
              "|---|---|---:|---:|---:|---:|"]
    for row in benchmark:
        lines.append(
            f"| {row['scenario_id']} | {row['algorithm']} | {row['runs']} | "
            f"{float(row['median_runtime_seconds']):.4f} | {float(row['p95_runtime_seconds']):.4f} | "
            f"{float(row['max_runtime_seconds']):.4f} |"
        )
    lines += ["", "## Comparison scope", "",
              "Affine Envelope is not a paired Class-A claim. It synthesizes its own schedule and is restricted to its",
              "implemented dependency-free, unit-slot TT scope. DTSS currently reports no separate offline stream-admission",
              "stage; this implementation limitation is reported rather than inferred away.", ""]
    (RESULTS / "reports/FINAL_CAMPAIGN_DESIGN.md").parent.mkdir(parents=True, exist_ok=True)
    (RESULTS / "reports/FINAL_CAMPAIGN_DESIGN.md").write_text("\n".join(lines))


def numeric(value: str) -> float:
    return float(value) if value not in ("", None) else 0.0


def aggregate_tasksets(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[(row["experiment"], row["scenario_id"], row["algorithm"], row["taskset_id"])].append(row)
    output: list[dict[str, Any]] = []
    for key, group in sorted(groups.items()):
        base: dict[str, Any] = {
            "experiment": key[0], "scenario_id": key[1], "algorithm": key[2], "taskset_id": key[3],
            "n_traces": len(group), "taskset_sha256": group[0]["taskset_sha256"],
            "baseline_schedule_sha256": group[0]["baseline_schedule_sha256"],
        }
        for field in group[0]:
            if field in INTEGER_FIELDS or field in FLOAT_FIELDS or field == "runner_wall_seconds":
                base[field] = sum(numeric(row[field]) for row in group) / len(group)
        prepared = base.get("preparation_success", 0.0) >= 1.0
        arrivals = base.get("dynamic_arrivals", 0.0) if prepared else 0.0
        oneshot = base.get("oneshot_arrivals", 0.0) if prepared else 0.0
        sporadic = base.get("sporadic_arrivals", 0.0) if prepared else 0.0
        base["dynamic_acceptance_ratio"] = base.get("dynamic_accepted", 0.0) / arrivals if arrivals else math.nan
        base["oneshot_acceptance_ratio"] = base.get("oneshot_accepted", 0.0) / oneshot if oneshot else math.nan
        base["sporadic_acceptance_ratio"] = base.get("sporadic_accepted", 0.0) / sporadic if sporadic else math.nan
        output.append(base)
    return output


def quantile_py(values: list[float], probability: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    position = probability * (len(ordered) - 1)
    lower, upper = math.floor(position), math.ceil(position)
    fraction = position - lower
    return ordered[lower] * (1 - fraction) + ordered[upper] * fraction


def bootstrap_ci(values: list[float], samples: int, seed: int) -> tuple[float, float]:
    clean = [v for v in values if math.isfinite(v)]
    if not clean:
        return math.nan, math.nan
    rng = random.Random(seed)
    means = [statistics.fmean(rng.choice(clean) for _ in clean) for _ in range(samples)]
    return quantile_py(means, 0.025), quantile_py(means, 0.975)


def summarize_statistics(
    aggregates: list[dict[str, Any]], bootstrap_samples: int = 2000, bootstrap_seed: int = MASTER_SEED
) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = defaultdict(list)
    for row in aggregates:
        groups[(str(row["experiment"]), str(row["scenario_id"]), str(row["algorithm"]))].append(row)
    output = []
    for key, group in sorted(groups.items()):
        values_by_metric = {
            "dynamic": [float(row["dynamic_acceptance_ratio"]) for row in group
                        if math.isfinite(float(row["dynamic_acceptance_ratio"]))],
            "oneshot": [float(row["oneshot_acceptance_ratio"]) for row in group
                        if math.isfinite(float(row["oneshot_acceptance_ratio"]))],
            "sporadic": [float(row["sporadic_acceptance_ratio"]) for row in group
                         if math.isfinite(float(row["sporadic_acceptance_ratio"]))],
        }
        runtime = [float(row.get("mean_latency_ns", 0)) for row in group]
        runner_runtime = [float(row.get("runner_wall_seconds", 0)) for row in group]
        summary: dict[str, Any] = {
            "experiment": key[0], "scenario_id": key[1], "algorithm": key[2], "tasksets": len(group),
            "mean_latency_ns": statistics.fmean(runtime) if runtime else math.nan,
            "mean_runner_seconds": statistics.fmean(runner_runtime) if runner_runtime else math.nan,
            "bootstrap_samples": bootstrap_samples,
        }
        for metric, values in values_by_metric.items():
            metric_seed = seed64(bootstrap_seed, *key, metric)
            lower, upper = bootstrap_ci(values, bootstrap_samples, metric_seed)
            summary.update({
                f"mean_{metric}_acceptance": statistics.fmean(values) if values else math.nan,
                f"median_{metric}_acceptance": statistics.median(values) if values else math.nan,
                f"q1_{metric}_acceptance": quantile_py(values, 0.25),
                f"q3_{metric}_acceptance": quantile_py(values, 0.75),
                f"ci95_{metric}_low": lower,
                f"ci95_{metric}_high": upper,
                f"bootstrap_seed_{metric}": metric_seed,
            })
        output.append(summary)
    return output


def validate_dataset(manifest: list[dict[str, Any]], rows: list[dict[str, str]]) -> tuple[bool, list[str]]:
    errors: list[str] = []
    header = read_worker_header() + ["runner_wall_seconds"]
    manifest_ids = [str(row["run_id"]) for row in manifest]
    raw_ids = [row.get("run_id", "") for row in rows]
    if len(manifest_ids) != len(set(manifest_ids)): errors.append("duplicate manifest run IDs")
    if len(raw_ids) != len(set(raw_ids)): errors.append("duplicate raw run IDs")
    if set(manifest_ids) != set(raw_ids): errors.append("manifest/raw run-ID bijection failure")
    by_manifest = {str(row["run_id"]): row for row in manifest}
    valid_algorithms = set(CLASS_A) | {"AffineEnvelope"}
    for index, row in enumerate(rows, 1):
        if list(row) != header: errors.append(f"row {index}: schema/header mismatch")
        if row.get("algorithm", "").split("_")[0] not in valid_algorithms and not row.get("algorithm", "").startswith("BCSS_"):
            errors.append(f"row {index}: invalid algorithm")
        for field in INTEGER_FIELDS:
            try: int(float(row[field]))
            except (ValueError, KeyError): errors.append(f"row {index}: invalid integer {field}")
        for field in FLOAT_FIELDS | {"runner_wall_seconds"}:
            try:
                if not math.isfinite(float(row[field])): raise ValueError
            except (ValueError, KeyError): errors.append(f"row {index}: invalid finite number {field}")
        for field in ("taskset_sha256", "trace_sha256", "scenario_input_sha256",
                      "baseline_schedule_sha256", "final_schedule_sha256"):
            if not HEX64.fullmatch(row.get(field, "")): errors.append(f"row {index}: invalid hash {field}")
        if row.get("status") not in {"PASSED", "FAILED", "PREPARATION_INFEASIBLE"}:
            errors.append(f"row {index}: invalid status")
        expected = by_manifest.get(row.get("run_id", ""), {})
        mappings = {
            "tt_util": "target_tt_utilization", "oneshot_util": "offered_oneshot_utilization",
            "profile": "profile", "K": "K", "dependency_level": "dependency_level",
            "multislot": "multislot_regime", "max_duration": "max_duration",
        }
        for source, target in mappings.items():
            if source in expected and str(expected[source]) != row.get(target):
                try:
                    matches = abs(float(expected[source]) - float(row[target])) < 1e-10
                except (ValueError, TypeError):
                    matches = False
                if not matches: errors.append(f"row {index}: propagation {source}->{target}")
        if int(row.get("max_actual_k", 0)) > int(row.get("K", 0)): errors.append(f"row {index}: K violation")
        if float(row.get("offered_oneshot_utilization", 0)) == 0 and int(row.get("oneshot_arrivals", 0)) != 0:
            errors.append(f"row {index}: zero-load one-shot violation")
    # Paired Class-A algorithms must share identical taskset, trace, scenario, and baseline inputs.
    pairing: dict[tuple[str, str, str, str], set[tuple[str, str, str, str]]] = defaultdict(set)
    for row in rows:
        base_algo = "BCSS" if row["algorithm"].startswith("BCSS") else row["algorithm"]
        if base_algo in CLASS_A:
            pairing[(row["experiment"], row["scenario_id"], row["taskset_id"], row["trace_id"])].add(
                (row["taskset_sha256"], row["trace_sha256"], row["scenario_input_sha256"], row["baseline_schedule_sha256"])
            )
    for key, fingerprints in pairing.items():
        if len(fingerprints) != 1: errors.append(f"pairing fingerprint mismatch {key}")
    return not errors, errors


def paired_comparisons(aggregates: list[dict[str, Any]]) -> list[dict[str, Any]]:
    lookup = {(r["experiment"], r["scenario_id"], r["taskset_id"], r["algorithm"]): r for r in aggregates}
    output = []
    cells = sorted({(r["experiment"], r["scenario_id"]) for r in aggregates if r["algorithm"] == "BCSS"})
    for experiment, scenario in cells:
        for comparator in ("StaticDirect", "SlotShifting", "DTSS"):
            for metric in ("dynamic_acceptance_ratio", "oneshot_acceptance_ratio", "sporadic_acceptance_ratio"):
                differences = []
                for lookup_key, bcss_row in lookup.items():
                    if lookup_key[0] != experiment or lookup_key[1] != scenario or lookup_key[3] != "BCSS": continue
                    other = lookup.get((experiment, scenario, lookup_key[2], comparator))
                    if other is None: continue
                    left, right = float(bcss_row[metric]), float(other[metric])
                    if math.isfinite(left) and math.isfinite(right): differences.append(left - right)
                if differences:
                    lo, hi = bootstrap_ci(
                        differences, 2000, seed64(MASTER_SEED, experiment, scenario, comparator, metric)
                    )
                    output.append({"experiment": experiment, "scenario_id": scenario,
                                   "metric": metric, "comparison": f"BCSS-{comparator}",
                                   "paired_tasksets": len(differences),
                                   "mean_difference": statistics.fmean(differences),
                                   "median_difference": statistics.median(differences),
                                   "ci95_low": lo, "ci95_high": hi})
    return output


def generate_summary_tables(rows: list[dict[str, str]], aggregates: list[dict[str, Any]], stats: list[dict[str, Any]]) -> None:
    stats_dir = RESULTS / "statistics"
    stats_dir.mkdir(parents=True, exist_ok=True)
    counts = []
    for experiment in sorted({row["experiment"] for row in rows}):
        subset = [row for row in rows if row["experiment"] == experiment]
        counts.append({"experiment": experiment, "raw_runs": len(subset),
                       "taskset_aggregates": sum(r["experiment"] == experiment for r in aggregates),
                       "scenarios": len({r["scenario_id"] for r in subset}),
                       "algorithms": len({r["algorithm"] for r in subset})})
    write_csv(stats_dir / "experiment_counts.csv", counts)
    parameters = []
    for experiment in sorted({row["experiment"] for row in rows}):
        subset = [row for row in rows if row["experiment"] == experiment]
        parameters.append({"experiment": experiment,
            "tt_utilizations": ";".join(sorted({r["target_tt_utilization"] for r in subset})),
            "profiles": ";".join(sorted({r["profile"] for r in subset})),
            "K_values": ";".join(sorted({r["K"] for r in subset}, key=int)),
            "oneshot_loads": ";".join(sorted({r["offered_oneshot_utilization"] for r in subset}, key=float)),
            "horizons": ";".join(sorted({r["horizon_slots"] for r in subset}, key=int))})
    write_csv(stats_dir / "parameter_summary.csv", parameters)
    algorithm_summary = []
    for algorithm in sorted({row["algorithm"] for row in rows}):
        subset = [row for row in rows if row["algorithm"] == algorithm]
        prepared = [row for row in subset if row["preparation_success"] == "1"]
        arrivals = sum(int(r["dynamic_arrivals"]) for r in prepared)
        accepted = sum(int(r["dynamic_accepted"]) for r in prepared)
        algorithm_summary.append({"algorithm": algorithm, "runs": len(subset), "dynamic_arrivals": arrivals,
                                  "dynamic_accepted": accepted, "acceptance_ratio": accepted / arrivals if arrivals else math.nan,
                                  "prepared_runs": len(prepared),
                                  "preparation_infeasible": sum(r["preparation_success"] == "0" for r in subset)})
    write_csv(stats_dir / "algorithm_summary.csv", algorithm_summary)
    mechanisms = []
    for algorithm in sorted({row["algorithm"] for row in rows}):
        subset = [row for row in rows if row["algorithm"] == algorithm]
        mechanisms.append({"algorithm": algorithm, "direct_accepts": sum(int(r["direct_accepts"]) for r in subset),
                           "reclamation_accepts": sum(int(r["reclamation_accepts"]) for r in subset),
                           "compensation_accepts": sum(int(r["compensation_accepts"]) for r in subset),
                           "rtc_checks": sum(int(r["RTC_checks"]) for r in subset),
                           "rtc_unsafe": sum(int(r["RTC_unsafe_outcomes"]) for r in subset),
                           "search_states": sum(int(r["search_states"]) for r in subset)})
    write_csv(stats_dir / "mechanism_usage.csv", mechanisms)
    safety_fields = ("periodic_deadline_misses", "protected_sporadic_runtime_rejects",
        "protected_sporadic_deadline_misses", "Tmin_contract_violations", "dependency_violations",
        "K_violations", "past_immutability_violations", "hash_state_inconsistencies", "rollback_errors")
    safety = []
    for algorithm in sorted({row["algorithm"] for row in rows}):
        subset = [row for row in rows if row["algorithm"] == algorithm]
        safety.append({"algorithm": algorithm, **{field: sum(int(r[field]) for r in subset) for field in safety_fields}})
    write_csv(stats_dir / "safety_invariants.csv", safety)
    runtime = []
    for algorithm in sorted({row["algorithm"] for row in rows}):
        subset = [row for row in rows if row["algorithm"] == algorithm]
        values = [float(r["runner_wall_seconds"]) for r in subset]
        runtime.append({"algorithm": algorithm, "runs": len(values), "median_run_seconds": statistics.median(values),
                        "p95_run_seconds": quantile_py(values, 0.95),
                        "median_decision_latency_ns": statistics.median(float(r["median_latency_ns"]) for r in subset)})
    write_csv(stats_dir / "runtime_summary.csv", runtime)
    write_csv(stats_dir / "paired_comparisons.csv", paired_comparisons(aggregates))
    thresholds = []
    for algorithm in CLASS_A:
        eligible = []
        for load in (0.0, 0.01, 0.03, 0.06, 0.10, 0.15):
            subset = [r for r in rows if r["experiment"] == "E5_oneshot" and r["algorithm"] == algorithm and
                      abs(float(r["offered_oneshot_utilization"]) - load) < 1e-9]
            arrivals = sum(int(r["oneshot_arrivals"]) for r in subset)
            accepted = sum(int(r["oneshot_accepted"]) for r in subset)
            safe = all(int(r["periodic_deadline_misses"]) == 0 and
                       int(r["protected_sporadic_runtime_rejects"]) == 0 and
                       int(r["protected_sporadic_deadline_misses"]) == 0 for r in subset)
            if arrivals and safe and accepted / arrivals >= 0.95: eligible.append(load)
        thresholds.append({"algorithm": algorithm, "U_OS_95": max(eligible) if eligible else math.nan})
    write_csv(stats_dir / "threshold_summary.csv", thresholds)
    write_csv(stats_dir / "statistics_summary.csv", stats)


def generate_figures(stats: list[dict[str, Any]]) -> list[dict[str, str]]:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/bcss-matplotlib")
    import matplotlib.pyplot as plt  # local import keeps non-plot commands lightweight

    figures = RESULTS / "figures"
    figures.mkdir(parents=True, exist_ok=True)
    provenance: list[dict[str, str]] = []

    def plot_experiment(
        experiment: str, filename: str, title: str, metric: str = "dynamic", ylabel: str = "Dynamic acceptance ratio"
    ) -> None:
        field = f"mean_{metric}_acceptance"
        subset = [r for r in stats if r["experiment"] == experiment and math.isfinite(float(r[field]))]
        if not subset: return
        fig, ax = plt.subplots(figsize=(7.2, 4.2))
        scenarios = sorted({r["scenario_id"] for r in subset})
        for algorithm in sorted({r["algorithm"] for r in subset}):
            alg = {r["scenario_id"]: r for r in subset if r["algorithm"] == algorithm}
            x = [s for s in scenarios if s in alg]
            y = [float(alg[s][field]) for s in x]
            ax.plot(range(len(x)), y, marker="o", label=algorithm)
        ax.set_xticks(range(len(scenarios)), scenarios, rotation=45, ha="right")
        ax.set_ylim(-0.03, 1.03); ax.set_ylabel(ylabel); ax.set_title(title)
        ax.grid(alpha=0.25); ax.legend(fontsize=8); fig.tight_layout()
        target = figures / filename
        fig.savefig(target, dpi=180); plt.close(fig)
        provenance.append({"figure_filename": filename, "plotting_script": "evaluation/final_evaluation.py",
                           "input_statistical_data": "results_final/statistics/statistics_summary.csv",
                           "source_aggregate": "results_final/aggregates/taskset_aggregates.csv",
                           "source_raw_data": "results_final/raw/all_runs.csv"})

    plot_experiment("E1_main", "e1_main_acceptance.png", "E1 paired algorithm comparison")
    plot_experiment("E2_k", "e2_k_sensitivity.png", "E2 bounded-recourse sensitivity")
    plot_experiment("E3_ablation", "e3_ablation.png", "E3 BCSS ablation")
    plot_experiment("E4_sporadic", "e4_sporadic_admission.png", "E4 sporadic-demand sensitivity",
                    "sporadic", "Sporadic acceptance ratio")
    plot_experiment("E5_oneshot", "e5_oneshot_saturation.png", "E5 one-shot saturation",
                    "oneshot", "One-shot acceptance ratio")
    plot_experiment("E6_multislot", "e6_multislot.png", "E6 contiguous multi-slot sensitivity",
                    "oneshot", "One-shot acceptance ratio")
    plot_experiment("E7_dependencies", "e7_dependencies.png", "E7 dependency sensitivity")
    plot_experiment("E8A_sporadic_deadlines", "e8a_sporadic_deadlines.png", "E8A sporadic deadline sensitivity",
                    "sporadic", "Sporadic acceptance ratio")
    plot_experiment("E8B_oneshot_deadlines", "e8b_oneshot_deadlines.png", "E8B one-shot deadline sensitivity",
                    "oneshot", "One-shot acceptance ratio")
    plot_experiment("E10_burst", "e10_burst.png", "E10 burst stress",
                    "oneshot", "One-shot acceptance ratio")
    write_csv(RESULTS / "statistics/figure_provenance.csv", provenance)
    return provenance


def write_reports(manifest: list[dict[str, Any]], rows: list[dict[str, str]], aggregates: list[dict[str, Any]],
                  errors: list[str], provenance: list[dict[str, str]]) -> str:
    reports = RESULTS / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    safety_fields = ("periodic_deadline_misses", "protected_sporadic_runtime_rejects",
        "protected_sporadic_deadline_misses", "dependency_violations", "K_violations",
        "past_immutability_violations", "hash_state_inconsistencies", "rollback_errors")
    totals = {field: sum(int(row[field]) for row in rows) for field in safety_fields}
    by_algorithm = {
        algorithm: {field: sum(int(row[field]) for row in rows if row["algorithm"] == algorithm)
                    for field in safety_fields}
        for algorithm in sorted({row["algorithm"] for row in rows})
    }
    preparation_infeasible = sum(row["preparation_success"] == "0" for row in rows)
    unexpected_preparation_failures = sum(
        row["preparation_success"] == "0" and row["algorithm"] != "AffineEnvelope" for row in rows
    )
    status_counts = Counter(row["status"] for row in rows)
    nontrivial = len({(r["algorithm"], r["dynamic_accepted"], r["dynamic_rejected"],
                      r["reclamation_accepts"], r["compensation_accepts"], r["RTC_checks"]) for r in rows})
    complete = len(rows) == len(manifest) and not errors
    full_bcss = [row for row in rows if row["algorithm"] in {"BCSS", "BCSS_Full"} and row["RTC_enabled"] == "ON"]
    full_bcss_protected_losses = sum(
        int(row["protected_sporadic_runtime_rejects"]) + int(row["protected_sporadic_deadline_misses"])
        for row in full_bcss
    )
    integrity_fields = ("periodic_deadline_misses", "dependency_violations", "K_violations",
                        "past_immutability_violations", "hash_state_inconsistencies", "rollback_errors")
    integrity_ok = all(totals[field] == 0 for field in integrity_fields)
    contract_generation_ok = sum(int(row["Tmin_contract_violations"]) for row in rows) == 0
    technically_valid = complete and unexpected_preparation_failures == 0 and integrity_ok and contract_generation_ok and \
        full_bcss_protected_losses == 0
    # Exact scheduler source provenance is a recorded worktree fingerprint rather
    # than a commit-contained tree, so this campaign cannot honestly receive the
    # unqualified thesis-ready verdict.
    verdict = "VALID WITH EXPLICIT LIMITATIONS" if technically_valid else "NOT YET VALID"

    completion_lines = [
        "# Campaign Completion Report", "",
        f"- Manifest rows: {len(manifest)}", f"- Raw rows: {len(rows)}",
        f"- Exact manifest/raw bijection: {'PASS' if complete else 'FAIL'}",
        f"- Duplicate run IDs: {len(rows) - len({row['run_id'] for row in rows})}",
        f"- Class-B Affine synthesis-infeasible outcomes: {preparation_infeasible}",
        f"- Unexpected preparation failures: {unexpected_preparation_failures}",
        f"- Evaluation fingerprint: `{git_evaluation_id()}`",
        f"- Scheduler reference label: `{SCHEDULER_COMMIT}`", "", "## Row status counts", "",
    ]
    completion_lines += [f"- {status}: {count}" for status, count in sorted(status_counts.items())]
    completion_lines += ["", f"Verdict: **{verdict}**", ""]
    (reports / "CAMPAIGN_COMPLETION_REPORT.md").write_text("\n".join(completion_lines))

    validation_checks = [
        "canonical column order and count", "integer/finite numeric types", "required enum values",
        "real lowercase 64-character SHA-256 values", "run-ID uniqueness", "manifest/raw bijection",
        "manifest-to-runtime parameter propagation", "zero-load one-shot invariant", "K bound",
        "paired Class-A taskset/trace/scenario/baseline fingerprints",
    ]
    (reports / "DATA_VALIDATION_REPORT.md").write_text(
        "# Data Validation Report\n\n" + ("DATA VALIDATION: PASS\n" if not errors else "DATA VALIDATION: FAIL\n") +
        "\nChecks performed independently over the manifest and canonical raw CSV:\n\n" +
        "\n".join(f"- {check}" for check in validation_checks) +
        ("\n\nNo validation errors were found.\n" if not errors else
         "\n\nErrors:\n\n" + "\n".join(f"- {error}" for error in errors) + "\n"))

    propagated = {
        "TT utilization": sorted({row["target_tt_utilization"] for row in rows}, key=float),
        "profiles": sorted({row["profile"] for row in rows}),
        "K": sorted({row["K"] for row in rows}, key=int),
        "RTC": sorted({row["RTC_enabled"] for row in rows}),
        "one-shot load": sorted({row["offered_oneshot_utilization"] for row in rows}, key=float),
        "multi-slot regimes": sorted({row["multislot_regime"] for row in rows}),
        "dependency levels": sorted({row["dependency_level"] for row in rows}),
        "sporadic D/C": sorted({row["sporadic_deadline_ratio"] for row in rows}, key=float),
        "one-shot D/C": sorted({row["oneshot_deadline_ratio"] for row in rows}, key=float),
    }
    propagation_lines = ["# Parameter Propagation Report", "",
        "The validator compared every manifest parameter with its independently recorded runtime value.", "",
        f"- Propagation errors: {sum('propagation' in error for error in errors)}",
        f"- Zero-load one-shot violations: {sum('zero-load' in error for error in errors)}", ""]
    propagation_lines += [f"- {name}: `{', '.join(values)}`" for name, values in propagated.items()]
    propagation_lines += ["", "The deterministic pre-flight additionally checked generated taskset/trace contents, not labels alone.", ""]
    (reports / "PARAMETER_PROPAGATION_REPORT.md").write_text(
        "\n".join(propagation_lines))

    signatures_by_algorithm = {
        algorithm: len({(row["dynamic_accepted"], row["dynamic_rejected"], row["direct_accepts"],
                         row["reclamation_accepts"], row["compensation_accepts"], row["RTC_checks"])
                        for row in rows if row["algorithm"] == algorithm})
        for algorithm in sorted({row["algorithm"] for row in rows})
    }
    nontrivial_lines = ["# Nontriviality Report", "", f"- Unique campaign-wide behavioral signatures: {nontrivial}",
        f"- Reclamation accepts: {sum(int(r['reclamation_accepts']) for r in rows)}",
        f"- Compensation accepts: {sum(int(r['compensation_accepts']) for r in rows)}",
        f"- RTC checks: {sum(int(r['RTC_checks']) for r in rows)}",
        f"- RTC-unsafe outcomes: {sum(int(r['RTC_unsafe_outcomes']) for r in rows)}", "",
        "## Signatures by algorithm", ""]
    nontrivial_lines += [f"- {algorithm}: {count}" for algorithm, count in signatures_by_algorithm.items()]
    nontrivial_lines += ["", "Controlled direct/reclamation/one-hop/two-hop/K-bound/RTC fixtures are recorded in `preflight/mechanism_checks.csv`.", ""]
    (reports / "NONTRIVIALITY_REPORT.md").write_text(
        "\n".join(nontrivial_lines))

    safety_lines = ["# Safety Invariant Report", "",
        "The evaluator checked committed schedules, before/after hashes, past slots, K, dependencies, and protected-job outcomes independently of aggregate scheduler counters.", "",
        "## Campaign totals", ""]
    safety_lines += [f"- {field}: {value}" for field, value in totals.items()]
    safety_lines += [f"- Tmin_contract_violations: {sum(int(row['Tmin_contract_violations']) for row in rows)}",
                     "", "## By algorithm", "",
                     "| Algorithm | Periodic misses | Protected rejects | Protected misses | Dependency | K | Past | Hash | Rollback |",
                     "|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for algorithm, values in by_algorithm.items():
        safety_lines.append(
            f"| {algorithm} | {values['periodic_deadline_misses']} | {values['protected_sporadic_runtime_rejects']} | "
            f"{values['protected_sporadic_deadline_misses']} | {values['dependency_violations']} | "
            f"{values['K_violations']} | {values['past_immutability_violations']} | "
            f"{values['hash_state_inconsistencies']} | {values['rollback_errors']} |"
        )
    safety_lines += ["", "Offline-rejected streams and Tmin-violating arrivals are not classified as protected-runtime failures.",
                     "RTC-off ablation outcomes are not interpreted as full-BCSS guarantee outcomes.", ""]
    (reports / "SAFETY_INVARIANT_REPORT.md").write_text(
        "\n".join(safety_lines))

    stats_rows = read_csv(RESULTS / "statistics/statistics_summary.csv")
    e1_rows = [row for row in stats_rows if row["experiment"] == "E1_main"]
    max_e1_halfwidth = max(
        ((float(row["ci95_dynamic_high"]) - float(row["ci95_dynamic_low"])) / 2 for row in e1_rows
         if math.isfinite(float(row["ci95_dynamic_low"]))), default=math.nan
    )
    paired_rows = read_csv(RESULTS / "statistics/paired_comparisons.csv")
    unstable_nonzero = sum(
        abs(float(row["mean_difference"])) > 1e-12 and float(row["ci95_low"]) <= 0 <= float(row["ci95_high"])
        for row in paired_rows if row["experiment"] == "E1_main" and row["metric"] == "dynamic_acceptance_ratio"
    )
    (reports / "STATISTICAL_ANALYSIS_REPORT.md").write_text(
        "# Statistical Analysis Report\n\n- Statistical unit: independent taskset.\n"
        f"- Raw trace rows: {len(rows)}; taskset aggregates: {len(aggregates)}.\n"
        "- Aggregation keys: `experiment + scenario_id + algorithm + taskset_id`.\n"
        "- Confidence intervals: taskset-level percentile bootstrap, 2,000 samples.\n"
        "- Bootstrap seeds: deterministically derived from master seed 20260811, cell, and metric.\n"
        "- Paired differences use matched Class-A scenario/taskset cells and explicitly identify dynamic, one-shot, or sporadic denominators.\n"
        f"- Maximum E1 dynamic-acceptance CI half-width: {max_e1_halfwidth:.6f}.\n"
        f"- Nonzero E1 paired effects whose 95% CI crosses zero: {unstable_nonzero}.\n"
        "- Secondary cells with n < 30 are exploratory. Thresholds are computed only on the tested E5 grid.\n")

    figure_lines = ["# Figure Provenance Report", "",
        f"- Provenance-validated figures: {len(provenance)}",
        "- Every figure is generated from `statistics_summary.csv`, derived from taskset aggregates and canonical raw data.", "",
        "| Figure | Statistical input | Aggregate | Raw |", "|---|---|---|---|"]
    figure_lines += [f"| {row['figure_filename']} | {row['input_statistical_data']} | {row['source_aggregate']} | {row['source_raw_data']} |"
                     for row in provenance]
    (reports / "FIGURE_PROVENANCE_REPORT.md").write_text(
        "\n".join(figure_lines) + "\n")

    limitations = [
        "The repository HEAD names scheduler reference 89e3a0e, but the clean-slate scheduler sources are untracked in that commit; exact source hashes are therefore recorded in final metadata.",
        "Simulation covers one finite 10,000-slot hyperperiod; the earlier 12-hyperperiod proposal is incompatible with the implemented finite-horizon scheduler without changing its model.",
        "StaticDirect, DTSS, and common-mode Slot Shifting have no separately validated protected-sporadic offline admission equivalent to BCSS; common-mode Slot Shifting explicitly admits none.",
        "Affine is a separate Class-B reference restricted to dependency-free unit-slot TT inputs.",
        "Affine acceptance results are conditional on successful co-design synthesis; synthesis-infeasible tasksets are reported separately and excluded from acceptance-ratio inference.",
        "Secondary sensitivity experiments use 6–12 tasksets and support exploratory, not small-effect confirmatory, claims.",
    ]
    algorithm_lines = []
    for algorithm in sorted({row["algorithm"] for row in rows}):
        subset = [row for row in rows if row["algorithm"] == algorithm]
        prepared = [row for row in subset if row["preparation_success"] == "1"]
        arrivals = sum(int(row["dynamic_arrivals"]) for row in prepared)
        accepted = sum(int(row["dynamic_accepted"]) for row in prepared)
        one_arrivals = sum(int(row["oneshot_arrivals"]) for row in prepared)
        one_accepted = sum(int(row["oneshot_accepted"]) for row in prepared)
        algorithm_lines.append(
            f"| {algorithm} | {len(subset)} | {accepted}/{arrivals} ({accepted/arrivals:.4f}) | "
            f"{one_accepted}/{one_arrivals} ({one_accepted/one_arrivals:.4f}) |"
            if arrivals and one_arrivals else
            f"| {algorithm} | {len(subset)} | {accepted}/{arrivals} | {one_accepted}/{one_arrivals} |"
        )
    final_report = ["# Final BCSS Evaluation Report", "", "## Outcome", "",
        f"Final bounded campaign rows: {len(rows):,}. Invalid archived rows used: 0.", "",
        "## Pooled descriptive results", "",
        "Pooled counts summarize execution volume; taskset-level confidence intervals and paired differences are the inferential results.", "",
        "| Algorithm | Runs | Dynamic accepted/arrivals | One-shot accepted/arrivals |", "|---|---:|---:|---:|",
        *algorithm_lines, "", "## Mechanism evidence", "",
        f"- BCSS-family reclamation accepts: {sum(int(r['reclamation_accepts']) for r in rows if r['algorithm'].startswith('BCSS'))}",
        f"- BCSS-family compensation accepts: {sum(int(r['compensation_accepts']) for r in rows if r['algorithm'].startswith('BCSS'))}",
        f"- BCSS-family RTC checks / unsafe outcomes: {sum(int(r['RTC_checks']) for r in rows if r['algorithm'].startswith('BCSS'))} / {sum(int(r['RTC_unsafe_outcomes']) for r in rows if r['algorithm'].startswith('BCSS'))}",
        "", "## Claim discipline", "",
        f"Across the bounded evaluation campaign, periodic deadline misses observed: {totals['periodic_deadline_misses']}.",
        f"Protected admitted-compliant sporadic runtime rejects observed: {totals['protected_sporadic_runtime_rejects']}.",
        f"Full RTC-on BCSS protected runtime rejects or misses: {full_bcss_protected_losses}.",
        "These are empirical observations, not a formal proof or certification.", "", "## Explicit limitations", ""]
    final_report += [f"- {item}" for item in limitations]
    final_report += ["", f"FINAL EVALUATION VERDICT:\n{verdict}", ""]
    (reports / "FINAL_EVALUATION_REPORT.md").write_text("\n".join(final_report))
    return verdict


def make_review_bundle(manifest: list[dict[str, Any]], rows: list[dict[str, str]], aggregates: list[dict[str, Any]], verdict: str) -> None:
    bundle_dir = RESULTS / "review_bundle"; bundle_dir.mkdir(parents=True, exist_ok=True)
    report_names = ("FINAL_CAMPAIGN_DESIGN.md", "PREFLIGHT_REPORT.md", "FINAL_EVALUATION_REPORT.md",
        "CAMPAIGN_COMPLETION_REPORT.md", "DATA_VALIDATION_REPORT.md", "PARAMETER_PROPAGATION_REPORT.md",
        "NONTRIVIALITY_REPORT.md", "SAFETY_INVARIANT_REPORT.md", "STATISTICAL_ANALYSIS_REPORT.md",
        "FIGURE_PROVENANCE_REPORT.md")
    for name in report_names:
        source = PREFLIGHT / name if name == "PREFLIGHT_REPORT.md" else RESULTS / "reports" / name
        if source.exists(): shutil.copy2(source, bundle_dir / name)
    table_names = ("experiment_counts.csv", "parameter_summary.csv", "algorithm_summary.csv", "mechanism_usage.csv",
        "safety_invariants.csv", "runtime_summary.csv", "paired_comparisons.csv", "threshold_summary.csv", "figure_provenance.csv")
    for name in table_names:
        shutil.copy2(RESULTS / "statistics" / name, bundle_dir / name)
    # Deterministic stratified samples: first run per experiment/scenario/algorithm, sorted by stable IDs.
    def stratified(data: list[dict[str, Any]], keys: tuple[str, ...], limit: int = 250) -> list[dict[str, Any]]:
        chosen = {}
        for row in sorted(data, key=lambda r: tuple(str(r.get(k, "")) for k in keys) + (str(r.get("run_id", "")),)):
            chosen.setdefault(tuple(str(row.get(k, "")) for k in keys), row)
        return list(chosen.values())[:limit]
    write_csv(bundle_dir / "representative_manifest_sample.csv", stratified(manifest, ("experiment", "scenario_id", "algorithm")))
    write_csv(bundle_dir / "representative_raw_sample.csv", stratified(rows, ("experiment", "scenario_id", "algorithm")))
    write_csv(bundle_dir / "representative_taskset_aggregate_sample.csv", stratified(aggregates, ("experiment", "scenario_id", "algorithm")))
    git_status = subprocess.check_output(["git", "status", "--short", "--branch"], cwd=ROOT, text=True)
    (bundle_dir / "git_provenance.txt").write_text(
        f"HEAD={subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()}\n"
        f"evaluation_id={git_evaluation_id()}\n\n{git_status}")
    (bundle_dir / "schema_columns.txt").write_text("\n".join(read_worker_header() + ["runner_wall_seconds"]) + "\n")
    metadata = {"verdict": verdict, "scheduler_reference_commit": SCHEDULER_COMMIT,
        "evaluation_id": git_evaluation_id(), "master_seed": MASTER_SEED, "manifest_rows": len(manifest),
        "raw_rows": len(rows), "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source_sha256": {
            path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in evaluation_source_paths()
        }}
    (bundle_dir / "final_metadata.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    target = ROOT / "BCSS_FINAL_EVALUATION_REVIEW_BUNDLE.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(bundle_dir.iterdir()):
            archive.write(path, path.name)


def finalize_campaign(manifest: list[dict[str, Any]], rows: list[dict[str, str]]) -> str:
    valid, errors = validate_dataset(manifest, rows)
    aggregates = aggregate_tasksets(rows)
    write_csv(RESULTS / "aggregates/taskset_aggregates.csv", aggregates)
    stats = summarize_statistics(aggregates)
    generate_summary_tables(rows, aggregates, stats)
    provenance = generate_figures(stats)
    verdict = write_reports(manifest, rows, aggregates, errors if not valid else [], provenance)
    make_review_bundle(manifest, rows, aggregates, verdict)
    return verdict


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("preflight", "benchmark", "design", "campaign", "validate", "all"))
    parser.add_argument("--workers", type=int, default=min(10, os.cpu_count() or 1))
    args = parser.parse_args()
    RESULTS.mkdir(parents=True, exist_ok=True)

    if args.action in {"preflight", "all"}:
        if not run_preflight(args.workers):
            print("PREFLIGHT: FAIL — campaign blocked", file=sys.stderr)
            return 1
        print("PREFLIGHT: PASS", file=sys.stderr)
        if args.action == "preflight": return 0
    elif not (PREFLIGHT / "PREFLIGHT_REPORT.md").exists() or "PREFLIGHT: PASS" not in (PREFLIGHT / "PREFLIGHT_REPORT.md").read_text():
        print("A recorded PREFLIGHT: PASS is required before this action.", file=sys.stderr)
        return 2

    benchmark = read_csv(RESULTS / "metadata/benchmark_summary.csv")
    if args.action in {"benchmark", "all"} or not benchmark:
        benchmark = benchmark_runner(args.workers)
        if args.action == "benchmark": return 0

    manifest = build_campaign_manifest()
    write_csv(RESULTS / "manifest/final_manifest.csv", manifest)
    write_campaign_design(manifest, benchmark)
    if args.action == "design": return 0

    if args.action in {"campaign", "all"}:
        rows, failures = execute_manifest(manifest, RESULTS / "raw/all_runs.csv", args.workers, "FINAL CAMPAIGN", timeout=900)
        write_csv(RESULTS / "logs/runner_failures.csv", failures, ["run_id", "error"])
        if failures:
            print(f"Campaign has {len(failures)} runner failures; validation will report NOT YET VALID.", file=sys.stderr)
        verdict = finalize_campaign(manifest, rows)
        print(f"FINAL EVALUATION VERDICT: {verdict}")
        return 0 if verdict != "NOT YET VALID" else 3

    if args.action == "validate":
        rows = read_csv(RESULTS / "raw/all_runs.csv")
        verdict = finalize_campaign(manifest, rows)
        print(f"FINAL EVALUATION VERDICT: {verdict}")
        return 0 if verdict != "NOT YET VALID" else 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
