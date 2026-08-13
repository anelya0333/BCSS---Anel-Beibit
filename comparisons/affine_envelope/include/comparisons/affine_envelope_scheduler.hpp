#ifndef COMPARISONS_AFFINE_ENVELOPE_SCHEDULER_HPP
#define COMPARISONS_AFFINE_ENVELOPE_SCHEDULER_HPP

#include "comparisons/interface.hpp"
#include <vector>
#include <unordered_map>

namespace comparisons {

struct AffineParameters {
    double sigma{0.0}; // Burst capability
    double rho{0.0};   // Steady-state rate
};

class AffineEnvelopeScheduler : public IComparisonScheduler {
public:
    AffineEnvelopeScheduler() = default;

    std::string name() const override { return "AffineEnvelope_CoDesign"; }

    PreparationResult prepare(
        const NeutralBaselineSchedule& baseline,
        const NeutralWorkload& workload
    ) override;

    ComparisonDecision on_dynamic_arrival(
        const ComparisonJob& request,
        SlotIndex current_time
    ) override;

    void advance_to(SlotIndex time) override;

    NeutralBaselineSchedule snapshot() const override { return synthesized_schedule_; }

    SchedulerMetrics metrics() const override { return metrics_; }

    // Fingerprint of synthesized schedule (Requirement 74)
    std::string generated_schedule_fingerprint() const { return generated_schedule_fingerprint_; }

    // Parameter inspection
    AffineParameters get_affine_parameters() const { return affine_params_; }

private:
    void calculate_arrival_curve_and_affine_envelope();
    bool synthesize_tt_schedule_modified_llf();

    NeutralWorkload workload_;
    NeutralBaselineSchedule synthesized_schedule_;
    SlotIndex current_time_{0};
    SchedulerMetrics metrics_;
    bool prepared_success_{false};

    AffineParameters affine_params_{0.0, 0.0};
    std::string generated_schedule_fingerprint_;
    std::unordered_map<JobID, ComparisonJob> active_jobs_;
};

} // namespace comparisons

#endif // COMPARISONS_AFFINE_ENVELOPE_SCHEDULER_HPP
