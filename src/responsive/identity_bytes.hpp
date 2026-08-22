#pragma once

#include "crankwave/contract/common.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace crankwave::responsive::detail {

class CanonicalIdentityBytes final {
  public:
    explicit CanonicalIdentityBytes(const std::string_view grammar_id) {
        bytes_.reserve(512U);
        string(grammar_id);
    }

    void boolean(const bool value) {
        bytes_.push_back(value ? std::byte{1U} : std::byte{0U});
    }

    void u32(const std::uint32_t value) {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void u64(const std::uint64_t value) {
        for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
            bytes_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
        }
    }

    void f32(const float value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }

    void f64(const double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void string(const std::string_view value) {
        u64(value.size());
        const auto payload =
            std::as_bytes(std::span<const char>{value.data(), value.size()});
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    void digest(const contract::Sha256Digest &value) {
        for (const auto byte : value.bytes) {
            bytes_.push_back(static_cast<std::byte>(byte));
        }
    }

    [[nodiscard]] contract::Sha256Digest finish() const {
        return contract::sha256(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
};

} // namespace crankwave::responsive::detail
