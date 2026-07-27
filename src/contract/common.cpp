#include "engine_sim_offline/contract/common.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <numeric>
#include <utility>

namespace engine_sim_offline::contract {

bool Sha256Digest::is_zero() const noexcept {
    return std::ranges::all_of(bytes, [](std::uint8_t byte) { return byte == 0; });
}

Sha256Digest sha256(std::span<const std::byte> payload) noexcept {
    // Project-owned direct implementation of FIPS PUB 180-4 SHA-256. The focused
    // known-answer tests pin the externally standardized digest behavior.
    constexpr std::array<std::uint32_t, 64> round_constants{
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
    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    const auto process_block = [&](const std::byte *block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const auto offset = index * 4;
            words[index] = (std::to_integer<std::uint32_t>(block[offset]) << 24U) |
                           (std::to_integer<std::uint32_t>(block[offset + 1]) << 16U) |
                           (std::to_integer<std::uint32_t>(block[offset + 2]) << 8U) |
                           std::to_integer<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const auto s0 = std::rotr(words[index - 15], 7) ^
                            std::rotr(words[index - 15], 18) ^
                            (words[index - 15] >> 3U);
            const auto s1 = std::rotr(words[index - 2], 17) ^
                            std::rotr(words[index - 2], 19) ^ (words[index - 2] >> 10U);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum_1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary_1 =
                h + sum_1 + choice + round_constants[index] + words[index];
            const auto sum_0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary_2 = sum_0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary_1;
            d = c;
            c = b;
            b = a;
            a = temporary_1 + temporary_2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    };

    std::size_t offset = 0;
    while (payload.size() - offset >= 64) {
        process_block(payload.data() + offset);
        offset += 64;
    }

    std::array<std::byte, 128> tail{};
    const auto remainder = payload.size() - offset;
    if (remainder > 0) {
        std::copy_n(payload.data() + offset, remainder, tail.data());
    }
    tail[remainder] = std::byte{0x80};
    const auto padded_size = remainder < 56 ? std::size_t{64} : std::size_t{128};
    const auto bit_length = static_cast<std::uint64_t>(payload.size()) * UINT64_C(8);
    for (std::size_t index = 0; index < 8; ++index) {
        tail[padded_size - 1 - index] =
            static_cast<std::byte>(bit_length >> (index * 8U));
    }
    process_block(tail.data());
    if (padded_size == 128) {
        process_block(tail.data() + 64);
    }

    Sha256Digest digest;
    for (std::size_t index = 0; index < state.size(); ++index) {
        digest.bytes[index * 4] = static_cast<std::uint8_t>(state[index] >> 24U);
        digest.bytes[index * 4 + 1] = static_cast<std::uint8_t>(state[index] >> 16U);
        digest.bytes[index * 4 + 2] = static_cast<std::uint8_t>(state[index] >> 8U);
        digest.bytes[index * 4 + 3] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

void ValidationReport::add(ContractIssueCode code, std::string path,
                           std::string message) {
    issues.push_back({code, std::move(path), std::move(message)});
}

void ValidationReport::append(ValidationReport other) {
    issues.insert(issues.end(), std::make_move_iterator(other.issues.begin()),
                  std::make_move_iterator(other.issues.end()));
}

bool is_valid_semantic_id(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }

    const auto ascii_lower = [](char character) {
        return character >= 'a' && character <= 'z';
    };
    const auto ascii_digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    if (!(ascii_lower(value.front()) || ascii_digit(value.front()))) {
        return false;
    }

    return std::ranges::all_of(value, [&](char character) {
        return ascii_lower(character) || ascii_digit(character) || character == '.' ||
               character == '_' || character == '-' || character == '/';
    });
}

ValidationReport validate(const RationalRateHz &rate) {
    ValidationReport report;
    if (rate.numerator == 0) {
        report.add(ContractIssueCode::invalid_value, "numerator",
                   "rate numerator must be positive");
    }
    if (rate.denominator == 0) {
        report.add(ContractIssueCode::invalid_value, "denominator",
                   "rate denominator must be positive");
    }
    if (rate.numerator != 0 && rate.denominator != 0 &&
        std::gcd(rate.numerator, rate.denominator) != 1) {
        report.add(ContractIssueCode::invalid_value, "",
                   "rate must be stored as a reduced rational");
    }
    return report;
}

ValidationReport validate(const RenderRates &rates) {
    ValidationReport report;
    const auto append = [&report](const RationalRateHz &rate, std::string_view path) {
        auto nested = validate(rate);
        for (auto &issue : nested.issues) {
            issue.path = issue.path.empty() ? std::string(path)
                                            : std::string(path) + "." + issue.path;
            report.issues.push_back(std::move(issue));
        }
    };
    append(rates.physics, "physics");
    append(rates.capture, "capture");
    append(rates.source_processing, "source_processing");
    append(rates.acoustic, "acoustic");
    append(rates.delivery, "delivery");
    return report;
}

ValidationReport validate(const MethodIdentity &method) {
    ValidationReport report;
    if (!is_valid_semantic_id(method.id)) {
        report.add(ContractIssueCode::invalid_value, "id",
                   "method ID must be a canonical semantic ID");
    }
    if (method.version == 0) {
        report.add(ContractIssueCode::invalid_value, "version",
                   "method version must be positive");
    }
    if (method.configuration_sha256.is_zero()) {
        report.add(ContractIssueCode::invalid_value, "configuration_sha256",
                   "method configuration digest must be nonzero");
    }
    return report;
}

} // namespace engine_sim_offline::contract
