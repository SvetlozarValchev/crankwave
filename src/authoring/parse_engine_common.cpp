#include "authoring/parse_engine_detail.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::authoring::detail {
namespace {

bool parse_quantity_dimension(DocumentReader &reader, JsonValue value,
                              std::string_view path, QuantityDimension &output,
                              const std::optional<DiagnosticSubject> &subject_value) {
    return read_enum(reader, value, path,
                     {
                         {"dimensionless", QuantityDimension::dimensionless},
                         {"angle", QuantityDimension::angle},
                         {"angular_speed", QuantityDimension::angular_speed},
                         {"area", QuantityDimension::area},
                         {"density", QuantityDimension::density},
                         {"duration", QuantityDimension::duration},
                         {"energy_per_mass", QuantityDimension::energy_per_mass},
                         {"force", QuantityDimension::force},
                         {"frequency", QuantityDimension::frequency},
                         {"length", QuantityDimension::length},
                         {"mass", QuantityDimension::mass},
                         {"mass_flow_rate", QuantityDimension::mass_flow_rate},
                         {"molar_mass", QuantityDimension::molar_mass},
                         {"moment_of_inertia", QuantityDimension::moment_of_inertia},
                         {"power", QuantityDimension::power},
                         {"pressure", QuantityDimension::pressure},
                         {"pressure_per_speed",
                          QuantityDimension::pressure_per_speed},
                         {"pressure_per_speed_squared",
                          QuantityDimension::pressure_per_speed_squared},
                         {"speed", QuantityDimension::speed},
                         {"temperature", QuantityDimension::temperature},
                         {"torque", QuantityDimension::torque},
                         {"volume", QuantityDimension::volume},
                         {"volume_flow_rate", QuantityDimension::volume_flow_rate},
                     },
                     output, subject_value);
}

} // namespace

void parse_curve_definition(DocumentReader &reader, JsonValue value,
                            std::string_view path, CurveDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path,
                          {"id", "input_dimension", "output_dimension", "evaluation",
                           "triangle_filter_radius", "below_domain", "above_domain",
                           "samples"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("curve", output.id.value);
    parse_quantity_dimension(
        reader, reader.required(value, "input_dimension", path, owner),
        pointer_member(path, "input_dimension"), output.input_dimension, owner);
    parse_quantity_dimension(
        reader, reader.required(value, "output_dimension", path, owner),
        pointer_member(path, "output_dimension"), output.output_dimension, owner);
    read_enum(
        reader, reader.required(value, "evaluation", path, owner),
        pointer_member(path, "evaluation"),
        {{"linear", CurveEvaluation::linear},
         {"right_continuous_hold", CurveEvaluation::right_continuous_hold},
         {"triangle_weighted_samples", CurveEvaluation::triangle_weighted_samples}},
        output.evaluation, owner);
    read_enum(reader, reader.required(value, "below_domain", path, owner),
              pointer_member(path, "below_domain"),
              {{"clamp", CurveBoundaryBehavior::clamp},
               {"zero", CurveBoundaryBehavior::zero},
               {"reject", CurveBoundaryBehavior::reject}},
              output.below_domain, owner);
    read_enum(reader, reader.required(value, "above_domain", path, owner),
              pointer_member(path, "above_domain"),
              {{"clamp", CurveBoundaryBehavior::clamp},
               {"zero", CurveBoundaryBehavior::zero},
               {"reject", CurveBoundaryBehavior::reject}},
              output.above_domain, owner);

    const auto radius = reader.optional(value, "triangle_filter_radius");
    if (radius.valid() && !radius.is_null()) {
        Quantity parsed;
        reader.quantity(radius, pointer_member(path, "triangle_filter_radius"),
                        output.input_dimension, parsed, owner);
        require_positive(reader, parsed, pointer_member(path, "triangle_filter_radius"),
                         owner);
        output.triangle_filter_radius = std::move(parsed);
    }
    if (output.evaluation == CurveEvaluation::triangle_weighted_samples &&
        !output.triangle_filter_radius) {
        reader.add(DiagnosticCode::missing_value,
                   pointer_member(path, "triangle_filter_radius"),
                   "triangle-weighted evaluation requires a filter radius", owner);
    }

    const auto samples = reader.required(value, "samples", path, owner);
    const auto samples_path = pointer_member(path, "samples");
    if (!reader.array(samples, samples_path, owner)) {
        return;
    }
    std::unordered_set<std::string> abscissas;
    output.samples.reserve(samples.size());
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample = samples.at(index);
        const auto sample_path = pointer_index(samples_path, index);
        CurveSample parsed;
        if (reader.object(sample, sample_path, owner)) {
            reader.reject_unknown(sample, sample_path, {"input", "output"}, owner);
            reader.quantity(reader.required(sample, "input", sample_path, owner),
                            pointer_member(sample_path, "input"),
                            output.input_dimension, parsed.input, owner);
            reader.quantity(reader.required(sample, "output", sample_path, owner),
                            pointer_member(sample_path, "output"),
                            output.output_dimension, parsed.output, owner);
            const auto authored_key =
                parsed.input.unit + '\n' + std::to_string(parsed.input.value);
            if (!abscissas.insert(authored_key).second) {
                reader.add(DiagnosticCode::duplicate_id,
                           pointer_member(sample_path, "input"),
                           "curve sample input is duplicated", owner);
            }
        }
        output.samples.push_back(std::move(parsed));
    }
    if (output.samples.empty()) {
        reader.add(DiagnosticCode::missing_value, samples_path,
                   "curve requires at least one sample", owner);
    }
}

