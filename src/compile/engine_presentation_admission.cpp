#include "compile/engine_resolver_internal.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

void add(authoring::DiagnosticReport &report, authoring::DiagnosticCode code,
         std::string path, std::string message) {
    authoring::Diagnostic value;
    value.code = code;
    value.json_pointer = std::move(path);
    value.message = std::move(message);
    report.diagnostics.push_back(std::move(value));
}

template <class Range, class Projection>
[[nodiscard]] const typename Range::value_type *
find_by_text(const Range &range, std::string_view id, Projection projection) {
    const auto found = std::ranges::find_if(
        range, [&](const auto &value) { return projection(value) == id; });
    return found == range.end() ? nullptr : &*found;
}

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

} // namespace

void admit_engine_presentation(const authoring::EnginePackageDocument &document,
                               ModelContext &resolved,
                               authoring::DiagnosticReport &report) {
    const auto &engine = document.engine;
    using authoring::DiagnosticCode;

    for (std::size_t index = 0; index < document.presentation.cylinder_routes.size();
         ++index) {
        const auto &binding = document.presentation.cylinder_routes[index];
        if (!resolved.cylinder_presentations.emplace(binding.cylinder.value, &binding)
                 .second) {
            add(report, DiagnosticCode::duplicate_id,
                pointer_index("/presentation/cylinder_routes", index) + "/cylinder",
                "each cylinder must have exactly one presentation binding");
        }
        const auto cylinder = find_by_text(
            engine.cylinders, binding.cylinder.value,
            [](const auto &value) -> const std::string & { return value.id.value; });
        if (cylinder != nullptr) {
            const auto route = resolved.route_for_exhaust.find(cylinder->exhaust.value);
            if (route == resolved.route_for_exhaust.end() ||
                route->second != binding.route.value) {
                add(report, DiagnosticCode::inconsistent_value,
                    pointer_index("/presentation/cylinder_routes", index) + "/route",
                    "cylinder presentation route must match its physical exhaust");
            }
        }
    }

    for (std::size_t index = 0; index < document.presentation.routes.size(); ++index) {
        const auto &binding = document.presentation.routes[index];
        if (!resolved.route_presentations.emplace(binding.route.value, &binding)
                 .second) {
            add(report, DiagnosticCode::duplicate_id,
                pointer_index("/presentation/routes", index) + "/route",
                "each source route must have exactly one presentation binding");
        }
        const auto source_route = find_by_text(
            engine.source_routes, binding.route.value,
            [](const auto &value) -> const std::string & { return value.id.value; });
        const bool exhaust =
            source_route != nullptr &&
            std::holds_alternative<authoring::ExhaustRouteSource>(source_route->source);
        const bool intake =
            source_route != nullptr &&
            std::holds_alternative<authoring::IntakeRouteSource>(source_route->source);
        if (exhaust && !binding.impulse_response.has_value()) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/presentation/routes", index) + "/impulse_response",
                "an active exhaust route requires an impulse response");
        }
        if (intake && (binding.impulse_response.has_value() ||
                       !same_binary64(binding.source_gain_linear, +0.0) ||
                       !same_binary64(binding.impulse_response_gain_linear, +0.0) ||
                       !same_binary64(binding.wet_mix_01, +0.0))) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/presentation/routes", index),
                "a declared-silent intake route requires no impulse response and "
                "canonical positive-zero source, transfer, and wet gains");
        }
    }
    if (resolved.route_presentations.size() != engine.source_routes.size()) {
        add(report, DiagnosticCode::unsupported_capability, "/presentation/routes",
            "presentation routes must exactly cover the engine source routes");
    }

    std::unordered_set<std::string> used_audio_assets;
    for (const auto &route : document.presentation.routes) {
        if (route.impulse_response) {
            used_audio_assets.insert(route.impulse_response->value);
        }
    }
    if (used_audio_assets.size() != document.presentation.assets.size()) {
        add(report, DiagnosticCode::disconnected_object, "/presentation/assets",
            "all audio assets must be referenced by an admitted route");
    }

    std::unordered_map<std::string, const authoring::AudioBusDefinition *> buses;
    for (const auto &bus : document.presentation.buses) {
        buses.emplace(bus.id.value, &bus);
    }
    if (document.presentation.buses.size() != 2U ||
        document.presentation.audition.buses.size() != 1U) {
        add(report, DiagnosticCode::unsupported_capability, "/presentation/buses",
            "the current renderer requires exactly one audition-selected bus and "
            "one remaining raw bus");
    }

    std::unordered_set<std::string> selected_bus_ids;
    std::vector<std::string> selected_routes;
    for (std::size_t index = 0; index < document.presentation.audition.buses.size();
         ++index) {
        const auto id = document.presentation.audition.buses[index].value;
        const auto found = buses.find(id);
        if (found == buses.end() || !selected_bus_ids.insert(id).second) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/presentation/audition/buses", index),
                "audition buses must resolve uniquely");
            continue;
        }
        for (const auto &route : found->second->routes) {
            selected_routes.push_back(route.value);
        }
    }
    const std::unordered_set<std::string> unique_selected_routes{
        selected_routes.begin(), selected_routes.end()};
    std::unordered_set<std::string> engine_route_ids;
    for (const auto &route : engine.source_routes) {
        engine_route_ids.insert(route.id.value);
    }
    if (selected_routes.size() != engine_route_ids.size() ||
        unique_selected_routes.size() != engine_route_ids.size() ||
        unique_selected_routes != engine_route_ids) {
        add(report, DiagnosticCode::unsupported_capability,
            "/presentation/audition/buses",
            "the current audition method requires the selected buses to flatten "
            "to every admitted source route exactly once");
    }

    std::vector<std::string> deterministic_raw_route_order;
    deterministic_raw_route_order.reserve(engine.source_routes.size());
    for (const auto &route : engine.source_routes) {
        deterministic_raw_route_order.push_back(route.id.value);
    }
    std::ranges::sort(deterministic_raw_route_order);
    for (std::size_t index = 0; index < document.presentation.buses.size(); ++index) {
        const auto &bus = document.presentation.buses[index];
        std::unordered_set<std::string> unique_routes;
        for (const auto &route : bus.routes) {
            unique_routes.insert(route.value);
        }
        if (!bus.publish || !same_binary64(bus.gain_linear, 1.0) ||
            bus.routes.size() != engine_route_ids.size() ||
            unique_routes.size() != engine_route_ids.size() ||
            unique_routes != engine_route_ids) {
            add(report, DiagnosticCode::unsupported_capability,
                pointer_index("/presentation/buses", index),
                "each current raw/audition bus must be published at exact unity "
                "gain and cover every admitted source route exactly once");
        }
        if (!selected_bus_ids.contains(bus.id.value)) {
            std::vector<std::string> raw_route_order;
            raw_route_order.reserve(bus.routes.size());
            for (const auto &route : bus.routes) {
                raw_route_order.push_back(route.value);
            }
            if (raw_route_order != deterministic_raw_route_order) {
                add(report, DiagnosticCode::unsupported_capability,
                    pointer_index("/presentation/buses", index) + "/routes",
                    "the raw bus route order must match the renderer's "
                    "deterministic EngineSpec route reduction order");
            }
        }
    }
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
