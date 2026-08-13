#include "comparisons/affine_envelope_scheduler.hpp"
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/neutral_validator.hpp"
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/workload_adapter.hpp"
#include "bcss/hasher.hpp"
#include "bcss/scheduler.hpp"
#include "bcss/validator.hpp"
#include "bcss/workload.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

const std::vector<std::string> kColumns = {
    "run_id", "experiment", "scenario_id", "algorithm", "algorithm_mode",
    "taskset_id", "trace_id", "master_seed", "experiment_seed", "scenario_seed",
    "taskset_seed", "trace_seed", "scheduler_commit", "evaluation_commit",
    "slot_quantum_ms", "horizon_slots", "hyperperiod_ms", "simulation_duration_slots",
    "warmup_hyperperiods", "measurement_hyperperiods", "N_persistent", "N_periodic_streams",
    "N_candidate_sporadic_streams", "target_tt_utilization", "actual_tt_utilization",
    "candidate_sporadic_utilization", "admitted_sporadic_utilization",
    "offered_oneshot_utilization", "actual_offered_oneshot_utilization", "profile", "K",
    "RTC_enabled", "reclamation_enabled", "compensation_enabled", "dependency_level",
    "dependency_edges", "multislot_regime", "max_duration", "sporadic_deadline_ratio",
    "oneshot_deadline_ratio", "burst_mode", "taskset_sha256", "trace_sha256",
    "scenario_input_sha256", "baseline_schedule_sha256", "final_schedule_sha256",
    "periodic_releases", "periodic_deadline_misses", "candidate_sporadic_streams",
    "offline_admitted_sporadic_streams", "offline_rejected_sporadic_streams",
    "sporadic_arrivals", "Tmin_compliant_sporadic_arrivals", "Tmin_contract_violations",
    "sporadic_accepted", "sporadic_rejected", "protected_sporadic_runtime_rejects",
    "protected_sporadic_deadline_misses", "oneshot_arrivals", "oneshot_accepted",
    "oneshot_rejected", "dynamic_arrivals", "dynamic_accepted", "dynamic_rejected",
    "accepted_dynamic_slots", "direct_accepts", "reclamation_accepts", "compensation_accepts",
    "actual_k_sum", "mean_actual_k", "max_actual_k", "k0_accepts", "k1_accepts",
    "k2_accepts", "k3_accepts", "k4_accepts", "k_gt4_accepts", "delta_max",
    "delta_total", "jobs_moved", "slots_changed", "candidate_schedules_generated",
    "candidate_schedules_feasible", "candidate_schedules_rtc_safe", "RTC_checks",
    "RTC_unsafe_outcomes", "RTC_induced_rejections", "search_states", "search_branches_pruned",
    "maximum_search_depth", "mean_latency_ns", "median_latency_ns", "p95_latency_ns",
    "p99_latency_ns", "max_latency_ns", "dependency_violations", "K_violations",
    "past_immutability_violations", "hash_state_inconsistencies", "rollback_errors",
    "preparation_success", "preparation_message", "status"
};

std::map<std::string, std::string> parse_args(int argc, char** argv) {
    std::map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key.rfind("--", 0) != 0) continue;
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
            args[key.substr(2)] = argv[++i];
        } else {
            args[key.substr(2)] = "1";
        }
    }
    return args;
}

std::string get_arg(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    const std::string& fallback
) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : it->second;
}

uint64_t get_u64(const std::map<std::string, std::string>& args, const std::string& key, uint64_t fallback) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : static_cast<uint64_t>(std::stoull(it->second));
}

int64_t get_i64(const std::map<std::string, std::string>& args, const std::string& key, int64_t fallback) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : static_cast<int64_t>(std::stoll(it->second));
}

double get_double(const std::map<std::string, std::string>& args, const std::string& key, double fallback) {
    const auto it = args.find(key);
    return it == args.end() ? fallback : std::stod(it->second);
}

bool get_bool(const std::map<std::string, std::string>& args, const std::string& key, bool fallback) {
    const std::string value = get_arg(args, key, fallback ? "1" : "0");
    return value == "1" || value == "true" || value == "TRUE" || value == "ON";
}

std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '\"') escaped += "\"\"";
        else escaped += ch;
    }
    escaped += "\"";
    return escaped;
}

std::string as_string(double value) {
    std::ostringstream stream;
    stream << std::setprecision(12) << value;
    return stream.str();
}

