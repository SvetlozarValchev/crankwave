#include "identity/simulation_request_identity_writer.hpp"

namespace engine_sim_offline::identity::detail {

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

bool write_provenance_bundle_ref(CanonicalJsonWriter &writer,
                                 const contract::ProvenanceBundleRef &provenance) {
    return writer.begin_object() && writer.key("id") &&
           writer.string_value(provenance.id) && writer.key("sha256") &&
           writer.sha256_value(provenance.sha256) && writer.end_object();
}

} // namespace engine_sim_offline::identity::detail
