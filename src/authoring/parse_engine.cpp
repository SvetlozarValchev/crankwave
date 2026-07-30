#include "engine_sim_offline/authoring/parse.hpp"

#include "authoring/document_reader.hpp"
#include "authoring/parse_engine_detail.hpp"
#include "authoring/parse_engine_references.hpp"

#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace engine_sim_offline::authoring {
namespace {

using detail::DocumentReader;
using detail::pointer_member;
using detail::read_enum;
using detail::read_id_member;
using detail::read_ref_member;
using detail::subject;

template <class Value, class Parse>
void parse_required_definitions(DocumentReader &reader, JsonValue object,
                                std::string_view member,
                                std::string_view object_path,
                                std::vector<Value> &output, Parse parse,
                                const std::optional<DiagnosticSubject> &owner) {
    detail::read_required_array(
        reader, object, member, object_path, output,
        [&](JsonValue item, std::string_view item_path, Value &definition) {
            parse(reader, item, item_path, definition);
        },
        owner);
}

void parse_engine_cycle(DocumentReader &reader, JsonValue value,
                        std::string_view path, EngineCycle &output,
                        const std::optional<DiagnosticSubject> &owner) {
    std::string token;
    if (!reader.string(value, path, token, owner)) {
        return;
    }
    if (token == "four_stroke") {
        output = EngineCycle::four_stroke;
        return;
    }
    reader.add(DiagnosticCode::forbidden_cycle, path,
               "only the four_stroke engine cycle is supported by this schema",
               owner);
}

void parse_engine_definition(DocumentReader &reader, JsonValue value,
                             std::string_view path, EngineDefinition &output) {
    if (!reader.object(value, path)) {
        return;
    }
    reader.reject_unknown(
        value, path,
        {"identity", "cycle", "layout", "limits", "curves", "crankshafts",
         "journals", "connecting_rods", "pistons", "banks", "intakes",
         "exhausts", "ports", "cam_lobes", "camshafts", "valvetrains", "heads",
         "fuels", "default_fuel", "losses", "ignition", "throttle_controllers",
         "throttle_controller", "starter", "cylinders", "source_routes"});

    detail::parse_engine_identity(
        reader, reader.required(value, "identity", path),
        pointer_member(path, "identity"), output.identity);
    const auto owner = subject("engine", output.identity.id.value);

    parse_engine_cycle(reader, reader.required(value, "cycle", path, owner),
                       pointer_member(path, "cycle"), output.cycle, owner);
    read_enum(reader, reader.required(value, "layout", path, owner),
              pointer_member(path, "layout"),
              {{"inline", CylinderLayout::inline_engine},
               {"v_engine", CylinderLayout::v_engine},
               {"opposed", CylinderLayout::opposed},
               {"custom", CylinderLayout::custom}},
              output.layout, owner);
    detail::parse_engine_limits(
        reader, reader.required(value, "limits", path, owner),
        pointer_member(path, "limits"), output.limits, owner);

    parse_required_definitions(
        reader, value, "curves", path, output.curves,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           CurveDefinition &definition) {
            detail::parse_curve_definition(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "crankshafts", path, output.crankshafts,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           CrankshaftDefinition &definition) {
            detail::parse_crankshaft(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "journals", path, output.journals,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           JournalDefinition &definition) {
            detail::parse_journal(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "connecting_rods", path, output.connecting_rods,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           ConnectingRodDefinition &definition) {
            detail::parse_connecting_rod(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "pistons", path, output.pistons,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           PistonDefinition &definition) {
            detail::parse_piston(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "banks", path, output.banks,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           BankDefinition &definition) {
            detail::parse_bank(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "intakes", path, output.intakes,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           IntakeDefinition &definition) {
            detail::parse_intake(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "exhausts", path, output.exhausts,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           ExhaustDefinition &definition) {
            detail::parse_exhaust(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "ports", path, output.ports,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           PortDefinition &definition) {
            detail::parse_port(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "cam_lobes", path, output.cam_lobes,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           CamLobeDefinition &definition) {
            detail::parse_cam_lobe(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "camshafts", path, output.camshafts,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           CamshaftDefinition &definition) {
            detail::parse_camshaft(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "valvetrains", path, output.valvetrains,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           ValvetrainDefinition &definition) {
            detail::parse_valvetrain(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "heads", path, output.heads,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           HeadDefinition &definition) {
            detail::parse_head(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "fuels", path, output.fuels,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           FuelDefinition &definition) {
            detail::parse_fuel(item_reader, item, item_path, definition);
        },
        owner);
    read_ref_member(reader, value, "default_fuel", path, output.default_fuel, owner);
    detail::parse_losses(reader, reader.required(value, "losses", path, owner),
                         pointer_member(path, "losses"), output.losses, owner);
    detail::parse_ignition(
        reader, reader.required(value, "ignition", path, owner),
        pointer_member(path, "ignition"), output.ignition, owner);

    const auto controllers = reader.optional(value, "throttle_controllers");
    if (controllers.valid() && !controllers.is_null()) {
        std::vector<ThrottleControllerDefinition> parsed;
        detail::read_array(
            reader, controllers, pointer_member(path, "throttle_controllers"),
            parsed,
            [&](JsonValue item, std::string_view item_path,
                ThrottleControllerDefinition &definition) {
                detail::parse_throttle_controller(reader, item, item_path,
                                                  definition);
            },
            owner);
        output.throttle_controllers = std::move(parsed);
    }
    const auto selected_controller = reader.optional(value, "throttle_controller");
    if (selected_controller.valid() && !selected_controller.is_null()) {
        ThrottleControllerRef parsed;
        if (reader.ref(selected_controller,
                       pointer_member(path, "throttle_controller"), parsed, owner)) {
            output.throttle_controller = std::move(parsed);
        }
    }
    detail::parse_starter(reader, reader.required(value, "starter", path, owner),
                          pointer_member(path, "starter"), output.starter, owner);
    parse_required_definitions(
        reader, value, "cylinders", path, output.cylinders,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           CylinderDefinition &definition) {
            detail::parse_cylinder(item_reader, item, item_path, definition);
        },
        owner);
    parse_required_definitions(
        reader, value, "source_routes", path, output.source_routes,
        [](DocumentReader &item_reader, JsonValue item, std::string_view item_path,
           SourceRouteDefinition &definition) {
            detail::parse_source_route(item_reader, item, item_path, definition);
        },
        owner);
}

void parse_engine_package(DocumentReader &reader, JsonValue value,
                          EnginePackageDocument &output) {
    if (!reader.object(value, "")) {
        return;
    }
    reader.reject_unknown(value, "", {"schema", "engine", "presentation", "rig"});
    reader.string(reader.required(value, "schema", ""), "/schema", output.schema);
    if (!output.schema.empty() && output.schema != "engine-sim-offline/engine") {
        reader.add(DiagnosticCode::unsupported_schema, "/schema",
                   "expected schema 'engine-sim-offline/engine'");
    }
    parse_engine_definition(reader, reader.required(value, "engine", ""), "/engine",
                            output.engine);
    detail::parse_presentation(
        reader, reader.required(value, "presentation", ""), "/presentation",
        output.presentation);
    const auto rig = reader.optional(value, "rig");
    if (rig.valid() && !rig.is_null()) {
        RigDefinition parsed;
        detail::parse_rig(reader, rig, "/rig", parsed);
        output.rig = std::move(parsed);
    }
}

[[nodiscard]] DiagnosticReport resource_diagnostic(std::string message) {
    Diagnostic diagnostic;
    diagnostic.code = DiagnosticCode::resource_limit;
    diagnostic.message = std::move(message);
    return DiagnosticReport{{std::move(diagnostic)}};
}

} // namespace

EngineDocumentParseResult
parse_engine_document(std::string_view json, AuthoringParseLimits limits) noexcept {
    try {
        auto json_result = parse_json(json, limits.json);
        if (const auto *error = std::get_if<JsonParseError>(&json_result)) {
            return detail::syntax_diagnostic(*error);
        }
        auto json_document = std::get<JsonDocument>(std::move(json_result));
        DocumentReader reader{std::move(limits)};
        EnginePackageDocument output;
        parse_engine_package(reader, json_document.root(), output);
        if (reader.ok()) {
            detail::validate_engine_document(reader, output);
        }
        if (!reader.ok()) {
            return std::move(reader).finish();
        }
        return output;
    } catch (const std::bad_alloc &) {
        return resource_diagnostic(
            "allocation failed while parsing engine package document");
    } catch (const std::exception &exception) {
        return detail::internal_diagnostic(
            "unexpected engine parser failure: " +
            std::string{exception.what()});
    } catch (...) {
        return detail::internal_diagnostic(
            "unexpected non-standard engine parser failure");
    }
}

} // namespace engine_sim_offline::authoring
