#include "bcss/scheduler.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace bcss;

struct CheckRow {
    std::string check;
    bool passed{false};
    bool success{false};
    std::string mechanism;
    SlotCount actual_k{0};
    uint64_t rtc_checks{0};
    uint64_t rtc_unsafe{0};
    std::string pre_hash;
    std::string post_hash;
    std::string details;
};

void print_row(const CheckRow& row) {
    std::cout << row.check << ',' << (row.passed ? "PASS" : "FAIL") << ','
              << (row.success ? 1 : 0) << ',' << row.mechanism << ',' << row.actual_k << ','
              << row.rtc_checks << ',' << row.rtc_unsafe << ',' << row.pre_hash << ','
              << row.post_hash << ',' << row.details << '\n';
}

CheckRow direct_check() {
    BcssScheduler scheduler(8, 0, false);
    Schedule baseline(8);
    scheduler.set_periodic_baseline({}, baseline);
    const BcssResult result = scheduler.admit_dynamic_job(Job(100, 1000, TaskType::OneShot, 0, 2, 1), 0);
    return {"direct", result.success && result.decision_mechanism == "ACCEPT_DIRECT" && result.actual_k == 0,
            result.success, result.decision_mechanism, result.actual_k, result.stats.rtc_checks,
            result.stats.rtc_unsafe, result.pre_schedule_hash, result.post_schedule_hash, "expected_direct_k0"};
}

CheckRow reclamation_check() {
    Schedule baseline(16);
    Job existing(1, 10, TaskType::Periodic, 0, 16, 1);
    baseline.assign_job(existing, 4);
    BcssScheduler scheduler(16, 1, false);
    scheduler.set_periodic_baseline({existing}, baseline);
    const BcssResult result = scheduler.admit_dynamic_job(Job(100, 1000, TaskType::OneShot, 4, 1, 1), 0);
    return {"reclamation", result.success && result.decision_mechanism == "ACCEPT_RECLAIM" && result.actual_k == 1,
            result.success, result.decision_mechanism, result.actual_k, result.stats.rtc_checks,
            result.stats.rtc_unsafe, result.pre_schedule_hash, result.post_schedule_hash, "expected_reclaim_k1"};
}

CheckRow one_hop_check() {
    Schedule baseline(8);
    Job existing(1, 10, TaskType::Periodic, 1, 7, 1);
    baseline.assign_job(existing, 1);
    BcssScheduler scheduler(8, 1, false);
    scheduler.set_periodic_baseline({existing}, baseline);
    const BcssResult result = scheduler.admit_dynamic_job(Job(100, 1000, TaskType::OneShot, 1, 1, 1), 0);
    return {"one_hop_compensation",
            result.success && result.decision_mechanism == "ACCEPT_COMPENSATION" && result.actual_k == 1,
            result.success, result.decision_mechanism, result.actual_k, result.stats.rtc_checks,
            result.stats.rtc_unsafe, result.pre_schedule_hash, result.post_schedule_hash, "expected_compensation_k1"};
}

CheckRow two_hop_check() {
    Schedule baseline(6);
    Job first(1, 10, TaskType::Periodic, 0, 2, 1);
    Job second(2, 11, TaskType::Periodic, 0, 6, 1);
    baseline.assign_job(first, 0);
    baseline.assign_job(second, 1);
    BcssScheduler scheduler(6, 2, false);
    scheduler.set_periodic_baseline({first, second}, baseline);
    const BcssResult result = scheduler.admit_dynamic_job(Job(100, 1000, TaskType::OneShot, 0, 1, 1), 0);
    return {"two_hop_compensation",
            result.success && result.decision_mechanism == "ACCEPT_COMPENSATION" && result.actual_k == 2,
            result.success, result.decision_mechanism, result.actual_k, result.stats.rtc_checks,
            result.stats.rtc_unsafe, result.pre_schedule_hash, result.post_schedule_hash, "expected_compensation_k2"};
}

