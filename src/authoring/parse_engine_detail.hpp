#pragma once

#include "authoring/document_reader.hpp"
#include "crankwave/authoring/engine_document.hpp"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crankwave::authoring::detail {

template <class Enum>
bool read_enum(DocumentReader &reader, JsonValue value, std::string_view path,
               std::initializer_list<std::pair<std::string_view, Enum>> choices,
               Enum &output,
               const std::optional<DiagnosticSubject> &subject_value = {}) {
    std::string token;
    if (!reader.string(value, path, token, subject_value)) {
        return false;
    }
    for (const auto &[name, choice] : choices) {
        if (token == name) {
            output = choice;
            return true;
        }
    }
    reader.add(DiagnosticCode::invalid_value, path,
               "unknown enum value '" + token + "'", subject_value);
    return false;
}

template <class Value, class Parse>
void read_array(DocumentReader &reader, JsonValue value, std::string_view path,
                std::vector<Value> &output, Parse parse,
                const std::optional<DiagnosticSubject> &subject_value = {}) {
    if (!reader.array(value, path, subject_value)) {
        return;
    }
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        Value parsed;
        parse(value.at(index), pointer_index(path, index), parsed);
        output.push_back(std::move(parsed));
    }
}

template <class Value, class Parse>
void read_required_array(DocumentReader &reader, JsonValue object, std::string_view key,
                         std::string_view object_path, std::vector<Value> &output,
                         Parse parse,
                         const std::optional<DiagnosticSubject> &subject_value = {}) {
    const auto path = pointer_member(object_path, key);
    read_array(reader, reader.required(object, key, object_path, subject_value), path,
               output, std::move(parse), subject_value);
}

inline bool
read_quantity_member(DocumentReader &reader, JsonValue object, std::string_view key,
                     std::string_view object_path, QuantityDimension dimension,
                     Quantity &output,
                     const std::optional<DiagnosticSubject> &subject_value = {}) {
    return reader.quantity(reader.required(object, key, object_path, subject_value),
                           pointer_member(object_path, key), dimension, output,
                           subject_value);
}

template <class Tag>
bool read_id_member(DocumentReader &reader, JsonValue object, std::string_view key,
                    std::string_view object_path, StableId<Tag> &output,
                    const std::optional<DiagnosticSubject> &subject_value = {}) {
    return reader.id(reader.required(object, key, object_path, subject_value),
                     pointer_member(object_path, key), output, subject_value);
}

template <class Tag>
bool read_ref_member(DocumentReader &reader, JsonValue object, std::string_view key,
                     std::string_view object_path, StableRef<Tag> &output,
                     const std::optional<DiagnosticSubject> &subject_value = {}) {
    return reader.ref(reader.required(object, key, object_path, subject_value),
                      pointer_member(object_path, key), output, subject_value);
}

inline void
require_positive(DocumentReader &reader, const Quantity &quantity,
                 std::string_view path,
                 const std::optional<DiagnosticSubject> &subject_value = {}) {
    if (quantity.value <= 0.0) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "value"),
                   "quantity must be positive", subject_value);
    }
}

inline void
require_nonnegative(DocumentReader &reader, const Quantity &quantity,
                    std::string_view path,
                    const std::optional<DiagnosticSubject> &subject_value = {}) {
    if (quantity.value < 0.0) {
        reader.add(DiagnosticCode::out_of_range, pointer_member(path, "value"),
                   "quantity must be nonnegative", subject_value);
    }
}

void parse_curve_definition(DocumentReader &, JsonValue, std::string_view,
                            CurveDefinition &);
void parse_flow_restriction(DocumentReader &, JsonValue, std::string_view,
                            FlowRestriction &,
                            const std::optional<DiagnosticSubject> &subject_value = {});
void parse_engine_identity(DocumentReader &, JsonValue, std::string_view,
                           EngineIdentity &);
void parse_engine_limits(DocumentReader &, JsonValue, std::string_view, EngineLimits &,
                         const std::optional<DiagnosticSubject> &);

void parse_crankshaft(DocumentReader &, JsonValue, std::string_view,
                      CrankshaftDefinition &);
void parse_journal(DocumentReader &, JsonValue, std::string_view, JournalDefinition &);
void parse_connecting_rod(DocumentReader &, JsonValue, std::string_view,
                          ConnectingRodDefinition &);
void parse_piston(DocumentReader &, JsonValue, std::string_view, PistonDefinition &);
void parse_bank(DocumentReader &, JsonValue, std::string_view, BankDefinition &);
void parse_cylinder(DocumentReader &, JsonValue, std::string_view,
                    CylinderDefinition &);

void parse_intake(DocumentReader &, JsonValue, std::string_view, IntakeDefinition &);
void parse_exhaust(DocumentReader &, JsonValue, std::string_view, ExhaustDefinition &);
void parse_port(DocumentReader &, JsonValue, std::string_view, PortDefinition &);
void parse_source_route(DocumentReader &, JsonValue, std::string_view,
                        SourceRouteDefinition &);

void parse_cam_lobe(DocumentReader &, JsonValue, std::string_view, CamLobeDefinition &);
void parse_camshaft(DocumentReader &, JsonValue, std::string_view,
                    CamshaftDefinition &);
void parse_valvetrain(DocumentReader &, JsonValue, std::string_view,
                      ValvetrainDefinition &);
void parse_head(DocumentReader &, JsonValue, std::string_view, HeadDefinition &);

void parse_ignition(DocumentReader &, JsonValue, std::string_view, IgnitionDefinition &,
                    const std::optional<DiagnosticSubject> &);
void parse_fuel(DocumentReader &, JsonValue, std::string_view, FuelDefinition &);
void parse_combustion(DocumentReader &, JsonValue, std::string_view,
                      CombustionDefinition &, const std::optional<DiagnosticSubject> &);
void parse_accessory_configuration(DocumentReader &, JsonValue, std::string_view,
                                   AccessoryConfigurationDefinition &);
void parse_losses(DocumentReader &, JsonValue, std::string_view,
                  EngineLossDefinition &,
                  const std::optional<DiagnosticSubject> &);
void parse_throttle_controller(DocumentReader &, JsonValue, std::string_view,
                               ThrottleControllerDefinition &);
void parse_starter(DocumentReader &, JsonValue, std::string_view, StarterDefinition &,
                   const std::optional<DiagnosticSubject> &);

void parse_presentation(DocumentReader &, JsonValue, std::string_view,
                        PresentationDefinition &);
void parse_rig(DocumentReader &, JsonValue, std::string_view, RigDefinition &);

} // namespace crankwave::authoring::detail
