#include "simulation/legacy_flow_calibration.hpp"

#include "simulation/legacy_gas_primitives.hpp"

namespace engine_sim_offline::simulation {
namespace {

constexpr double kCalibrationPressurePa = 101325.0;
constexpr double kCalibrationTemperatureK = 298.15;

} // namespace

bool known_legacy_flow_calibration(
    const contract::LegacyRestrictionCalibration calibration) noexcept {
    return calibration == contract::LegacyRestrictionCalibration::carb_at_1p5_inhg ||
           calibration == contract::LegacyRestrictionCalibration::cfm_at_28_inh2o;
}

double legacy_flow_calibration_pressure_drop_pa(
    const contract::LegacyRestrictionCalibration calibration) noexcept {
    if (calibration == contract::LegacyRestrictionCalibration::cfm_at_28_inh2o) {
        return 28.0 * (3386.3886666666713 * 0.0734824);
    }
    return 1.5 * 3386.3886666666713;
}

double legacy_flow_bench_restriction_coefficient(
    const contract::LegacyRestrictionCalibration calibration,
    const double source_rating) noexcept {
    const double one_source_scfm_mol_s = 0.002641 * 453.59237 / 60.0;
    const double target_source_flow_mol_s = source_rating * one_source_scfm_mol_s;
    return legacy_restriction_coefficient(
        target_source_flow_mol_s, kCalibrationPressurePa,
        legacy_flow_calibration_pressure_drop_pa(calibration),
        kCalibrationTemperatureK);
}

} // namespace engine_sim_offline::simulation
