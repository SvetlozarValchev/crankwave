#include "compile/scenario_resolver_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace engine_sim_offline::compile::detail::scenario_resolution {

const ResolvedAudioBusDescriptor *
ScenarioResolver::find_bus(std::string_view authored_id) {
    const ResolvedAudioBusDescriptor *result = nullptr;
    for (const auto &bus : context_.audio_buses) {
        if (bus.authored_id != authored_id) {
            continue;
        }
        if (result != nullptr) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "compiled engine contains duplicate audio-bus descriptors");
            return nullptr;
        }
        result = &bus;
    }
    return result;
}

const contract::RouteSpec *
ScenarioResolver::find_route(contract::RouteId route_id) const noexcept {
    const auto found =
        std::ranges::find(context_.engine.routes, route_id, &contract::RouteSpec::id);
    return found == context_.engine.routes.end() ? nullptr : &*found;
}

void ScenarioResolver::compile_output_selection() {
    if (!document_.output.telemetry_channels.empty()) {
        add(authoring::DiagnosticCode::unsupported_capability,
            "/output/telemetry_channels",
            "the current renderer does not publish authored telemetry channels");
    }
    if (document_.output.buses.empty()) {
        add(authoring::DiagnosticCode::missing_value, "/output/buses",
            "at least one audio output bus must be selected");
        return;
    }

    selected_buses_.reserve(document_.output.buses.size());
    std::unordered_set<std::string> selected_ids;
    for (std::size_t index = 0; index < document_.output.buses.size(); ++index) {
        const auto &reference = document_.output.buses[index];
        const auto path = "/output/buses/" + std::to_string(index);
        if (!selected_ids.insert(reference.value).second) {
            add(authoring::DiagnosticCode::duplicate_id, path,
                "audio output bus is selected more than once");
            continue;
        }
        const auto *bus = find_bus(reference.value);
        if (bus == nullptr) {
            add(authoring::DiagnosticCode::dangling_reference, path,
                "audio bus reference does not resolve in the compiled engine");
            continue;
        }
        if (!bus->selectable) {
            add(authoring::DiagnosticCode::unsupported_capability, path,
                "selected audio bus is not publishable");
        }
        if (!contract::is_valid_semantic_id(bus->semantic_id) ||
            bus->kind == contract::OutputBusKind::unspecified ||
            sample_encoding_id(bus->sample_encoding).empty() ||
            !std::isfinite(bus->gain_linear) || bus->gain_linear != 1.0) {
            add(authoring::DiagnosticCode::internal_failure, "",
                "compiled audio-bus descriptor is not executable");
        }
        if (bus->routes.empty()) {
            add(authoring::DiagnosticCode::unsupported_capability, path,
                "selected audio bus has no physical source routes");
        }
        selected_buses_.push_back(bus);
    }
    const auto raw_count =
        std::ranges::count(selected_buses_, contract::OutputBusKind::master_engine_raw,
                           [](const auto *bus) { return bus->kind; });
    const auto audition_count = std::ranges::count(
        selected_buses_, contract::OutputBusKind::master_engine_audition,
        [](const auto *bus) { return bus->kind; });
    if (selected_buses_.size() != 2U || raw_count != 1U || audition_count != 1U) {
        add(authoring::DiagnosticCode::unsupported_capability, "/output/buses",
            "the current presentation session requires exactly the engine raw "
            "and audition buses");
    }
    std::ranges::sort(selected_buses_, [](const auto *left, const auto *right) {
        return std::tie(left->semantic_id, left->authored_id) <
               std::tie(right->semantic_id, right->authored_id);
    });
    request_input_.selected_audio_buses.reserve(selected_buses_.size());
    for (const auto *bus : selected_buses_) {
        request_input_.selected_audio_buses.push_back(*bus);
    }

    for (const auto *bus : selected_buses_) {
        for (const auto route_id : bus->routes) {
            const auto *route = find_route(route_id);
            if (route == nullptr) {
                add(authoring::DiagnosticCode::internal_failure, "",
                    "compiled audio bus references an unknown engine route");
                continue;
            }
            if (!contract::is_valid_semantic_id(route->semantic_id.value) ||
                route->kind.value == contract::SourceRouteKind::unspecified) {
                add(authoring::DiagnosticCode::internal_failure, "",
                    "compiled audio bus references a non-executable engine route");
            }
            request_input_.published_route_ids.push_back(route_id);
        }
    }
    std::ranges::sort(request_input_.published_route_ids, {},
                      [](const contract::RouteId id) { return id.value; });
    request_input_.published_route_ids.erase(
        std::unique(request_input_.published_route_ids.begin(),
                    request_input_.published_route_ids.end()),
        request_input_.published_route_ids.end());
}

