#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace engine_sim_offline::artifacts::detail {

// Bounded incremental counterpart to the public one-shot SHA-256 helper. Promote this
// to contract/common if another subsystem needs streaming hashes; do not fork it.
class Sha256Stream {
  public:
    void update(std::span<const std::byte> payload) noexcept;
    [[nodiscard]] contract::Sha256Digest finish() const noexcept;

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

} // namespace engine_sim_offline::artifacts::detail
