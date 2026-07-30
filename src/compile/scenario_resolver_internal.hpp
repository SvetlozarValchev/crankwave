#pragma once

#include "compile/resolution_builder.hpp"
#include "compile/scenario_resolver.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::compile::detail::scenario_resolution {

void append(authoring::DiagnosticReport &destination,
            authoring::DiagnosticReport source);

[[nodiscard]] authoring::DiagnosticCode
diagnostic_code(contract::ContractIssueCode code) noexcept;

[[nodiscard]] std::string json_pointer_from_contract_path(std::string_view path);

void append_contract_report(
    authoring::DiagnosticReport &destination, contract::ValidationReport source,
    std::optional<authoring::DiagnosticCode> forced_code = std::nullopt,
    std::string_view forced_pointer = {});

[[nodiscard]] bool approximately_equal(double left, double right) noexcept;
[[nodiscard]] double radians_per_second_to_rpm(double value) noexcept;
[[nodiscard]] contract::TrajectoryInterpolation
contract_interpolation(authoring::TrajectoryInterpolation value) noexcept;
[[nodiscard]] std::string_view
sample_encoding_id(contract::AudioSampleEncoding encoding) noexcept;

[[nodiscard]] const contract::ResolutionRecord *
find_resolution(const contract::ProvenanceLedger &provenance,
                std::string_view resolution_id) noexcept;

[[nodiscard]] const contract::ResolutionRecord *
find_resolution_path(const contract::ProvenanceLedger &provenance,
                     std::string_view parameter_path) noexcept;

[[nodiscard]] const contract::MethodIdentity &fuel_projection_method_identity();

[[nodiscard]] contract::Sha256Digest
source_matrix_digest(const contract::SourceMatrixContract &matrix);

struct ConvertedQuantityTrajectoryPoint {
    double time_s = 0.0;
    std::uint64_t frame = 0;
    double value = 0.0;
};

struct ConvertedQuantityTrajectory {
    authoring::TrajectoryInterpolation interpolation =
        authoring::TrajectoryInterpolation::right_continuous_hold;
    std::vector<ConvertedQuantityTrajectoryPoint> points;
};

struct SelectedFuel {
    const ResolvedFuelDescriptor *descriptor = nullptr;
    std::string heating_value_source_path;
    std::string molecular_mass_source_path;
    std::string molecular_afr_source_path;
};

class ScenarioResolver final {
  public:
    ScenarioResolver(const authoring::ScenarioDocument &document,
                     const ScenarioResolverContext &context);

    [[nodiscard]] ScenarioResolutionResult resolve();

  private:
    void add(authoring::DiagnosticCode code, std::string_view path,
             std::string message);

    [[nodiscard]] double quantity(const authoring::Quantity &value,
                                  authoring::QuantityDimension dimension,
                                  std::string_view path);

    // Stored speed commands use RPM. Validate through the SI converter, but preserve
    // an authored RPM value instead of adding a lossy rpm->rad/s->rpm round trip.
    [[nodiscard]] double engine_speed_rpm(const authoring::Quantity &value,
                                          std::string_view path);

    [[nodiscard]] contract::RationalRateHz rate(const authoring::RationalRate &value,
                                                std::string_view path);

    [[nodiscard]] std::optional<std::uint64_t> physics_frame(double time_s,
                                                             std::string_view path);

    [[nodiscard]] contract::ScalarTrajectory
    scalar_trajectory(const authoring::ScalarTrajectory &input, std::string_view path,
                      bool require_unit_interval, bool require_current_scheduler_hold);

    [[nodiscard]] contract::ScalarTrajectory
    torque_trajectory(const authoring::QuantityTrajectory &input,
                      std::string_view path);

    [[nodiscard]] ConvertedQuantityTrajectory
    speed_trajectory(const authoring::QuantityTrajectory &input, std::string_view path);

    [[nodiscard]] contract::FixedRateRpmTrajectory
    materialize_rpm_lane(const ConvertedQuantityTrajectory &trajectory);

    void validate_context();
    void compile_common_fields();
    void compile_preparation();
    void compile_operating_state();
    void require_initial_speed(double mode_speed_rpm, std::string_view mode_path);
    void require_executable_initial_crank_angle();
    void compile_mode();

    [[nodiscard]] const ResolvedAudioBusDescriptor *
    find_bus(std::string_view authored_id);
    [[nodiscard]] const contract::RouteSpec *
    find_route(contract::RouteId route_id) const noexcept;
    void compile_output_selection();

    void register_common_provenance();
    void register_provenance();
    [[nodiscard]] std::string resolution_id(std::string_view path);

    template <class T>
    void bind(contract::ResolvedValue<T> &value, std::string_view path) {
        value.resolution_id = resolution_id(path);
    }

    void bind_resolution_ids();
    [[nodiscard]] contract::SourceMatrixContract build_source_matrix();

    const authoring::ScenarioDocument &document_;
    const ScenarioResolverContext &context_;
    authoring::DiagnosticReport report_;
    ResolutionProvenanceBuilder provenance_;
    SelectedFuel selected_fuel_;
    double initial_theta_rad_ = 0.0;
    contract::RenderScenario scenario_;
    std::vector<const ResolvedAudioBusDescriptor *> selected_buses_;
    contract::SourceMatrixContract source_matrix_;
    contract::ProvenanceLedger combined_provenance_;
    ScenarioRequestInputMaterial request_input_;
    std::vector<StableIdAssignment> stable_id_assignments_;
};

} // namespace engine_sim_offline::compile::detail::scenario_resolution
