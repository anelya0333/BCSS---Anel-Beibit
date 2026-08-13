#ifndef BCSS_HASHER_HPP
#define BCSS_HASHER_HPP

#include "schedule.hpp"
#include <string>
#include <vector>

namespace bcss {

class ScheduleHasher {
public:
    // Computes deterministic SHA-256 hash of canonical schedule serialization using OpenSSL EVP
    static std::string compute_hash(const Schedule& schedule);
    static std::vector<uint8_t> compute_hash_bytes(const Schedule& schedule);

    // Formats raw 32-byte hash as 64-character lowercase hex string
    static std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
};

} // namespace bcss

#endif // BCSS_HASHER_HPP
