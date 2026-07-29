#include "manifest_encoder_impl.hpp"

namespace engine_sim_offline::artifacts::detail {
namespace {

bool write_presentation_methods(CanonicalJsonWriter &writer,
                                const contract::PresentationMethods &methods) {
    const auto write_method = [](CanonicalJsonWriter &output,
                                 const contract::MethodIdentity &method) {
        return write_method_identity(output, method);
    };
    return writer.begin_object() &&
           writer.key("calibrated_pressure_publication") &&
           write_resolved(writer, methods.calibrated_pressure_publication,
                          write_method) &&
           writer.key("coherent_two_outlet_audition") &&
           write_resolved(writer, methods.coherent_two_outlet_audition,
                          write_method) &&
           writer.end_object();
}

bool write_monitoring(CanonicalJsonWriter &writer,
                      const contract::PresentationMonitoring &monitoring) {
    const auto write_f64 = [](CanonicalJsonWriter &output, double value) {
        return output.binary64_bits_value(value);
    };
    return writer.begin_object() && writer.key("gain_linear") &&
           write_resolved(writer, monitoring.gain_linear, write_f64) &&
           writer.key("fade_in_duration_s") &&
           write_resolved(writer, monitoring.fade_in_duration_s, write_f64) &&
           writer.key("fade_out_duration_s") &&
           write_resolved(writer, monitoring.fade_out_duration_s, write_f64) &&
           writer.end_object();
}

} // namespace

bool write_presentation_calibration(
    CanonicalJsonWriter &writer,
    const contract::PresentationCalibration &presentation) {
    if (presentation.schema_version != 1U) {
        return writer.fail(CanonicalJsonWriter::Error::unsupported_value,
                           "physical-pressure presentation schema version is not v1");
    }
    const auto write_string = [](CanonicalJsonWriter &output,
                                 const std::string &value) {
        return output.string_value(value);
    };
    return writer.begin_object() && writer.key("schema_version") &&
           writer.uint32_value(presentation.schema_version) &&
           writer.key("calibration_id") &&
           writer.string_value(presentation.calibration_id) &&
           writer.key("engine_profile_id") &&
           write_resolved(writer, presentation.engine_profile_id, write_string) &&
           writer.key("methods") &&
           write_presentation_methods(writer, presentation.methods) &&
           writer.key("monitoring") &&
           write_monitoring(writer, presentation.monitoring) &&
           writer.key("provenance_schema_id") &&
           writer.string_value(presentation.provenance_schema_id) &&
           writer.end_object();
}

} // namespace engine_sim_offline::artifacts::detail
