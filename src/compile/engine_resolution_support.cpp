#include "compile/engine_resolver_internal.hpp"

#include "compile/diagnostics.hpp"

#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace engine_sim_offline::compile::detail::engine_resolution {
namespace {

constexpr double kLegacyPi = 3.14159265359;
constexpr double kLegacyRpmScale = 0.104719755;

} // namespace

std::string pointer_index(std::string_view collection, std::size_t index) {
    return std::string{collection} + "/" + std::to_string(index);
}

authoring::DiagnosticReport unsupported(std::string_view path,
                                        std::string message) {
    return diagnostic(authoring::DiagnosticCode::unsupported_capability, path,
                      std::move(message));
}

authoring::DiagnosticReport invalid(std::string_view path,
                                    std::string message) {
    return diagnostic(authoring::DiagnosticCode::invalid_value, path,
                      std::move(message));
}

double legacy_si_value(const authoring::Quantity &quantity) {
    // legacy_low_order_v1 owns these source operation orders. They intentionally
    // differ by a few binary64 bits from a mathematically collapsed SI conversion.
    const double centimetre = 1.0 / 100.0;
    const double millimetre = 1.0 / 1000.0;
    const double inch = centimetre * 2.54;
    const double foot = inch * 12.0;
    const double pound_force = 4.44822;
    const double cc = centimetre * centimetre * centimetre;
    if (quantity.unit == "1" || quantity.unit == "rad" ||
        quantity.unit == "m" || quantity.unit == "m2" ||
        quantity.unit == "m3" || quantity.unit == "kg" ||
        quantity.unit == "kg*m2" || quantity.unit == "N*m" ||
        quantity.unit == "Pa" || quantity.unit == "Pa*s/m" ||
        quantity.unit == "Pa*s2/m2" || quantity.unit == "K" ||
        quantity.unit == "s" || quantity.unit == "Hz" ||
        quantity.unit == "m/s" || quantity.unit == "kg/mol" ||
        quantity.unit == "J/kg") {
        return quantity.value;
    }
    if (quantity.unit == "deg") {
        return quantity.value * (kLegacyPi / 180.0);
    }
    if (quantity.unit == "rpm") {
        return quantity.value * kLegacyRpmScale;
    }
    if (quantity.unit == "cm") {
        return quantity.value * centimetre;
    }
    if (quantity.unit == "mm") {
        return quantity.value * millimetre;
    }
    if (quantity.unit == "in") {
        return quantity.value * inch;
    }
    if (quantity.unit == "lb*ft") {
        return quantity.value * (pound_force * foot);
    }
    if (quantity.unit == "cm2") {
        return quantity.value * (centimetre * centimetre);
    }
    if (quantity.unit == "mm2") {
        return quantity.value * (millimetre * millimetre);
    }
    if (quantity.unit == "in2") {
        return quantity.value * (inch * inch);
    }
    if (quantity.unit == "cm3") {
        return quantity.value * cc;
    }
    if (quantity.unit == "L") {
        return quantity.value * (cc * 1000.0);
    }
    if (quantity.unit == "g") {
        return quantity.value * (1.0 / 1000.0);
    }
    if (quantity.unit == "g/mol") {
        return quantity.value * (1.0 / 1000.0);
    }
    if (quantity.unit == "kJ/kg") {
        return quantity.value * 1000.0;
    }
    if (quantity.unit == "MJ/kg") {
        return (quantity.value * 1000.0) / (1.0 / 1000.0);
    }
    if (quantity.unit == "kPa") {
        return quantity.value * 1000.0;
    }
    if (quantity.unit == "bar") {
        return quantity.value * 100000.0;
    }
    if (quantity.unit == "atm") {
        return quantity.value * 101325.0;
    }
    if (quantity.unit == "psi") {
        return quantity.value * 6894.757293168361;
    }
    if (quantity.unit == "inHg") {
        return quantity.value * 3386.3886666666713;
    }
    if (quantity.unit == "inH2O") {
        return quantity.value * (3386.3886666666713 * 0.0734824);
    }
    if (quantity.unit == "kPa*s/m" ||
        quantity.unit == "kPa*s2/m2") {
        return quantity.value * 1000.0;
    }
    if (quantity.unit == "bar*s/m" ||
        quantity.unit == "bar*s2/m2") {
        return quantity.value * 100000.0;
    }
    if (quantity.unit == "degC") {
        return quantity.value + 273.15;
    }
    if (quantity.unit == "ms") {
        return quantity.value * (1.0 / 1000.0);
    }
    if (quantity.unit == "cfm") {
        return quantity.value * 0.0004719474432;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

ResolutionEmitter::ResolutionEmitter(
    ResolutionProvenanceBuilder &builder) noexcept
    : builder_(&builder) {}

ResolutionEmitter::ResolutionEmitter(
    const contract::ProvenanceLedger &ledger) {
    resolution_ids_.reserve(ledger.resolutions.size());
    for (const auto &resolution : ledger.resolutions) {
        resolution_ids_.emplace(resolution.parameter_path, resolution.id);
    }
}

std::string ResolutionEmitter::authored_id(std::string path) {
    if (builder_ != nullptr) {
        builder_->add_authored(std::move(path));
        return {};
    }
    return resolution_ids_.at(path);
}

std::string ResolutionEmitter::derived_id(
    std::string path, contract::MethodIdentity method,
    std::span<const std::string_view> dependencies) {
    if (builder_ != nullptr) {
        builder_->add_derived(std::move(path), std::move(method), dependencies);
        return {};
    }
    return resolution_ids_.at(path);
}

AssembledContracts assemble_contracts(const ModelContext &context,
                                      ResolutionEmitter &emitter) {
    AssembledContracts result;
    result.engine = assemble_engine(context, emitter);
    result.rig = assemble_rig(context, emitter);
    const auto &profile =
        std::get<contract::LowOrderOperatingPointV1Profile>(
            result.engine.physics_profile);
    result.fuels.push_back({
        context.fuel->id.value,
        profile.core.fuel.fuel_id,
        profile.core.fuel.energy_density_j_per_kg,
        profile.core.fuel.molecular_mass_kg_per_mol,
        profile.core.fuel.molecular_afr,
    });
    assemble_presentation(context, emitter, result.presentation, result.randomness,
                          result.audio_buses);
    return result;
}

} // namespace engine_sim_offline::compile::detail::engine_resolution
