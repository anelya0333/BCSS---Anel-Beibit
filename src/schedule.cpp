#include "bcss/schedule.hpp"
#include <cstring>

namespace bcss {

static void append_uint64_be(std::vector<uint8_t>& buf, uint64_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void append_int64_be(std::vector<uint8_t>& buf, int64_t val) {
    uint64_t uval;
    std::memcpy(&uval, &val, sizeof(val));
    append_uint64_be(buf, uval);
}

std::vector<uint8_t> Schedule::serialize_canonical() const {
    std::vector<uint8_t> buf;
    buf.reserve(static_cast<size_t>(1 + 8 + horizon * (8 + 8 + 1 + 8 + 8 + 8)));

    // Version byte
    buf.push_back(0x01);

    // Horizon H
    append_int64_be(buf, horizon);

    // Slot-by-slot canonical layout
    for (SlotIndex s = 0; s < horizon; ++s) {
        const auto& slot = slots[static_cast<size_t>(s)];
        append_int64_be(buf, slot.slot_index);
        append_int64_be(buf, slot.job_id);
        buf.push_back(static_cast<uint8_t>(slot.type));
        append_int64_be(buf, slot.release);
        append_int64_be(buf, slot.deadline);
        append_int64_be(buf, slot.period);
    }

    return buf;
}

} // namespace bcss