contract::SourceMatrixContract ScenarioResolver::build_source_matrix() {
    contract::SourceMatrixContract matrix;
    matrix.id = "scenario-source-matrix." + scenario_.scenario_id;
    matrix.distribution = context_.distribution;
    const contract::AudioContract route_audio{
        scenario_.rates.delivery,
        request_input_.audible_delivery_frames,
        "mono",
        "float32le",
    };

    for (const auto route_id : request_input_.published_route_ids) {
        const auto *route = find_route(route_id);
        if (route == nullptr) {
            continue;
        }
        const auto role_prefix = route->semantic_id.value;
        const std::vector<std::string> roles{
            role_prefix + ".dry",
            role_prefix + ".configured_transfer",
            role_prefix + ".selected",
        };
        matrix.required_source_routes.push_back({
            route->semantic_id.value,
            route->kind.value,
            contract::RouteDisposition::rendered,
            "",
            roles,
        });
        matrix.required_artifacts.push_back(
            {roles[0], contract::ArtifactKind::audio, route_audio, true});
        matrix.required_artifacts.push_back(
            {roles[1], contract::ArtifactKind::audio, route_audio, true});
        matrix.required_artifacts.push_back(
            {roles[2], contract::ArtifactKind::audio, route_audio, false});
    }
    std::ranges::sort(matrix.required_source_routes, {},
                      &contract::SourceRouteRequirement::semantic_id);

    for (const auto *bus : selected_buses_) {
        const auto &role = bus->semantic_id;
        matrix.required_output_buses.push_back({
            bus->semantic_id,
            bus->kind,
            {role},
        });
        matrix.required_artifacts.push_back({
            role,
            contract::ArtifactKind::audio,
            contract::AudioContract{
                scenario_.rates.delivery,
                request_input_.audible_delivery_frames,
                "mono",
                std::string{sample_encoding_id(bus->sample_encoding)},
            },
            bus->diagnostic,
        });
    }
    std::ranges::sort(matrix.required_output_buses, {},
                      &contract::OutputBusRequirement::semantic_id);

    std::vector<const contract::RouteSpec *> omitted;
    for (const auto &route : context_.engine.routes) {
        if (std::ranges::find(request_input_.published_route_ids, route.id) ==
            request_input_.published_route_ids.end()) {
            omitted.push_back(&route);
        }
    }
    std::ranges::sort(omitted, [](const auto *left, const auto *right) {
        return left->semantic_id.value < right->semantic_id.value;
    });
    for (const auto *route : omitted) {
        matrix.declared_omissions.push_back({
            "omitted.source-route." + route->semantic_id.value,
            contract::OmissionKind::source_route,
            "Route '" + route->semantic_id.value +
                "' was not selected by the authored scenario output buses.",
        });
    }

    std::ranges::sort(matrix.required_artifacts, {},
                      &contract::ArtifactRequirement::role);
    matrix.sha256 = source_matrix_digest(matrix);
    return matrix;
}

} // namespace engine_sim_offline::compile::detail::scenario_resolution
