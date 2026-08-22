#include "crankwave/contract/provenance.hpp"

#include "sha256_stream.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace crankwave::contract {
namespace {

class DigestWriter {
  public:
    void byte(std::uint8_t value) noexcept {
        const std::byte encoded{value};
        stream_.update(std::span<const std::byte>{&encoded, 1});
    }

    void u32(std::uint32_t value) noexcept {
        std::array<std::byte, 4> encoded{};
        for (std::uint32_t index = 0; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        stream_.update(encoded);
    }

    void u64(std::uint64_t value) noexcept {
        std::array<std::byte, 8> encoded{};
        for (std::uint32_t index = 0; index < encoded.size(); ++index) {
            encoded[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        stream_.update(encoded);
    }

    void string(std::string_view value) noexcept {
        u64(static_cast<std::uint64_t>(value.size()));
        stream_.update(
            std::as_bytes(std::span<const char>{value.data(), value.size()}));
    }

    void digest(const Sha256Digest &value) noexcept {
        stream_.update(std::as_bytes(
            std::span<const std::uint8_t>{value.bytes.data(), value.bytes.size()}));
    }

    template <class T, class Write>
    void optional(const std::optional<T> &value, Write write) noexcept {
        byte(value.has_value() ? 1U : 0U);
        if (value.has_value()) {
            write(*value);
        }
    }

    [[nodiscard]] Sha256Digest finish() const noexcept {
        return stream_.finish();
    }

  private:
    detail::Sha256Stream stream_;
};

void write_method(DigestWriter &writer, const MethodIdentity &method) noexcept {
    writer.string(method.id);
    writer.u32(method.version);
    writer.digest(method.configuration_sha256);
}

void write_uncertainty(DigestWriter &writer,
                       const UncertaintyStatement &uncertainty) noexcept {
    writer.optional<double>(
        uncertainty.standard_uncertainty, [&](double value) noexcept {
            // The contract treats both signed-zero representations as the same
            // uncertainty, so either serializes as positive zero.
            const auto bits =
                value == 0.0 ? UINT64_C(0) : std::bit_cast<std::uint64_t>(value);
            writer.u64(bits);
        });
    writer.string(uncertainty.method);
}

} // namespace

Sha256Digest canonical_provenance_ledger_digest(const ProvenanceLedger &ledger,
                                                std::string_view grammar_id) noexcept {
    DigestWriter writer;
    writer.string(grammar_id);
    writer.string(ledger.schema_id);
    writer.string(ledger.bundle.id);

    writer.u64(static_cast<std::uint64_t>(ledger.evidence.size()));
    for (const auto &evidence : ledger.evidence) {
        writer.string(evidence.id);
        writer.string(evidence.locator);
        writer.optional<std::string>(
            evidence.revision,
            [&](const std::string &revision) noexcept { writer.string(revision); });
        writer.optional<Sha256Digest>(
            evidence.content_sha256,
            [&](const Sha256Digest &digest) noexcept { writer.digest(digest); });
        writer.byte(static_cast<std::uint8_t>(evidence.rights));
    }

    writer.u64(static_cast<std::uint64_t>(ledger.claims.size()));
    for (const auto &claim : ledger.claims) {
        writer.string(claim.id);
        writer.byte(static_cast<std::uint8_t>(claim.origin));
        writer.u64(static_cast<std::uint64_t>(claim.citations.size()));
        for (const auto &citation : claim.citations) {
            writer.string(citation.evidence_id);
            writer.string(citation.claim_locator);
        }
        writer.optional<UncertaintyStatement>(
            claim.uncertainty, [&](const UncertaintyStatement &uncertainty) noexcept {
                write_uncertainty(writer, uncertainty);
            });
    }

    writer.u64(static_cast<std::uint64_t>(ledger.resolutions.size()));
    for (const auto &resolution : ledger.resolutions) {
        writer.string(resolution.id);
        writer.string(resolution.parameter_path);
        writer.byte(static_cast<std::uint8_t>(resolution.mode));
        writer.string(resolution.claim_id);
        writer.optional<MethodIdentity>(resolution.method,
                                        [&](const MethodIdentity &method) noexcept {
                                            write_method(writer, method);
                                        });
        writer.u64(
            static_cast<std::uint64_t>(resolution.dependency_parameter_paths.size()));
        for (const auto &dependency : resolution.dependency_parameter_paths) {
            writer.string(dependency);
        }
    }
    return writer.finish();
}

} // namespace crankwave::contract