std::string neutral_schedule_hash(const comparisons::NeutralBaselineSchedule& schedule) {
    comparisons::NeutralWorkload empty;
    empty.horizon = schedule.horizon;
    return comparisons::compute_input_fingerprint(empty, schedule);
}

double quantile(std::vector<uint64_t> values, double probability) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = probability * static_cast<double>(values.size() - 1U);
    const auto lower = static_cast<size_t>(std::floor(position));
    const auto upper = static_cast<size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return static_cast<double>(values[lower]) * (1.0 - fraction) +
           static_cast<double>(values[upper]) * fraction;
}

bcss::WorkloadProfile parse_profile(const std::string& profile) {
    if (profile == "QUIET") return bcss::WorkloadProfile::Quiet;
    if (profile == "BUSY") return bcss::WorkloadProfile::Busy;
    if (profile == "WORST") return bcss::WorkloadProfile::Worst;
    return bcss::WorkloadProfile::Normal;
}

struct RunMetrics {
    uint64_t periodic_misses{0};
    uint64_t sporadic_arrivals{0};
    uint64_t compliant_sporadic_arrivals{0};
    uint64_t tmin_violations{0};
    uint64_t sporadic_accepted{0};
    uint64_t sporadic_rejected{0};
    uint64_t protected_rejects{0};
    uint64_t protected_deadline_misses{0};
    uint64_t oneshot_arrivals{0};
    uint64_t oneshot_accepted{0};
    uint64_t oneshot_rejected{0};
    uint64_t accepted_slots{0};
    uint64_t direct_accepts{0};
    uint64_t reclaim_accepts{0};
    uint64_t compensation_accepts{0};
    uint64_t actual_k_sum{0};
    uint64_t max_actual_k{0};
    std::vector<uint64_t> k_accepts{0, 0, 0, 0, 0, 0};
    uint64_t delta_max{0};
    uint64_t delta_total{0};
    uint64_t jobs_moved{0};
    uint64_t slots_changed{0};
    uint64_t candidates_generated{0};
    uint64_t candidates_feasible{0};
    uint64_t rtc_safe_candidates{0};
    uint64_t rtc_checks{0};
    uint64_t rtc_unsafe{0};
    uint64_t rtc_induced_rejections{0};
    uint64_t search_states{0};
    uint64_t search_pruned{0};
    uint64_t max_search_depth{0};
    uint64_t dependency_violations{0};
    uint64_t k_violations{0};
    uint64_t past_violations{0};
    uint64_t hash_errors{0};
    uint64_t rollback_errors{0};
    std::vector<uint64_t> latencies{};
};

uint64_t count_dependency_edges(const bcss::DependencyGraph& graph) {
    uint64_t count = 0;
    for (const auto& entry : graph.adj_list) {
        count += static_cast<uint64_t>(entry.second.size());
    }
    return count;
}

bool tmin_compliant(
    const comparisons::ComparisonJob& request,
    std::unordered_map<comparisons::TaskID, comparisons::SlotIndex>& last_release
) {
    if (request.type != comparisons::TrafficType::Sporadic) return true;
    const auto previous = last_release.find(request.task_id);
    const auto tmin = request.min_interarrival.value_or(1);
    const bool compliant = previous == last_release.end() || request.release - previous->second >= tmin;
    last_release[request.task_id] = request.release;
    return compliant;
}

void update_common_decision_metrics(
    RunMetrics& metrics,
    const comparisons::ComparisonJob& request,
    bool accepted,
    bool protected_stream,
    bool compliant
) {
    if (request.type == comparisons::TrafficType::Sporadic) {
        ++metrics.sporadic_arrivals;
        if (compliant) ++metrics.compliant_sporadic_arrivals;
        else ++metrics.tmin_violations;
        if (accepted) ++metrics.sporadic_accepted;
        else {
            ++metrics.sporadic_rejected;
            if (protected_stream && compliant) ++metrics.protected_rejects;
        }
    } else if (request.type == comparisons::TrafficType::OneShot) {
        ++metrics.oneshot_arrivals;
        if (accepted) ++metrics.oneshot_accepted;
        else ++metrics.oneshot_rejected;
    }
    if (accepted) metrics.accepted_slots += static_cast<uint64_t>(request.execution_requirement);
}

