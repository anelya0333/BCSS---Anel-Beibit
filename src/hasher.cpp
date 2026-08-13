#include "bcss/hasher.hpp"
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace bcss {

std::vector<uint8_t> ScheduleHasher::compute_hash_bytes(const Schedule& schedule) {
    std::vector<uint8_t> canonical_bytes = schedule.serialize_canonical();
    std::vector<uint8_t> hash_out(32, 0);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create OpenSSL EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize OpenSSL SHA-256 digest");
    }

    if (EVP_DigestUpdate(ctx, canonical_bytes.data(), canonical_bytes.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update OpenSSL SHA-256 digest");
    }

    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx, hash_out.data(), &len) != 1 || len != 32) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize OpenSSL SHA-256 digest");
    }

    EVP_MD_CTX_free(ctx);
    return hash_out;
}

std::string ScheduleHasher::bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t b : bytes) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

std::string ScheduleHasher::compute_hash(const Schedule& schedule) {
    std::vector<uint8_t> raw_bytes = compute_hash_bytes(schedule);
    return bytes_to_hex(raw_bytes);
}

} // namespace bcss
