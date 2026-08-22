#include "dsp/pcg32.hpp"

#include <stdexcept>

namespace crankwave::dsp {
namespace {

constexpr std::uint64_t kPcg32Multiplier = UINT64_C(6364136223846793005);

} // namespace

Pcg32::Pcg32(std::uint64_t initial_state, std::uint64_t stream) {
    if (stream > kMaximumPcg32Stream) {
        throw std::invalid_argument{
            "PCG32 stream must fit before odd-increment encoding"};
    }

    state_ = 0;
    increment_ = (stream << 1U) | UINT64_C(1);
    static_cast<void>(next_u32());
    state_ = state_ + initial_state;
    static_cast<void>(next_u32());
}

std::uint32_t Pcg32::next_u32() noexcept {
    const std::uint64_t old_state = state_;
    state_ = old_state * kPcg32Multiplier + increment_;
    const auto xorshifted =
        static_cast<std::uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
    const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
    const auto inverse_rotation =
        static_cast<std::uint32_t>((UINT32_C(0) - rotation) & UINT32_C(31));
    return static_cast<std::uint32_t>((xorshifted >> rotation) |
                                      (xorshifted << inverse_rotation));
}

double Pcg32::uniform_double() noexcept {
    const auto high = static_cast<std::uint64_t>(next_u32() >> 5U);
    const auto low = static_cast<std::uint64_t>(next_u32() >> 6U);
    return static_cast<double>((high << 26U) | low) * 0x1.0p-53;
}

double Pcg32::uniform_signed_double() noexcept {
    return 2.0 * uniform_double() - 1.0;
}

std::uint64_t Pcg32::state() const noexcept {
    return state_;
}

std::uint64_t Pcg32::increment() const noexcept {
    return increment_;
}

} // namespace crankwave::dsp
