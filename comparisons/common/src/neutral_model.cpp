#include "comparisons/neutral_model.hpp"
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

namespace comparisons {

static void append_be_64(std::string& buf, int64_t val) {
    uint64_t uval = static_cast<uint64_t>(val);
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<char>((uval >> (i * 8)) & 0xFF));
    }
}

std::string compute_input_fingerprint(
    const NeutralWorkload& workload,
    const NeutralBaselineSchedule& schedule
) {
    std::string canonical;
    canonical.reserve(1024);

    canonical.push_back(0x03); // Version byte for complete neutral input fingerprint
    append_be_64(canonical, workload.horizon);

    // Periodic jobs
    append_be_64(canonical, static_cast<int64_t>(workload.periodic_jobs.size()));
    for (const auto& j : workload.periodic_jobs) {
        append_be_64(canonical, j.id);
        append_be_64(canonical, j.task_id);
        append_be_64(canonical, j.release);
        append_be_64(canonical, j.absolute_deadline);
        append_be_64(canonical, j.execution_requirement);
        append_be_64(canonical, j.period.value_or(-1));
        append_be_64(canonical, static_cast<int64_t>(j.predecessors.size()));
        for (JobID predecessor : j.predecessors) {
            append_be_64(canonical, predecessor);
        }
    }

    // Candidate sporadic stream contracts are part of the paired input.
    append_be_64(canonical, static_cast<int64_t>(workload.sporadic_streams.size()));
    for (const auto& stream : workload.sporadic_streams) {
        append_be_64(canonical, stream.stream_id);
        append_be_64(canonical, stream.min_interarrival);
        append_be_64(canonical, stream.execution_requirement);
        append_be_64(canonical, stream.relative_deadline);
    }

    // Dynamic arrivals
    append_be_64(canonical, static_cast<int64_t>(workload.dynamic_arrivals.size()));
    for (const auto& j : workload.dynamic_arrivals) {
        append_be_64(canonical, j.id);
        append_be_64(canonical, j.task_id);
        canonical.push_back(static_cast<char>(j.type));
        append_be_64(canonical, j.release);
        append_be_64(canonical, j.absolute_deadline);
        append_be_64(canonical, j.execution_requirement);
        append_be_64(canonical, j.min_interarrival.value_or(-1));
        append_be_64(canonical, static_cast<int64_t>(j.predecessors.size()));
        for (JobID predecessor : j.predecessors) {
            append_be_64(canonical, predecessor);
        }
    }

    // Schedule slots
    append_be_64(canonical, schedule.horizon);
    for (SlotIndex s = 0; s < schedule.horizon; ++s) {
        const auto& slot = schedule.slots[static_cast<size_t>(s)];
        append_be_64(canonical, slot.slot_index);
        append_be_64(canonical, slot.job_id);
        canonical.push_back(static_cast<char>(slot.type));
        append_be_64(canonical, slot.release);
        append_be_64(canonical, slot.deadline);
    }

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(mdctx, canonical.data(), canonical.size());
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    std::ostringstream ss;
    for (unsigned int i = 0; i < md_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md_value[i]);
    }
    return ss.str();
}

} // namespace comparisons