void print_header() {
    for (size_t index = 0; index < kColumns.size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << kColumns[index];
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const auto args = parse_args(argc, argv);
    if (args.count("header") != 0U) {
        print_header();
        return 0;
    }

    const std::string run_id = get_arg(args, "run-id", "run-0");
    const std::string experiment = get_arg(args, "experiment", "PREFLIGHT");
    const std::string scenario_id = get_arg(args, "scenario-id", "default");
    const std::string algorithm = get_arg(args, "algorithm", "BCSS");
    const uint64_t taskset_id = get_u64(args, "taskset-id", 0);
    const uint64_t trace_id = get_u64(args, "trace-id", 0);
    const uint64_t taskset_seed = get_u64(args, "taskset-seed", 101);
    const uint64_t trace_seed = get_u64(args, "trace-seed", 202);
    const uint64_t master_seed = get_u64(args, "master-seed", 42);
    const uint64_t experiment_seed = get_u64(args, "experiment-seed", 0);
    const uint64_t scenario_seed = get_u64(args, "scenario-seed", 0);
    const std::string scheduler_commit = get_arg(args, "scheduler-commit", "89e3a0e");
    const std::string evaluation_commit = get_arg(args, "evaluation-commit", "WORKTREE");

    bcss::WorkloadConfig config;
    config.horizon = get_i64(args, "horizon", 10000);
    config.seed = taskset_seed;
    config.trace_seed = trace_seed;
    config.periodic_load = get_double(args, "tt-util", 0.70);
    config.oneshot_ratio = get_double(args, "oneshot-util", 0.06);
    config.periodic_stream_count = get_i64(args, "periodic-streams", 70);
    config.candidate_sporadic_stream_count = get_i64(args, "sporadic-streams", 30);
    config.candidate_sporadic_utilization = get_double(args, "sporadic-util", 0.15);
    config.profile = parse_profile(get_arg(args, "profile", "NORMAL"));
    config.multislot_regime = get_arg(args, "multislot", "mixed");
    config.max_duration = get_i64(args, "max-duration", 4);
    config.allow_multi_slot = config.max_duration > 1;
    config.sporadic_deadline_ratio = get_double(args, "sporadic-deadline-ratio", 10.0);
    config.oneshot_deadline_ratio = get_double(args, "oneshot-deadline-ratio", 10.0);
    config.synchronized_arrivals = get_arg(args, "burst-mode", "RANDOM") == "SYNC";
    const std::string dependency_level = get_arg(args, "dependency-level", "0");
    const double dependency_density = get_double(args, "dependency-density", 0.0);
    config.dependency_density = dependency_density;

    const int64_t configured_k = get_i64(args, "K", 2);
    const bool rtc_enabled = get_bool(args, "rtc", true);
    const bool reclamation_enabled = get_bool(args, "reclamation", true);
    const bool compensation_enabled = get_bool(args, "compensation", true);
    const std::string profile_name = get_arg(args, "profile", "NORMAL");
    const std::string burst_mode = get_arg(args, "burst-mode", "RANDOM");

    bcss::GeneratedTaskset taskset;
    try {
        taskset = bcss::WorkloadGenerator::generate(config);
    } catch (const std::exception& error) {
        std::cerr << "Workload generation failed: " << error.what() << '\n';
        return 2;
    }

    comparisons::NeutralWorkload workload = comparisons::WorkloadAdapter::convert_taskset(taskset);
    comparisons::NeutralBaselineSchedule baseline = comparisons::WorkloadAdapter::convert_schedule(taskset.baseline_schedule);
    comparisons::NeutralWorkload taskset_only = workload;
    taskset_only.dynamic_arrivals.clear();
    comparisons::NeutralWorkload trace_only;
    trace_only.horizon = workload.horizon;
    trace_only.dynamic_arrivals = workload.dynamic_arrivals;
    comparisons::NeutralBaselineSchedule empty_baseline(workload.horizon);

    const std::string taskset_hash = comparisons::compute_input_fingerprint(taskset_only, baseline);
    const std::string trace_hash = comparisons::compute_input_fingerprint(trace_only, empty_baseline);
    const std::string input_hash = comparisons::compute_input_fingerprint(workload, baseline);
    const std::string baseline_hash = bcss::ScheduleHasher::compute_hash(taskset.baseline_schedule);

    uint64_t occupied_tt_slots = 0;
    for (const auto& slot : taskset.baseline_schedule.slots) {
        if (!slot.is_free()) ++occupied_tt_slots;
    }
    uint64_t offered_oneshot_slots = 0;
    double candidate_sporadic_util = 0.0;
    for (const auto& stream : taskset.sporadic_streams) {
        candidate_sporadic_util += static_cast<double>(stream.duration) /
                                   static_cast<double>(stream.min_inter_arrival);
    }
    for (const auto& request : workload.dynamic_arrivals) {
        if (request.type == comparisons::TrafficType::OneShot) {
            offered_oneshot_slots += static_cast<uint64_t>(request.execution_requirement);
        }
    }

    RunMetrics metrics;
    bool preparation_success = true;
    std::string preparation_message = "PREPARED";
    uint64_t offline_admitted = 0;
    uint64_t offline_rejected = 0;
    double admitted_sporadic_util = 0.0;
    std::set<comparisons::TaskID> admitted_stream_ids;
    std::string final_schedule_hash = baseline_hash;

    if (algorithm == "BCSS" || algorithm.rfind("BCSS_", 0) == 0) {
        bcss::BcssScheduler scheduler(config.horizon, configured_k, rtc_enabled);
        scheduler.enable_reclamation = reclamation_enabled;
        scheduler.enable_compensation = compensation_enabled;
        scheduler.dependencies = taskset.dependencies;
        preparation_success = scheduler.set_periodic_baseline(taskset.periodic_jobs, taskset.baseline_schedule);
        preparation_message = preparation_success ? "BCSS_PREPARED" : "BCSS_BASELINE_INVALID";

        if (preparation_success) {
            for (const auto& stream : taskset.sporadic_streams) {
                if (scheduler.admit_sporadic_stream_offline(stream)) {
                    ++offline_admitted;
                    admitted_stream_ids.insert(stream.task_id);
                    admitted_sporadic_util += static_cast<double>(stream.duration) /
                                              static_cast<double>(stream.min_inter_arrival);
                } else {
                    ++offline_rejected;
                }
            }
        }

        std::unordered_map<comparisons::TaskID, comparisons::SlotIndex> last_release;
        for (const auto& request : workload.dynamic_arrivals) {
            const bool compliant = tmin_compliant(request, last_release);
            const bool protected_stream = request.type == comparisons::TrafficType::Sporadic &&
                admitted_stream_ids.count(request.task_id) != 0U;
            const bool is_unadmitted_sporadic = request.type == comparisons::TrafficType::Sporadic && !protected_stream;

            if (is_unadmitted_sporadic || !preparation_success) {
                update_common_decision_metrics(metrics, request, false, false, compliant);
                continue;
            }

            bcss::Job native_job(
                request.task_id,
                request.id,
                request.type == comparisons::TrafficType::Sporadic ? bcss::TaskType::Sporadic : bcss::TaskType::OneShot,
                request.release,
                request.relative_deadline,
                request.execution_requirement,
                -1,
                request.min_interarrival.value_or(-1)
            );
            native_job.precedence_parents.assign(request.predecessors.begin(), request.predecessors.end());

            const bcss::Schedule before = scheduler.active_schedule;
            const auto start = Clock::now();
            const bcss::BcssResult decision = scheduler.admit_dynamic_job(native_job, request.release);
            const auto stop = Clock::now();
            metrics.latencies.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()
            ));

            update_common_decision_metrics(metrics, request, decision.success, protected_stream, compliant);
            metrics.candidates_generated += decision.stats.candidates_generated;
            metrics.candidates_feasible += decision.stats.candidates_feasible;
            metrics.rtc_checks += decision.stats.rtc_checks;
            metrics.rtc_unsafe += decision.stats.rtc_unsafe;
            metrics.rtc_safe_candidates += decision.stats.rtc_checks - decision.stats.rtc_unsafe;
            metrics.search_states += decision.stats.nodes_expanded;
            metrics.search_pruned += decision.stats.paths_pruned;
            metrics.max_search_depth = std::max(metrics.max_search_depth, decision.stats.max_search_depth);

            if (decision.success) {
                if (decision.decision_mechanism == "ACCEPT_DIRECT") ++metrics.direct_accepts;
                else if (decision.decision_mechanism == "ACCEPT_RECLAIM") ++metrics.reclaim_accepts;
                else if (decision.decision_mechanism == "ACCEPT_COMPENSATION") ++metrics.compensation_accepts;
                metrics.actual_k_sum += static_cast<uint64_t>(decision.actual_k);
                metrics.max_actual_k = std::max(metrics.max_actual_k, static_cast<uint64_t>(decision.actual_k));
                const size_t k_bucket = decision.actual_k > 4 ? 5U : static_cast<size_t>(decision.actual_k);
                ++metrics.k_accepts[k_bucket];
                metrics.delta_max = std::max(metrics.delta_max, static_cast<uint64_t>(decision.max_disp));
                metrics.delta_total += static_cast<uint64_t>(decision.total_disp);
                metrics.jobs_moved += static_cast<uint64_t>(decision.actual_k);
            } else if (decision.stats.rtc_unsafe > 0U) {
                ++metrics.rtc_induced_rejections;
            }

            if (decision.actual_k > configured_k) ++metrics.k_violations;
            if (!decision.success && decision.pre_schedule_hash != decision.post_schedule_hash) {
                ++metrics.hash_errors;
                ++metrics.rollback_errors;
            }
            if (decision.success && decision.post_schedule_hash != bcss::ScheduleHasher::compute_hash(scheduler.active_schedule)) {
                ++metrics.hash_errors;
            }
            for (bcss::SlotIndex slot = 0; slot < request.release; ++slot) {
                if (before.slots[static_cast<size_t>(slot)].job_id !=
                    scheduler.active_schedule.slots[static_cast<size_t>(slot)].job_id) {
                    ++metrics.past_violations;
                    break;
                }
            }
        }

        final_schedule_hash = bcss::ScheduleHasher::compute_hash(scheduler.active_schedule);
        const bcss::ValidationResult final_validation = bcss::ScheduleValidator::verify_schedule(
            scheduler.active_schedule,
            scheduler.all_jobs,
            scheduler.dependencies,
            0
        );
        if (!final_validation.valid) ++metrics.dependency_violations;
        for (const auto& periodic_job : taskset.periodic_jobs) {
            const bcss::SlotIndex start = scheduler.active_schedule.get_job_start(periodic_job.job_id);
            if (start < periodic_job.release || start + periodic_job.duration > periodic_job.absolute_deadline) {
                ++metrics.periodic_misses;
            }
        }
        for (const auto& job : scheduler.all_jobs) {
            if (job.type != bcss::TaskType::Sporadic || admitted_stream_ids.count(job.task_id) == 0U) continue;
            const bcss::SlotIndex start = scheduler.active_schedule.get_job_start(job.job_id);
            if (start < job.release || start + job.duration > job.absolute_deadline) {
                ++metrics.protected_deadline_misses;
            }
        }
    } else {
        std::unique_ptr<comparisons::IComparisonScheduler> scheduler;
        if (algorithm == "StaticDirect") {
            scheduler = std::make_unique<comparisons::StaticDirectScheduler>();
        } else if (algorithm == "SlotShifting") {
            scheduler = std::make_unique<comparisons::SlotShiftingScheduler>();
        } else if (algorithm == "DTSS") {
            scheduler = std::make_unique<comparisons::DtssScheduler>();
        } else if (algorithm == "AffineEnvelope") {
            scheduler = std::make_unique<comparisons::AffineEnvelopeScheduler>();
        } else {
            std::cerr << "Unknown algorithm: " << algorithm << '\n';
            return 3;
        }

        const comparisons::PreparationResult prepared = scheduler->prepare(baseline, workload);
        preparation_success = prepared.success;
        preparation_message = prepared.message;
        offline_admitted = static_cast<uint64_t>(prepared.admitted_sporadic_streams);
        offline_rejected = static_cast<uint64_t>(prepared.rejected_sporadic_streams);

        if (algorithm == "SlotShifting") {
            const auto* slot_scheduler = dynamic_cast<const comparisons::SlotShiftingScheduler*>(scheduler.get());
            if (slot_scheduler != nullptr) {
                for (const auto& stream : workload.sporadic_streams) {
                    if (slot_scheduler->is_sporadic_stream_admitted(stream.stream_id)) {
                        admitted_stream_ids.insert(stream.stream_id);
                        admitted_sporadic_util += static_cast<double>(stream.execution_requirement) /
                                                  static_cast<double>(stream.min_interarrival);
                    }
                }
            }
        } else if (algorithm == "AffineEnvelope" && preparation_success) {
            for (const auto& stream : workload.sporadic_streams) {
                admitted_stream_ids.insert(stream.stream_id);
                admitted_sporadic_util += static_cast<double>(stream.execution_requirement) /
                                          static_cast<double>(stream.min_interarrival);
            }
        }

        std::vector<comparisons::ComparisonJob> accepted_jobs = workload.periodic_jobs;
        std::unordered_map<comparisons::TaskID, comparisons::SlotIndex> last_release;
        for (const auto& request : workload.dynamic_arrivals) {
            const bool compliant = tmin_compliant(request, last_release);
            const bool protected_stream = request.type == comparisons::TrafficType::Sporadic &&
                admitted_stream_ids.count(request.task_id) != 0U;
            const comparisons::NeutralBaselineSchedule before = scheduler->snapshot();
            comparisons::ComparisonDecision decision;
            if (preparation_success) {
                const auto start = Clock::now();
                decision = scheduler->on_dynamic_arrival(request, request.release);
                const auto stop = Clock::now();
                metrics.latencies.push_back(static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()
                ));
            }
            update_common_decision_metrics(metrics, request, decision.accepted, protected_stream, compliant);
            metrics.jobs_moved += static_cast<uint64_t>(decision.jobs_moved);
            metrics.slots_changed += static_cast<uint64_t>(decision.slots_changed);
            metrics.search_states += decision.states_examined;
            if (decision.accepted) {
                accepted_jobs.push_back(request);
                if (algorithm == "StaticDirect") ++metrics.direct_accepts;
                ++metrics.k_accepts[0];
            }

            const comparisons::NeutralBaselineSchedule after = scheduler->snapshot();
            const std::string pre_hash = neutral_schedule_hash(before);
            const std::string post_hash = neutral_schedule_hash(after);
            if (!decision.accepted && pre_hash != post_hash) {
                ++metrics.hash_errors;
                ++metrics.rollback_errors;
            }
            for (comparisons::SlotIndex slot = 0; slot < request.release; ++slot) {
                if (before.slots[static_cast<size_t>(slot)].job_id !=
                    after.slots[static_cast<size_t>(slot)].job_id) {
                    ++metrics.past_violations;
                    break;
                }
            }
            const comparisons::ValidationResult validation = comparisons::NeutralValidator::validate_schedule(
                after,
                accepted_jobs,
                request.release
            );
            if (!validation.valid) ++metrics.dependency_violations;
        }

        const comparisons::NeutralBaselineSchedule final_schedule = scheduler->snapshot();
        final_schedule_hash = neutral_schedule_hash(final_schedule);
        if (preparation_success) {
            for (const auto& periodic_job : workload.periodic_jobs) {
                const comparisons::SlotIndex start = final_schedule.get_job_start(periodic_job.id);
                if (start < periodic_job.release ||
                    start + periodic_job.execution_requirement > periodic_job.absolute_deadline) {
                    ++metrics.periodic_misses;
                }
            }
        }
    }

    const uint64_t dynamic_arrivals = metrics.sporadic_arrivals + metrics.oneshot_arrivals;
    const uint64_t dynamic_accepted = metrics.sporadic_accepted + metrics.oneshot_accepted;
    const uint64_t dynamic_rejected = metrics.sporadic_rejected + metrics.oneshot_rejected;
    const uint64_t mechanism_accepts = metrics.direct_accepts + metrics.reclaim_accepts + metrics.compensation_accepts;
    const double mean_k = mechanism_accepts == 0U
        ? 0.0
        : static_cast<double>(metrics.actual_k_sum) / static_cast<double>(mechanism_accepts);
    const double mean_latency = metrics.latencies.empty()
        ? 0.0
        : static_cast<double>(std::accumulate(metrics.latencies.begin(), metrics.latencies.end(), UINT64_C(0))) /
          static_cast<double>(metrics.latencies.size());
    const double actual_tt_util = static_cast<double>(occupied_tt_slots) / static_cast<double>(config.horizon);
    const double actual_oneshot_util = static_cast<double>(offered_oneshot_slots) / static_cast<double>(config.horizon);
    const uint64_t safety_failures = metrics.periodic_misses + metrics.protected_rejects +
        metrics.protected_deadline_misses + metrics.dependency_violations + metrics.k_violations +
        metrics.past_violations + metrics.hash_errors + metrics.rollback_errors;
    const std::string status = !preparation_success ? "PREPARATION_INFEASIBLE" :
        (safety_failures == 0U ? "PASSED" : "FAILED");

    std::vector<std::string> row = {
        run_id, experiment, scenario_id, algorithm,
        algorithm == "AffineEnvelope" ? "CoDesign" : "CommonCommunication",
        std::to_string(taskset_id), std::to_string(trace_id), std::to_string(master_seed),
        std::to_string(experiment_seed), std::to_string(scenario_seed), std::to_string(taskset_seed),
        std::to_string(trace_seed), scheduler_commit, evaluation_commit, "0.1",
        std::to_string(config.horizon), "1000.0", std::to_string(config.horizon), "0", "1",
        std::to_string(config.periodic_stream_count + config.candidate_sporadic_stream_count),
        std::to_string(config.periodic_stream_count), std::to_string(config.candidate_sporadic_stream_count),
        as_string(config.periodic_load), as_string(actual_tt_util), as_string(candidate_sporadic_util),
        as_string(admitted_sporadic_util), as_string(config.oneshot_ratio), as_string(actual_oneshot_util),
        profile_name, std::to_string(configured_k), rtc_enabled ? "ON" : "OFF",
        reclamation_enabled ? "ON" : "OFF", compensation_enabled ? "ON" : "OFF",
        dependency_level, std::to_string(count_dependency_edges(taskset.dependencies)), config.multislot_regime,
        std::to_string(config.max_duration), as_string(config.sporadic_deadline_ratio),
        as_string(config.oneshot_deadline_ratio), burst_mode, taskset_hash, trace_hash, input_hash,
        baseline_hash, final_schedule_hash, std::to_string(taskset.periodic_jobs.size()),
        std::to_string(metrics.periodic_misses), std::to_string(taskset.sporadic_streams.size()),
        std::to_string(offline_admitted), std::to_string(offline_rejected),
        std::to_string(metrics.sporadic_arrivals), std::to_string(metrics.compliant_sporadic_arrivals),
        std::to_string(metrics.tmin_violations), std::to_string(metrics.sporadic_accepted),
        std::to_string(metrics.sporadic_rejected), std::to_string(metrics.protected_rejects),
        std::to_string(metrics.protected_deadline_misses), std::to_string(metrics.oneshot_arrivals),
        std::to_string(metrics.oneshot_accepted), std::to_string(metrics.oneshot_rejected),
        std::to_string(dynamic_arrivals), std::to_string(dynamic_accepted), std::to_string(dynamic_rejected),
        std::to_string(metrics.accepted_slots), std::to_string(metrics.direct_accepts),
        std::to_string(metrics.reclaim_accepts), std::to_string(metrics.compensation_accepts),
        std::to_string(metrics.actual_k_sum), as_string(mean_k), std::to_string(metrics.max_actual_k),
        std::to_string(metrics.k_accepts[0]), std::to_string(metrics.k_accepts[1]),
        std::to_string(metrics.k_accepts[2]), std::to_string(metrics.k_accepts[3]),
        std::to_string(metrics.k_accepts[4]), std::to_string(metrics.k_accepts[5]),
        std::to_string(metrics.delta_max), std::to_string(metrics.delta_total),
        std::to_string(metrics.jobs_moved), std::to_string(metrics.slots_changed),
        std::to_string(metrics.candidates_generated), std::to_string(metrics.candidates_feasible),
        std::to_string(metrics.rtc_safe_candidates), std::to_string(metrics.rtc_checks),
        std::to_string(metrics.rtc_unsafe), std::to_string(metrics.rtc_induced_rejections),
        std::to_string(metrics.search_states), std::to_string(metrics.search_pruned),
        std::to_string(metrics.max_search_depth), as_string(mean_latency),
        as_string(quantile(metrics.latencies, 0.50)), as_string(quantile(metrics.latencies, 0.95)),
        as_string(quantile(metrics.latencies, 0.99)), as_string(quantile(metrics.latencies, 1.00)),
        std::to_string(metrics.dependency_violations), std::to_string(metrics.k_violations),
        std::to_string(metrics.past_violations), std::to_string(metrics.hash_errors),
        std::to_string(metrics.rollback_errors), preparation_success ? "1" : "0",
        preparation_message, status
    };

    if (row.size() != kColumns.size()) {
        std::cerr << "Internal schema error: expected " << kColumns.size() << " columns, generated "
                  << row.size() << '\n';
        return 4;
    }
    for (size_t index = 0; index < row.size(); ++index) {
        if (index != 0) std::cout << ',';
        std::cout << csv_escape(row[index]);
    }
    std::cout << '\n';
    return 0;
}
