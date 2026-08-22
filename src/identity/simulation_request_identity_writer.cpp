#include "identity/simulation_request_identity_writer.hpp"

#include <optional>
#include <string_view>

namespace crankwave::identity::detail {
namespace {

[[nodiscard]] std::string_view
random_component_kind(contract::RandomComponentKind kind) noexcept {
    switch (kind) {
    case contract::RandomComponentKind::combustion:
        return "combustion";
    case contract::RandomComponentKind::presentation_jitter:
        return "presentation_jitter";
    case contract::RandomComponentKind::presentation_air_noise:
        return "presentation_air_noise";
    case contract::RandomComponentKind::starter:
        return "starter";
    case contract::RandomComponentKind::unspecified:
        break;
    }
    return {};
}

template <class Id>
[[nodiscard]] bool write_optional_id(CanonicalJsonWriter &writer,
                                     const std::optional<Id> &id) {
    return id.has_value() ? writer.uint32_value(id->value) : writer.null_value();
}

[[nodiscard]] bool write_component_seed(CanonicalJsonWriter &writer,
                                        const contract::ComponentSeed &seed) {
    const auto kind = random_component_kind(seed.kind);
    if (kind.empty()) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "random component kind is not encodable");
    }
    return writer.begin_object() && writer.key("kind") && writer.string_value(kind) &&
           writer.key("cylinder_id") && write_optional_id(writer, seed.cylinder_id) &&
           writer.key("route_id") && write_optional_id(writer, seed.route_id) &&
           writer.key("initial_state") && writer.uint64_hex_value(seed.initial_state) &&
           writer.key("stream") && writer.uint64_hex_value(seed.stream) &&
           writer.end_object();
}

} // namespace

bool write_rational_rate(CanonicalJsonWriter &writer,
                         const contract::RationalRateHz &rate) {
    return writer.begin_object() && writer.key("numerator") &&
           writer.uint64_hex_value(rate.numerator) && writer.key("denominator") &&
           writer.uint64_hex_value(rate.denominator) && writer.end_object();
}

bool write_render_rates(CanonicalJsonWriter &writer,
                        const contract::RenderRates &rates) {
    return writer.begin_object() && writer.key("physics") &&
           write_rational_rate(writer, rates.physics) && writer.key("capture") &&
           write_rational_rate(writer, rates.capture) &&
           writer.key("source_processing") &&
           write_rational_rate(writer, rates.source_processing) &&
           writer.key("acoustic") && write_rational_rate(writer, rates.acoustic) &&
           writer.key("delivery") && write_rational_rate(writer, rates.delivery) &&
           writer.end_object();
}

bool write_method_identity(CanonicalJsonWriter &writer,
                           const contract::MethodIdentity &method) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(method.id) && writer.key("version") &&
           writer.uint32_value(method.version) && writer.key("configuration_sha256") &&
           writer.sha256_value(method.configuration_sha256) && writer.end_object();
}

bool write_random_plan(CanonicalJsonWriter &writer,
                       const contract::RandomPlan &random_plan) {
    if (!(writer.begin_object() && writer.key("generator") &&
          write_method_identity(writer, random_plan.generator) &&
          writer.key("public_seed") &&
          writer.uint64_hex_value(random_plan.public_seed) &&
          writer.key("derivation") &&
          write_method_identity(writer, random_plan.derivation) &&
          writer.key("component_seeds") && writer.begin_array())) {
        return false;
    }
    for (const auto &seed : random_plan.component_seeds) {
        if (!write_component_seed(writer, seed)) {
            return false;
        }
    }
    return writer.end_array() && writer.end_object();
}

bool write_provenance_bundle_ref(CanonicalJsonWriter &writer,
                                 const contract::ProvenanceBundleRef &provenance) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(provenance.id) && writer.key("sha256") &&
           writer.sha256_value(provenance.sha256) && writer.end_object();
}

} // namespace crankwave::identity::detail