void parse_flow_restriction(DocumentReader &reader, JsonValue value,
                            std::string_view path, FlowRestriction &output,
                            const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    std::string type;
    reader.string(reader.required(value, "type", path, subject_value),
                  pointer_member(path, "type"), type, subject_value);
    if (type == "flow_bench") {
        reader.reject_unknown(value, path, {"type", "rated_flow", "pressure_drop"},
                              subject_value);
        FlowBenchRestriction parsed;
        read_quantity_member(reader, value, "rated_flow", path,
                             QuantityDimension::volume_flow_rate, parsed.rated_flow,
                             subject_value);
        read_quantity_member(reader, value, "pressure_drop", path,
                             QuantityDimension::pressure, parsed.pressure_drop,
                             subject_value);
        require_positive(reader, parsed.rated_flow, pointer_member(path, "rated_flow"),
                         subject_value);
        require_positive(reader, parsed.pressure_drop,
                         pointer_member(path, "pressure_drop"), subject_value);
        if (!parsed.rated_flow.standard) {
            reader.add(DiagnosticCode::missing_value,
                       pointer_member(pointer_member(path, "rated_flow"), "standard"),
                       "flow-bench rating requires its calibration standard",
                       subject_value);
        }
        output = std::move(parsed);
    } else if (type == "orifice") {
        reader.reject_unknown(value, path,
                              {"type", "effective_area", "discharge_coefficient_01"},
                              subject_value);
        OrificeRestriction parsed;
        read_quantity_member(reader, value, "effective_area", path,
                             QuantityDimension::area, parsed.effective_area,
                             subject_value);
        reader.fraction(
            reader.required(value, "discharge_coefficient_01", path, subject_value),
            pointer_member(path, "discharge_coefficient_01"),
            parsed.discharge_coefficient_01, subject_value);
        require_positive(reader, parsed.effective_area,
                         pointer_member(path, "effective_area"), subject_value);
        output = std::move(parsed);
    } else if (type == "curve") {
        reader.reject_unknown(value, path, {"type", "pressure_drop_to_flow"},
                              subject_value);
        CurveRestriction parsed;
        read_ref_member(reader, value, "pressure_drop_to_flow", path,
                        parsed.pressure_drop_to_flow, subject_value);
        output = std::move(parsed);
    } else if (!type.empty()) {
        reader.add(DiagnosticCode::invalid_value, pointer_member(path, "type"),
                   "unknown restriction type '" + type + "'", subject_value);
    }
}

void parse_engine_identity(DocumentReader &reader, JsonValue value,
                           std::string_view path, EngineIdentity &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(value, path, {"id", "display_name", "description"});
    read_id_member(reader, value, "id", path, output.id);
    const auto owner = subject("engine", output.id.value);
    reader.string(reader.required(value, "display_name", path, owner),
                  pointer_member(path, "display_name"), output.display_name, owner);
    const auto description = reader.optional(value, "description");
    if (description.valid() && !description.is_null()) {
        std::string parsed;
        if (reader.string(description, pointer_member(path, "description"), parsed,
                          owner)) {
            output.description = std::move(parsed);
        }
    }
}

void parse_engine_limits(DocumentReader &reader, JsonValue value, std::string_view path,
                         EngineLimits &output,
                         const std::optional<DiagnosticSubject> &subject_value) {
    if (!reader.object(value, path, subject_value)) {
        return;
    }
    reader.reject_unknown(value, path, {"redline"}, subject_value);
    read_quantity_member(reader, value, "redline", path,
                         QuantityDimension::angular_speed, output.redline,
                         subject_value);
    require_positive(reader, output.redline, pointer_member(path, "redline"),
                     subject_value);
}

} // namespace engine_sim_offline::authoring::detail
