#include "sha256_stream.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace crankwave::contract::detail {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned count) noexcept {
    return (value >> count) | (value << (32U - count));
}

} // namespace

void Sha256Stream::update(std::span<const std::byte> payload) noexcept {
    total_bytes_ += static_cast<std::uint64_t>(payload.size());
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const auto count =
            std::min(block_.size() - block_size_, payload.size() - offset);
        std::memcpy(block_.data() + block_size_, payload.data() + offset, count);
        block_size_ += count;
        offset += count;
        if (block_size_ == block_.size()) {
            transform(block_);
            block_size_ = 0;
        }
    }
}

Sha256Digest Sha256Stream::finish() const noexcept {
    auto copy = *this;
    const auto bit_length = copy.total_bytes_ * UINT64_C(8);

    copy.block_[copy.block_size_++] = std::byte{0x80};
    if (copy.block_size_ > 56) {
        std::fill(copy.block_.begin() + static_cast<std::ptrdiff_t>(copy.block_size_),
                  copy.block_.end(), std::byte{0});
        copy.transform(copy.block_);
        copy.block_size_ = 0;
    }
    std::fill(copy.block_.begin() + static_cast<std::ptrdiff_t>(copy.block_size_),
              copy.block_.begin() + 56, std::byte{0});
    for (std::size_t index = 0; index < 8; ++index) {
        copy.block_[63 - index] = static_cast<std::byte>(bit_length >> (index * 8U));
    }
    copy.transform(copy.block_);

    Sha256Digest digest;
    for (std::size_t index = 0; index < copy.state_.size(); ++index) {
        digest.bytes[index * 4] = static_cast<std::uint8_t>(copy.state_[index] >> 24U);
        digest.bytes[index * 4 + 1] =
            static_cast<std::uint8_t>(copy.state_[index] >> 16U);
        digest.bytes[index * 4 + 2] =
            static_cast<std::uint8_t>(copy.state_[index] >> 8U);
        digest.bytes[index * 4 + 3] = static_cast<std::uint8_t>(copy.state_[index]);
    }
    return digest;
}

void Sha256Stream::transform(const std::array<std::byte, 64> &block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        words[index] =
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset]))
            << 24U;
        words[index] |=
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 1]))
            << 16U;
        words[index] |=
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(block[offset + 2]))
            << 8U;
        words[index] |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(block[offset + 3]));
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = rotate_right(words[index - 15], 7) ^
                        rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3U);
        const auto s1 = rotate_right(words[index - 2], 17) ^
                        rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto a = state_[0];
    auto b = state_[1];
    auto c = state_[2];
    auto d = state_[3];
    auto e = state_[4];
    auto f = state_[5];
    auto g = state_[6];
    auto h = state_[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sum1 =
            rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto first = h + sum1 + choose + kRoundConstants[index] + words[index];
        const auto sum0 =
            rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto second = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

} // namespace crankwave::contract::detail
