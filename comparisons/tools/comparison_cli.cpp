#include "comparisons/static_direct_scheduler.hpp"
#include "comparisons/slot_shifting_scheduler.hpp"
#include "comparisons/dtss_scheduler.hpp"
#include "comparisons/affine_envelope_scheduler.hpp"
#include "comparisons/workload_adapter.hpp"
#include "bcss/workload.hpp"

#include <iostream>
#include <memory>
#include <string>

using namespace comparisons;

int main(int argc, char** argv) {
    std::string algo = "static";
    uint64_t seed = 42;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--algorithm" && i + 1 < argc) {
            algo = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        }
    }

    std::cout << "======================================================================\n";
    std::cout << "BCSS COMPARISON ALGORITHMS CLI RUNNER\n";
    std::cout << "Algorithm: " << algo << " | Seed: " << seed << "\n";
    std::cout << "======================================================================\n";

    bcss::WorkloadConfig config{};
    config.horizon = 32;
    config.seed = seed;
    config.profile = bcss::WorkloadProfile::Normal;

    auto bcss_taskset = bcss::WorkloadGenerator::generate(config);

    NeutralWorkload workload = WorkloadAdapter::convert_taskset(bcss_taskset);
    NeutralBaselineSchedule baseline = WorkloadAdapter::convert_schedule(bcss_taskset.baseline_schedule);

    std::unique_ptr<IComparisonScheduler> scheduler;

    if (algo == "static") {
        scheduler = std::make_unique<StaticDirectScheduler>();
    } else if (algo == "slot-shifting" || algo == "slot-shifting-common") {
        scheduler = std::make_unique<SlotShiftingScheduler>(SlotShiftingMode::CommonCommunication);
    } else if (algo == "slot-shifting-native") {
        scheduler = std::make_unique<SlotShiftingScheduler>(SlotShiftingMode::PaperNative);
    } else if (algo == "dtss-static" || algo == "dtss") {
        scheduler = std::make_unique<DtssScheduler>(DtssMode::StaticGranularity);
    } else if (algo == "dtss-dynamic") {
        scheduler = std::make_unique<DtssScheduler>(DtssMode::DynamicGranularity);
    } else if (algo == "affine") {
        scheduler = std::make_unique<AffineEnvelopeScheduler>();
    } else {
        std::cerr << "Unknown algorithm: " << algo << "\n";
        return 1;
    }

    auto prep = scheduler->prepare(baseline, workload);
    std::cout << "Preparation: " << prep.message << " (Admitted streams: " << prep.admitted_sporadic_streams << ")\n";

    size_t accepted = 0;
    size_t rejected = 0;

    for (const auto& req : workload.dynamic_arrivals) {
        auto decision = scheduler->on_dynamic_arrival(req, req.release);
        if (decision.accepted) accepted++;
        else rejected++;
    }

    auto metrics = scheduler->metrics();
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Results for " << scheduler->name() << ":\n";
    std::cout << "  Accepted: " << accepted << " / " << workload.dynamic_arrivals.size() << "\n";
    std::cout << "  Rejected: " << rejected << "\n";
    std::cout << "  Input Fingerprint: " << metrics.input_fingerprint << "\n";
    std::cout << "======================================================================\n";

    return 0;
}
