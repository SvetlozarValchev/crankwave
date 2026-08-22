#pragma once

#include "crankwave/contract/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace crankwave::contract::detail {

// Project-owned bounded incremental SHA-256 used by subsystems that cannot retain a
// complete payload in memory. The public one-shot helper remains the API boundary.
class Sha256Stream {
  public:
    void update(std::span<const std::byte> payload) noexcept;
    [[nodiscard]] Sha256Digest finish() const noexcept;

  private:
    void transform(const std::array<std::byte, 64> &block) noexcept;

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::byte, 64> block_{};
    std::size_t block_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

} // namespace crankwave::contract::detail
