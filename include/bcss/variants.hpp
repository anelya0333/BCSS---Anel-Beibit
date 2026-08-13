#ifndef BCSS_VARIANTS_HPP
#define BCSS_VARIANTS_HPP

#include "scheduler.hpp"

namespace bcss {

enum class BcssVariant {
    Bcss0,     // Direct allocation & reclamation only (K=0)
    Bcss1,     // Direct, reclamation, and 1-hop compensation (K=1)
    BcssFull   // Complete multi-hop K-bounded BCSS (configurable K)
};

class BcssVariantRunner {
public:
    static BcssResult run_variant(
        BcssScheduler& scheduler,
        BcssVariant variant,
        const Job& new_job,
        SlotIndex t_now
    );
};

} // namespace bcss

#endif // BCSS_VARIANTS_HPP
