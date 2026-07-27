#pragma once

#include <cstdint>

namespace engine_sim_offline::dsp {

// Exact P1.8 PCG32 state machine and binary64 draw construction. The constructor
// arguments are the recorded inputs to the seeding procedure, not an already-seeded
// internal state and increment.
class P18Pcg32 {
  public:
    P18Pcg32(std::uint64_t initial_state, std::uint64_t stream);

    [[nodiscard]] std::uint32_t next_u32() noexcept;
    [[nodiscard]] double uniform_double() noexcept;
    [[nodiscard]] double uniform_signed_double() noexcept;

    [[nodiscard]] std::uint64_t state() const noexcept;
    [[nodiscard]] std::uint64_t increment() const noexcept;

  private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 0;
};

} // namespace engine_sim_offline::dsp