CheckRow k_bound_check() {
    Schedule baseline(8);
    Job first(1, 10, TaskType::Periodic, 0, 2, 1);
    Job second(2, 11, TaskType::Periodic, 0, 3, 1);
    Job third(3, 12, TaskType::Periodic, 0, 8, 1);
    baseline.assign_job(first, 0);
    baseline.assign_job(second, 1);
    baseline.assign_job(third, 2);
    Job incoming(100, 1000, TaskType::OneShot, 0, 1, 1);

    BcssScheduler scheduler_k2(8, 2, false);
    scheduler_k2.set_periodic_baseline({first, second, third}, baseline);
    const BcssResult rejected = scheduler_k2.admit_dynamic_job(incoming, 0);

    BcssScheduler scheduler_k3(8, 3, false);
    scheduler_k3.set_periodic_baseline({first, second, third}, baseline);
    const BcssResult accepted = scheduler_k3.admit_dynamic_job(incoming, 0);
    const bool passed = !rejected.success && rejected.pre_schedule_hash == rejected.post_schedule_hash &&
                        accepted.success && accepted.actual_k == 3;
    return {"k_bound_rejection", passed, rejected.success, rejected.decision_mechanism,
            rejected.actual_k, rejected.stats.rtc_checks, rejected.stats.rtc_unsafe,
            rejected.pre_schedule_hash, rejected.post_schedule_hash, "K2_rejects_K3_accepts_k3"};
}

CheckRow rtc_check() {
    BcssScheduler scheduler(10, 1, true);
    Schedule baseline(10);
    scheduler.set_periodic_baseline({}, baseline);
    scheduler.admit_sporadic_stream_offline({1, 5, 2, 5});
    const BcssResult result = scheduler.admit_dynamic_job(Job(100, 1000, TaskType::OneShot, 0, 10, 8), 0);
    const bool passed = !result.success && result.stats.rtc_checks > 0 && result.stats.rtc_unsafe > 0 &&
                        result.pre_schedule_hash == result.post_schedule_hash;
    return {"rtc_rejection", passed, result.success, result.decision_mechanism, result.actual_k,
            result.stats.rtc_checks, result.stats.rtc_unsafe, result.pre_schedule_hash,
            result.post_schedule_hash, "structurally_feasible_but_rtc_unsafe"};
}

CheckRow tmin_check() {
    BcssScheduler scheduler(20, 0, false);
    Schedule baseline(20);
    scheduler.set_periodic_baseline({}, baseline);
    Job first(7, 2000, TaskType::Sporadic, 0, 5, 1, -1, 5);
    Job violating(7, 2001, TaskType::Sporadic, 4, 5, 1, -1, 5);
    const BcssResult accepted = scheduler.admit_dynamic_job(first, 0);
    const BcssResult result = scheduler.admit_dynamic_job(violating, 4);
    const bool passed = accepted.success && !result.success &&
                        result.decision_mechanism == "SPORADIC_CONTRACT_VIOLATION" &&
                        result.pre_schedule_hash == result.post_schedule_hash;
    return {"tmin_contract_violation", passed, result.success, result.decision_mechanism,
            result.actual_k, result.stats.rtc_checks, result.stats.rtc_unsafe,
            result.pre_schedule_hash, result.post_schedule_hash, "delta4_lt_Tmin5"};
}

} // namespace

int main() {
    std::cout << "check,status,success,mechanism,actual_k,rtc_checks,rtc_unsafe,pre_hash,post_hash,details\n";
    const std::vector<CheckRow> rows = {
        direct_check(), reclamation_check(), one_hop_check(), two_hop_check(),
        k_bound_check(), rtc_check(), tmin_check()
    };
    bool passed = true;
    for (const auto& row : rows) {
        print_row(row);
        passed = passed && row.passed;
    }
    return passed ? 0 : 1;
}
