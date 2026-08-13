#include "bcss/variants.hpp"

namespace bcss {

BcssResult BcssVariantRunner::run_variant(
    BcssScheduler& scheduler,
    BcssVariant variant,
    const Job& new_job,
    SlotIndex t_now
) {
    SlotCount original_K = scheduler.max_K;
    switch (variant) {
        case BcssVariant::Bcss0:
            scheduler.max_K = 0;
            break;
        case BcssVariant::Bcss1:
            scheduler.max_K = 1;
            break;
        case BcssVariant::BcssFull:
            // Uses configured K
            break;
    }

    BcssResult res = scheduler.admit_dynamic_job(new_job, t_now);
    scheduler.max_K = original_K;
    return res;
}

} // namespace bcss
