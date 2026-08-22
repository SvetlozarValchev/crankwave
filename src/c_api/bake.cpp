#include "c_api/c_api_internal.hpp"

#include "determinism/renderer_source_stamp.hpp"
#include "crankwave/authoring/parse.hpp"
#include "crankwave/responsive/directional_cook.hpp"
#include "crankwave/responsive/finite_capture.hpp"
#include "crankwave/responsive/held_texture.hpp"
#include "crankwave/responsive/lifecycle.hpp"
#include "crankwave/responsive/package_children.hpp"
#include "crankwave/responsive/presentation_transfer.hpp"
#include "crankwave/responsive/profile.hpp"
#include "crankwave/responsive/scenario_template.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if defined(__EMSCRIPTEN__)
static_assert(
    sizeof(crankwave_bake_inputs_t) == 104U &&
        offsetof(crankwave_bake_inputs_t, engine_json) == 0U &&
        offsetof(crankwave_bake_inputs_t, assets) == 8U &&
        offsetof(crankwave_bake_inputs_t, shared_starter_runtime_json) == 16U &&
        offsetof(crankwave_bake_inputs_t, shared_starter_audio) == 24U &&
        offsetof(crankwave_bake_inputs_t, release_identity) == 32U &&
        offsetof(crankwave_bake_inputs_t, wasm_module_sha256) == 40U &&
        offsetof(crankwave_bake_inputs_t, asset_catalog_sha256) == 72U,
    "wasm32 CRANKWAVE bake-input ABI layout changed");
static_assert(
    sizeof(crankwave_package_descriptor_t) == 112U &&
        offsetof(crankwave_package_descriptor_t, container_byte_count) == 0U &&
        offsetof(crankwave_package_descriptor_t, engine_id_utf8_bytes) == 40U &&
        offsetof(crankwave_package_descriptor_t, container_sha256) == 48U &&
        offsetof(crankwave_package_descriptor_t, cache_identity_sha256) == 80U,
    "wasm32 CRANKWAVE descriptor ABI layout changed");
static_assert(sizeof(crankwave_package_identity_buffers_t) == 16U,
              "wasm32 CRANKWAVE identity-buffer ABI layout changed");
#endif

namespace crankwave::c_api {
namespace {

struct BakeFailure {
    crankwave_error_code_t code = CRANKWAVE_ERROR_BAKE_COOK;
    std::string detail_code;
    std::string message;
};

template <class Value> using BakeResult = std::variant<Value, BakeFailure>;

[[nodiscard]] std::string_view text(const crankwave_utf8_view_t value) noexcept {
    return {value.data, value.size};
}

[[nodiscard]] std::span<const std::byte> bytes(const crankwave_byte_view_t value) noexcept {
    return {reinterpret_cast<const std::byte *>(value.data), value.size};
}

[[nodiscard]] contract::Sha256Digest digest(const crankwave_sha256_digest_t &value) noexcept {
    contract::Sha256Digest result;
    std::copy(std::begin(value.bytes), std::end(value.bytes), result.bytes.begin());
    return result;
}

[[nodiscard]] crankwave_sha256_digest_t digest(const contract::Sha256Digest &value) noexcept {
    crankwave_sha256_digest_t result{};
    std::copy(value.bytes.begin(), value.bytes.end(), std::begin(result.bytes));
    return result;
}

[[nodiscard]] BakeFailure failure(const crankwave_error_code_t code, std::string detail_code,
                                  std::string message) {
    return {code, std::move(detail_code), std::move(message)};
}

[[nodiscard]] BakeFailure validation_failure(const crankwave_error_code_t code,
                                             std::string detail_code, std::string stage,
                                             const contract::ValidationReport &report) {
    std::string message = std::move(stage) + " was rejected";
    if (!report.issues.empty()) {
        message.append(" at ");
        message.append(report.issues.front().path);
        message.append(": ");
        message.append(report.issues.front().message);
    }
    return failure(code, std::move(detail_code), std::move(message));
}

[[nodiscard]] BakeFailure
capture_failure(std::string stage,
                const responsive::FiniteResponsiveCaptureError &source) {
    if (!source.message.empty()) {
        stage.append(": ");
        stage.append(source.message);
    }
    return failure(CRANKWAVE_ERROR_BAKE_CAPTURE,
                   source.detail_code.empty() ? "responsive-capture-failed"
                                              : source.detail_code,
                   std::move(stage));
}

[[nodiscard]] BakeFailure lifecycle_failure(std::string stage,
                                            const responsive::LifecycleError &source) {
    if (!source.message.empty()) {
        stage.append(": ");
        stage.append(source.message);
    }
    return failure(CRANKWAVE_ERROR_BAKE_COOK,
                   source.detail_code.empty() ? "responsive-lifecycle-failed"
                                              : source.detail_code,
                   std::move(stage));
}

[[nodiscard]] BakeFailure
package_failure(std::string stage,
                const responsive::NativeResponsivePackageError &source) {
    if (!source.message.empty()) {
        stage.append(": ");
        stage.append(source.message);
    }
    return failure(CRANKWAVE_ERROR_BAKE_PACKAGE,
                   source.detail_code.empty() ? "responsive-package-failed"
                                              : source.detail_code,
                   std::move(stage));
}

[[nodiscard]] responsive::HeldCaptureOperatingMode
held_capture_mode(const authoring::EnginePackageDocument &engine) {
    bool direct_controller = false;
    const auto &definition = engine.engine;
    if (definition.throttle_controllers && definition.throttle_controller) {
        const auto selected = std::find_if(
            definition.throttle_controllers->begin(),
            definition.throttle_controllers->end(), [&](const auto &candidate) {
                return candidate.id.value == definition.throttle_controller->value;
            });
        direct_controller =
            selected != definition.throttle_controllers->end() &&
            std::holds_alternative<authoring::DirectThrottleController>(selected->kind);
    }
    const bool master_rod = std::any_of(
        definition.journals.begin(), definition.journals.end(),
        [](const auto &journal) {
            return std::holds_alternative<authoring::MasterRodJournalAttachment>(
                journal.attachment);
        });
    return direct_controller && !master_rod
               ? responsive::HeldCaptureOperatingMode::held_speed
               : responsive::HeldCaptureOperatingMode::held_dyno;
}

[[nodiscard]] BakeResult<compile::CompiledScenario>
compile_scenario(const compile::CompiledEngine &engine,
                 const authoring::ScenarioDocument &document,
                 const std::string_view stage) {
    auto compiled = compile::compile_scenario(engine, document);
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
        std::string message{stage};
        message.append(" compilation failed");
        if (!report->diagnostics.empty()) {
            message.append(": ");
            message.append(report->diagnostics.front().message);
        }
        return failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                       "responsive-scenario-compilation-failed", std::move(message));
    }
    return std::get<compile::CompiledScenario>(std::move(compiled));
}

[[nodiscard]] BakeResult<responsive::FiniteResponsiveCapture>
capture_scenario(const compile::CompiledEngine &engine,
                 const authoring::ScenarioDocument &document,
                 const std::span<const std::string_view> buses,
                 const bool dry_projection, const std::string_view stage) {
    auto scenario = compile_scenario(engine, document, stage);
    if (auto *failed = std::get_if<BakeFailure>(&scenario)) {
        return std::move(*failed);
    }
    auto captured = dry_projection
                        ? responsive::capture_finite_responsive_dry_routes(
                              std::get<compile::CompiledScenario>(scenario), buses)
                        : responsive::capture_finite_responsive_session(
                              std::get<compile::CompiledScenario>(scenario), buses);
    if (const auto *failed =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&captured)) {
        return capture_failure(std::string{stage}, *failed);
    }
    return std::get<responsive::FiniteResponsiveCapture>(std::move(captured));
}

[[nodiscard]] std::vector<std::string_view>
string_views(const std::vector<std::string> &values) {
    std::vector<std::string_view> result;
    result.reserve(values.size());
    for (const auto &value : values) {
        result.push_back(value);
    }
    return result;
}

[[nodiscard]] BakeResult<std::string>
resolve_lifecycle_audition_bus(const compile::CompiledScenario &scenario) {
    auto created =
        create_engine_session(scenario, EngineSessionExecutionKind::finite_scenario);
    const auto *session = std::get_if<EngineSession>(&created);
    if (session == nullptr) {
        return failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                       "responsive-lifecycle-bus-session-create-failed",
                       "could not create the template session used to resolve the "
                       "lifecycle audition bus");
    }
    std::string resolved;
    std::size_t matches = 0U;
    for (const auto &bus : session->descriptor().audio_buses) {
        if (bus.kind == EngineAudioBusKind::engine_audition_master) {
            ++matches;
            resolved = bus.id;
        }
    }
    if (matches != 1U || resolved.empty()) {
        return failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                       "responsive-lifecycle-audition-bus-resolution-invalid",
                       "the template session must expose exactly one engine audition "
                       "master bus");
    }
    return resolved;
}

struct CookedLifecycle {
    responsive::LifecycleCookedPackage package;
    std::uint64_t capture_count = 0U;
};

[[nodiscard]] BakeResult<CookedLifecycle>
cook_lifecycle(const responsive::ResponsiveBakeProfile &profile,
               const authoring::EnginePackageDocument &engine_document,
               const responsive::ResponsiveScenarioTemplate &scenario_template,
               const compile::CompiledEngine &engine,
               const std::string_view audition_bus_id,
               const std::uint64_t maximum_transfer_coefficient_count) {
    const responsive::LifecycleScenarioPlanRequest request{
        profile, engine_document, scenario_template.document,
        scenario_template.identity_sha256, maximum_transfer_coefficient_count};
    const std::array<std::string_view, 1U> buses{audition_bus_id};

    const auto plan =
        [&](const responsive::LifecycleScenarioRole role, const std::string_view label,
            const std::optional<responsive::LifecycleDynamicStarterRelease> release =
                std::nullopt) -> BakeResult<responsive::LifecycleScenarioSpec> {
        auto result = responsive::plan_lifecycle_scenario(request, role, release);
        if (const auto *failed = std::get_if<responsive::LifecycleError>(&result)) {
            return lifecycle_failure("lifecycle " + std::string{label} + " planning",
                                     *failed);
        }
        return std::get<responsive::LifecycleScenarioSpec>(std::move(result));
    };
    const auto capture = [&](const responsive::LifecycleScenarioSpec &spec,
                             const std::string_view label)
        -> BakeResult<responsive::LifecycleCaptureEvidence> {
        auto rendered =
            capture_scenario(engine, spec.scenario, buses, false,
                             "lifecycle " + std::string{label} + " capture");
        if (auto *failed = std::get_if<BakeFailure>(&rendered)) {
            return std::move(*failed);
        }
        auto mapped = responsive::map_lifecycle_capture(
            spec, std::get<responsive::FiniteResponsiveCapture>(std::move(rendered)),
            0U);
        if (const auto *failed = std::get_if<responsive::LifecycleError>(&mapped)) {
            return lifecycle_failure("lifecycle " + std::string{label} + " mapping",
                                     *failed);
        }
        return std::get<responsive::LifecycleCaptureEvidence>(std::move(mapped));
    };

    auto starter_spec_result =
        plan(responsive::LifecycleScenarioRole::starter, "starter");
    if (auto *failed = std::get_if<BakeFailure>(&starter_spec_result)) {
        return std::move(*failed);
    }
    auto starter_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(starter_spec_result));
    auto starter_capture_result = capture(starter_spec, "starter");
    if (auto *failed = std::get_if<BakeFailure>(&starter_capture_result)) {
        return std::move(*failed);
    }
    auto starter_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(starter_capture_result));

    auto probe_spec_result =
        plan(responsive::LifecycleScenarioRole::startup_probe, "startup probe");
    if (auto *failed = std::get_if<BakeFailure>(&probe_spec_result)) {
        return std::move(*failed);
    }
    auto probe_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(probe_spec_result));
    auto probe_capture_result = capture(probe_spec, "startup probe");
    if (auto *failed = std::get_if<BakeFailure>(&probe_capture_result)) {
        return std::move(*failed);
    }
    auto release_result = responsive::choose_dynamic_starter_release(
        std::get<responsive::LifecycleCaptureEvidence>(probe_capture_result),
        profile.rpm.outer_minimum_rpm);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&release_result)) {
        return lifecycle_failure("lifecycle starter release selection", *failed);
    }
    auto release =
        std::get<responsive::LifecycleDynamicStarterRelease>(std::move(release_result));

    auto startup_spec_result =
        plan(responsive::LifecycleScenarioRole::startup, "startup", release);
    if (auto *failed = std::get_if<BakeFailure>(&startup_spec_result)) {
        return std::move(*failed);
    }
    auto startup_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(startup_spec_result));
    auto startup_capture_result = capture(startup_spec, "startup");
    if (auto *failed = std::get_if<BakeFailure>(&startup_capture_result)) {
        return std::move(*failed);
    }
    auto startup_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(startup_capture_result));

    auto shutdown_spec_result =
        plan(responsive::LifecycleScenarioRole::shutdown, "shutdown");
    if (auto *failed = std::get_if<BakeFailure>(&shutdown_spec_result)) {
        return std::move(*failed);
    }
    auto shutdown_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(shutdown_spec_result));
    auto shutdown_capture_result = capture(shutdown_spec, "shutdown");
    if (auto *failed = std::get_if<BakeFailure>(&shutdown_capture_result)) {
        return std::move(*failed);
    }
    auto shutdown_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(shutdown_capture_result));

    auto elevated_spec_result =
        plan(responsive::LifecycleScenarioRole::shutdown_elevated, "elevated shutdown");
    if (auto *failed = std::get_if<BakeFailure>(&elevated_spec_result)) {
        return std::move(*failed);
    }
    auto elevated_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(elevated_spec_result));
    auto elevated_capture_result = capture(elevated_spec, "elevated shutdown");
    if (auto *failed = std::get_if<BakeFailure>(&elevated_capture_result)) {
        return std::move(*failed);
    }
    auto elevated_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(elevated_capture_result));

    auto cooked_starter = responsive::cook_lifecycle_starter(starter_capture);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&cooked_starter)) {
        return lifecycle_failure("lifecycle starter cooking", *failed);
    }
    const auto &starter = std::get<responsive::LifecycleCookedStarter>(cooked_starter);
    auto startup = responsive::cook_lifecycle_startup(startup_spec, startup_capture,
                                                      starter_capture, starter);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&startup)) {
        return lifecycle_failure("lifecycle startup cooking", *failed);
    }
    auto shutdown =
        responsive::cook_lifecycle_shutdown(shutdown_spec, shutdown_capture);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&shutdown)) {
        return lifecycle_failure("lifecycle shutdown cooking", *failed);
    }
    auto elevated =
        responsive::cook_lifecycle_elevated_shutdown(elevated_spec, elevated_capture);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&elevated)) {
        return lifecycle_failure("lifecycle elevated shutdown cooking", *failed);
    }
    auto admission =
        responsive::make_lifecycle_startup_admission_seed(profile, release);
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&admission)) {
        return lifecycle_failure("lifecycle startup admission", *failed);
    }
    auto assembled = responsive::assemble_lifecycle_package(
        std::move(starter_capture), std::move(startup_capture),
        std::move(shutdown_capture), std::move(elevated_capture), starter,
        std::get<responsive::LifecycleStartupPresentation>(std::move(startup)),
        std::get<responsive::LifecycleShutdownPresentation>(std::move(shutdown)),
        std::get<responsive::LifecycleShutdownPresentation>(std::move(elevated)),
        std::get<responsive::LifecycleStartupAdmissionSeed>(std::move(admission)));
    if (const auto *failed = std::get_if<responsive::LifecycleError>(&assembled)) {
        return lifecycle_failure("lifecycle package assembly", *failed);
    }
    return CookedLifecycle{
        std::get<responsive::LifecycleCookedPackage>(std::move(assembled)), 4U};
}

[[nodiscard]] std::string asset_kind_name(const compile::AssetKind kind) {
    switch (kind) {
    case compile::AssetKind::audio:
        return "audio";
    case compile::AssetKind::accessory_configuration:
        return "accessory-configuration";
    }
    return "unknown";
}

[[nodiscard]] contract::Sha256Digest
authority_digest(const std::string_view schema,
                 const std::span<const std::string_view> records) {
    std::vector<std::byte> preimage;
    const auto append = [&](const std::string_view value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            preimage.push_back(static_cast<std::byte>(
                (static_cast<std::uint64_t>(value.size()) >> shift) & UINT64_C(0xff)));
        }
        for (const unsigned char byte : value) {
            preimage.push_back(static_cast<std::byte>(byte));
        }
    };
    append(schema);
    for (const auto record : records) {
        append(record);
    }
    return contract::sha256(preimage);
}

[[nodiscard]] contract::Sha256Digest wasm_method_authority() {
    using namespace responsive;
    const std::array methods{
        kEngineRedlineAffineProfilePolicyId,
        kResponsiveProfileIdentityMethodId,
        kResponsiveScenarioTemplateIdentityMethodId,
        kHeldCaptureMethodId,
        kHeldDecompositionMethodId,
        kHeldPhaseAlignmentMethodId,
        kHeldStateIdentityMethodId,
        kHeldTextureIdentityMethodId,
        kHeldGridIdentityMethodId,
        kHeldLoadCoalescingMethodId,
        kDirectionalCaptureMethodId,
        kDirectionalSeamAlgorithmId,
        kDirectionalScenarioIdentityMethodId,
        kDirectionalCellIdentityMethodId,
        kDirectionalCaptureIdentityMethodId,
        kDirectionalModelIdentityMethodId,
        kDirectionalLoadCoalescingMethodId,
        kLifecycleScenarioIdentityMethodId,
        kResponsivePresentationIdentityMethodId,
        kResponsiveFixedTransferKind,
        kResponsivePartitionedTransferKind,
        kResponsiveTransferSpectrumEncoding,
    };
    return authority_digest("crankwave.wasm-responsive-method-authority.v1",
                            methods);
}

[[nodiscard]] contract::Sha256Digest wasm_bake_recipe() {
    const std::array recipe{
        std::string_view{"engine-redline-affine-full-11x3-held-grid-v1"},
        std::string_view{"full-six-directional-captures-v1"},
        std::string_view{"starter-probe-startup-shutdown-elevated-lifecycle-v1"},
        std::string_view{"compiled-authored-presentation-transfer-v1"},
        std::string_view{"shared-recorded-starter-exact-two-member-bundle-v1"},
        std::string_view{"single-worker-canonical-result-order-v1"},
        std::string_view{"verified-in-memory-crankwave-v1"},
    };
    return authority_digest("crankwave.wasm-responsive-bake-recipe.v1",
                            recipe);
}

[[nodiscard]] BakeResult<CrankwaveEntry>
bake_crankwave(const authoring::EnginePackageDocument &document,
                   const std::span<const compile::AssetPayloadView> assets,
                   const std::span<const std::byte> engine_source,
                   const std::span<const std::byte> starter_runtime,
                   const std::span<const std::byte> starter_audio,
                   const std::string_view release_identity,
                   const contract::Sha256Digest &wasm_module_sha256,
                   const contract::Sha256Digest &asset_catalog_sha256) {
    auto selected = responsive::derive_engine_redline_affine_profile(document);
    if (const auto *report = std::get_if<contract::ValidationReport>(&selected)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_PROFILE,
                                  "responsive-profile-selection-failed",
                                  "responsive profile selection", *report);
    }
    auto profile = std::get<responsive::ResponsiveBakeProfile>(std::move(selected));
    auto template_result =
        responsive::make_responsive_scenario_template(document, profile);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&template_result)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                                  "responsive-scenario-template-failed",
                                  "responsive scenario template", *report);
    }
    auto scenario_template =
        std::get<responsive::ResponsiveScenarioTemplate>(std::move(template_result));

    auto compiled_result = compile::compile_engine(document, assets);
    if (const auto *report =
            std::get_if<authoring::DiagnosticReport>(&compiled_result)) {
        std::string message = "engine compilation failed during responsive bake";
        if (!report->diagnostics.empty()) {
            message.append(": ");
            message.append(report->diagnostics.front().message);
        }
        return failure(CRANKWAVE_ERROR_BAKE_SCENARIO, "responsive-engine-compilation-failed",
                       std::move(message));
    }
    auto engine = std::get<compile::CompiledEngine>(std::move(compiled_result));
    auto template_scenario_result =
        compile_scenario(engine, scenario_template.document, "presentation template");
    if (auto *failed = std::get_if<BakeFailure>(&template_scenario_result)) {
        return std::move(*failed);
    }
    const auto &template_scenario =
        std::get<compile::CompiledScenario>(template_scenario_result);
    auto presentation_result =
        responsive::compile_responsive_presentation_transfer(template_scenario);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&presentation_result)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                                  "responsive-presentation-compilation-failed",
                                  "responsive presentation compilation", *report);
    }
    auto presentation = std::get<responsive::ResponsiveCompiledPresentation>(
        std::move(presentation_result));
    const auto maximum_transfer_coefficient_count =
        std::max_element(presentation.transfers.begin(), presentation.transfers.end(),
                         [](const auto &left, const auto &right) {
                             return left.coefficient_count < right.coefficient_count;
                         })
            ->coefficient_count;
    const auto dry_bus_views = string_views(presentation.audition_dry_bus_order);
    auto lifecycle_bus_result = resolve_lifecycle_audition_bus(template_scenario);
    if (auto *failed = std::get_if<BakeFailure>(&lifecycle_bus_result)) {
        return std::move(*failed);
    }

    auto held_plan = responsive::plan_held_state_scenarios(
        {profile, scenario_template.document, scenario_template.identity_sha256,
         held_capture_mode(document)});
    if (const auto *report = std::get_if<contract::ValidationReport>(&held_plan)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                                  "responsive-held-planning-failed",
                                  "responsive held planning", *report);
    }
    auto held_specs =
        std::get<std::vector<responsive::HeldStateScenarioSpec>>(std::move(held_plan));
    std::vector<responsive::HeldCookedCell> held_cells;
    held_cells.reserve(held_specs.size());
    for (const auto &spec : held_specs) {
        auto captured = capture_scenario(engine, spec.scenario, dry_bus_views, true,
                                         "held " + spec.id);
        if (auto *failed = std::get_if<BakeFailure>(&captured)) {
            return std::move(*failed);
        }
        auto cooked = responsive::cook_held_state(
            spec, std::get<responsive::FiniteResponsiveCapture>(std::move(captured)));
        if (const auto *report = std::get_if<contract::ValidationReport>(&cooked)) {
            return validation_failure(CRANKWAVE_ERROR_BAKE_COOK,
                                      "responsive-held-cook-failed",
                                      "responsive held cooking", *report);
        }
        held_cells.push_back(std::get<responsive::HeldCookedCell>(std::move(cooked)));
    }
    auto held_grid_result = responsive::cook_held_texture_grid(profile, held_cells);
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&held_grid_result)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_COOK, "responsive-held-grid-failed",
                                  "responsive held grid cooking", *report);
    }
    auto held_grid = std::get<responsive::HeldCookedGrid>(std::move(held_grid_result));

    auto directional_plan = responsive::plan_directional_sweep_scenarios(
        {profile, scenario_template.document, scenario_template.identity_sha256});
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&directional_plan)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_SCENARIO,
                                  "responsive-directional-planning-failed",
                                  "responsive directional planning", *report);
    }
    auto directional_specs =
        std::get<std::vector<responsive::DirectionalSweepScenarioSpec>>(
            std::move(directional_plan));
    const auto directional_capture_count = directional_specs.size();
    std::vector<responsive::DirectionalCookedCapture> directional_captures;
    directional_captures.reserve(directional_specs.size());
    for (const auto &spec : directional_specs) {
        auto captured = capture_scenario(engine, spec.scenario, dry_bus_views, true,
                                         "directional " + spec.id);
        if (auto *failed = std::get_if<BakeFailure>(&captured)) {
            return std::move(*failed);
        }
        auto cooked = responsive::cook_directional_capture(
            spec, std::get<responsive::FiniteResponsiveCapture>(std::move(captured)));
        if (const auto *report = std::get_if<contract::ValidationReport>(&cooked)) {
            return validation_failure(CRANKWAVE_ERROR_BAKE_COOK,
                                      "responsive-directional-cook-failed",
                                      "responsive directional cooking", *report);
        }
        directional_captures.push_back(
            std::get<responsive::DirectionalCookedCapture>(std::move(cooked)));
    }
    auto directional_model_result = responsive::assemble_directional_model(
        profile, std::move(directional_captures));
    if (const auto *report =
            std::get_if<contract::ValidationReport>(&directional_model_result)) {
        return validation_failure(CRANKWAVE_ERROR_BAKE_COOK,
                                  "responsive-directional-model-failed",
                                  "responsive directional model assembly", *report);
    }
    auto directional_model = std::get<responsive::DirectionalCookedModel>(
        std::move(directional_model_result));

    auto lifecycle_result = cook_lifecycle(profile, document, scenario_template, engine,
                                           std::get<std::string>(lifecycle_bus_result),
                                           maximum_transfer_coefficient_count);
    if (auto *failed = std::get_if<BakeFailure>(&lifecycle_result)) {
        return std::move(*failed);
    }
    auto lifecycle = std::get<CookedLifecycle>(std::move(lifecycle_result));

    auto source_closure_result = determinism::renderer_source_closure();
    const auto *source_closure =
        std::get_if<determinism::RendererSourceClosure>(&source_closure_result);
    if (source_closure == nullptr) {
        return failure(CRANKWAVE_ERROR_BAKE_IDENTITY, "responsive-source-closure-unavailable",
                       "the embedded renderer source identity is unavailable");
    }
    const responsive::ResponsivePackageProvenanceV1 provenance{
        std::string{engine.id()}, engine.provenance().bundle.sha256,
        "crankwave-renderer-build", source_closure->source_closure_sha256};
    responsive::ResponsivePackageChildrenViewV1 children;
    children.profile = &profile;
    children.held = &held_grid;
    children.directional = &directional_model;
    children.presentation = &presentation;
    children.provenance = provenance;
    children.canonical_offline_bake = false;
    auto core_result = responsive::encode_responsive_package_children_v1(children);
    if (const auto *failed =
            std::get_if<responsive::NativeResponsivePackageError>(&core_result)) {
        return package_failure("responsive child encoding", *failed);
    }
    auto core = std::get<responsive::EncodedResponsivePackageChildrenV1>(
        std::move(core_result));

    auto starter_result = responsive::encode_responsive_shared_recorded_starter_v1(
        {starter_runtime, starter_audio}, provenance);
    if (const auto *failed =
            std::get_if<responsive::NativeResponsivePackageError>(&starter_result)) {
        return package_failure("shared recorded starter encoding", *failed);
    }
    auto shared_starter =
        std::get<responsive::EncodedResponsiveSharedRecordedStarterV1>(
            std::move(starter_result));

    auto lifecycle_child_result = responsive::encode_responsive_lifecycle_child_v1(
        lifecycle.package, provenance, core.held_manifest_sha256);
    if (const auto *failed = std::get_if<responsive::NativeResponsivePackageError>(
            &lifecycle_child_result)) {
        return package_failure("lifecycle child encoding", *failed);
    }
    std::vector<responsive::ResponsiveOptionalChildPackageV1> optional_children;
    optional_children.reserve(2U);
    optional_children.push_back(std::get<responsive::ResponsiveOptionalChildPackageV1>(
        std::move(lifecycle_child_result)));
    optional_children.push_back(std::move(shared_starter.child));
    auto attached_result = responsive::attach_responsive_optional_children_v1(
        std::move(core), std::move(optional_children));
    if (const auto *failed =
            std::get_if<responsive::NativeResponsivePackageError>(&attached_result)) {
        return package_failure("responsive child attachment", *failed);
    }

    responsive::NativeResponsiveBakeIdentityInputV1 identity;
    identity.backend.kind = std::string{responsive::kWasmResponsiveBackendKindV1};
    identity.backend.release_identity = std::string{release_identity};
    identity.backend.c_api_version = CRANKWAVE_C_API_VERSION;
    identity.backend.target = std::string{responsive::kWasmResponsiveTargetV1};
    identity.backend.numeric_runtime =
        std::string{responsive::kWasmResponsiveNumericRuntimeV1};
    identity.backend.executable_sha256 = wasm_module_sha256;
    identity.backend.source_closure_sha256 = source_closure->source_closure_sha256;
    identity.backend.method_registry_sha256 = wasm_method_authority();
    identity.engine_id = std::string{engine.id()};
    identity.engine_source_sha256 = contract::sha256(engine_source);
    identity.profile_id = profile.id;
    identity.profile_sha256 = profile.selection_identity_sha256;
    identity.bake_recipe_sha256 = wasm_bake_recipe();
    identity.builtin_asset_catalog_sha256 = asset_catalog_sha256;
    identity.resolved_assets.reserve(assets.size());
    for (const auto &asset : assets) {
        identity.resolved_assets.push_back({asset_kind_name(asset.kind),
                                            std::string{asset.asset_id},
                                            contract::sha256(asset.bytes)});
    }
    identity.shared_recorded_starter = shared_starter.identity;

    auto built_result = responsive::build_native_responsive_package_from_encoded_v2(
        {std::move(identity),
         std::get<responsive::EncodedResponsivePackageChildrenV1>(
             std::move(attached_result)),
         std::nullopt});
    if (const auto *failed =
            std::get_if<responsive::NativeResponsivePackageError>(&built_result)) {
        return package_failure("CRANKWAVE construction", *failed);
    }
    auto built =
        std::get<responsive::NativeResponsiveCookedPackageV2>(std::move(built_result));
    CrankwaveEntry result;
    result.bytes = std::move(built.package.crankwave_v1);
    result.engine_id = std::string{engine.id()};
    result.profile_id = profile.id;
    result.entry_count = built.package.members.size();
    result.held_cell_count = held_grid.cells.size();
    result.directional_capture_count = directional_capture_count;
    result.lifecycle_capture_count = lifecycle.capture_count;
    result.container_sha256 = built.package.crankwave_sha256;
    result.cache_identity_sha256 = built.package.cache_identity.sha256;
    return result;
}

[[nodiscard]] std::optional<compile::AssetKind>
asset_kind(const crankwave_asset_kind_t kind) noexcept {
    switch (kind) {
    case CRANKWAVE_ASSET_AUDIO:
        return compile::AssetKind::audio;
    case CRANKWAVE_ASSET_ACCESSORY_CONFIGURATION:
        return compile::AssetKind::accessory_configuration;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] crankwave_status_t invalid_argument(crankwave_context &context, std::string message) {
    return set_error(context, CRANKWAVE_STATUS_INVALID_ARGUMENT, CRANKWAVE_ERROR_STAGE_ARGUMENT,
                     CRANKWAVE_ERROR_INVALID_POINTER, "c-api-invalid-bake-argument",
                     std::move(message));
}

[[nodiscard]] crankwave_status_t invalid_handle(crankwave_context &context, std::string message) {
    return set_error(context, CRANKWAVE_STATUS_INVALID_HANDLE, CRANKWAVE_ERROR_STAGE_HANDLE,
                     CRANKWAVE_ERROR_INVALID_HANDLE, "c-api-invalid-handle",
                     std::move(message));
}

} // namespace
} // namespace crankwave::c_api

extern "C" {

crankwave_status_t
crankwave_bake_package(crankwave_context_t *const context,
                       const crankwave_bake_inputs_t *const inputs,
                       crankwave_package_handle_t *const out_crankwave) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave;
        using namespace crankwave::c_api;
        if (inputs == nullptr || out_crankwave == nullptr) {
            return invalid_argument(*context,
                                    "bake inputs and output handle must not be null");
        }
        *out_crankwave = CRANKWAVE_INVALID_HANDLE;
        if (!valid(inputs->engine_json) ||
            !valid(inputs->shared_starter_runtime_json) ||
            !valid(inputs->shared_starter_audio) || !valid(inputs->release_identity) ||
            (inputs->assets == nullptr && inputs->asset_count != 0U)) {
            return invalid_argument(
                *context, "bake input pointers must be non-null for nonzero extents");
        }
        if (inputs->engine_json.size == 0U ||
            inputs->shared_starter_runtime_json.size == 0U ||
            inputs->shared_starter_audio.size == 0U ||
            inputs->release_identity.size == 0U) {
            return invalid_argument(
                *context, "engine, starter, and release inputs must not be empty");
        }
        const auto module_sha256 = digest(inputs->wasm_module_sha256);
        const auto catalog_sha256 = digest(inputs->asset_catalog_sha256);
        if (module_sha256.is_zero() || catalog_sha256.is_zero()) {
            return invalid_argument(
                *context, "module and asset-catalog identities must be nonzero");
        }

        std::vector<compile::AssetPayloadView> asset_views;
        asset_views.reserve(inputs->asset_count);
        for (std::size_t index = 0U; index < inputs->asset_count; ++index) {
            const auto &asset = inputs->assets[index];
            const auto kind = asset_kind(asset.kind);
            if (!kind.has_value()) {
                return set_error(*context, CRANKWAVE_STATUS_INVALID_ARGUMENT,
                                 CRANKWAVE_ERROR_STAGE_ARGUMENT, CRANKWAVE_ERROR_INVALID_ENUM,
                                 "c-api-invalid-asset-kind",
                                 "bake asset has an unknown kind");
            }
            if (!valid(asset.asset_id) || !valid(asset.bytes)) {
                return invalid_argument(
                    *context, "bake asset pointer is null for a nonzero extent");
            }
            asset_views.push_back({*kind, text(asset.asset_id), bytes(asset.bytes)});
        }

        auto parsed = authoring::parse_engine_document(text(inputs->engine_json));
        if (auto *report = std::get_if<authoring::DiagnosticReport>(&parsed)) {
            return set_diagnostics(*context, CRANKWAVE_STATUS_ENGINE_PARSE_FAILED,
                                   CRANKWAVE_ERROR_STAGE_ENGINE_PARSE, "engine-json-invalid",
                                   "engine JSON parsing or schema validation failed",
                                   std::move(*report));
        }
        const auto engine_bytes = std::span<const std::byte>{
            reinterpret_cast<const std::byte *>(inputs->engine_json.data),
            inputs->engine_json.size};
        auto baked = bake_crankwave(
            std::get<authoring::EnginePackageDocument>(parsed), asset_views,
            engine_bytes, bytes(inputs->shared_starter_runtime_json),
            bytes(inputs->shared_starter_audio), text(inputs->release_identity),
            module_sha256, catalog_sha256);
        if (auto *failed = std::get_if<BakeFailure>(&baked)) {
            return set_error(*context, CRANKWAVE_STATUS_BAKE_FAILED, CRANKWAVE_ERROR_STAGE_BAKE,
                             failed->code, std::move(failed->detail_code),
                             std::move(failed->message));
        }
        *out_crankwave = context->crankwaves.insert(
            std::get<CrankwaveEntry>(std::move(baked)));
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t
crankwave_destroy_package(crankwave_context_t *const context,
                          const crankwave_package_handle_t crankwave) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (!context->crankwaves.erase(crankwave)) {
            return invalid_handle(
                *context, "CRANKWAVE handle is stale, invalid, or wrong-kind");
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_package_get_descriptor(
    crankwave_context_t *const context, const crankwave_package_handle_t crankwave,
    crankwave_package_descriptor_t *const out_descriptor) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (out_descriptor == nullptr) {
            return invalid_argument(*context, "descriptor output must not be null");
        }
        const auto *entry = context->crankwaves.get(crankwave);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "CRANKWAVE handle is stale, invalid, or wrong-kind");
        }
        *out_descriptor = {
            entry->bytes.size(),
            entry->entry_count,
            entry->held_cell_count,
            entry->directional_capture_count,
            entry->lifecycle_capture_count,
            entry->engine_id.size(),
            entry->profile_id.size(),
            digest(entry->container_sha256),
            digest(entry->cache_identity_sha256),
        };
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t crankwave_package_copy_identity(
    crankwave_context_t *const context, const crankwave_package_handle_t crankwave,
    crankwave_package_identity_buffers_t *const buffers) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (buffers == nullptr) {
            return invalid_argument(*context, "identity buffers must not be null");
        }
        const auto *entry = context->crankwaves.get(crankwave);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "CRANKWAVE handle is stale, invalid, or wrong-kind");
        }
        const auto fits = [](const std::string_view value,
                             const crankwave_mutable_utf8_buffer_t buffer) {
            return (buffer.data == nullptr && buffer.capacity == 0U) ||
                   (buffer.data != nullptr && buffer.capacity > value.size());
        };
        if (!fits(entry->engine_id, buffers->engine_id) ||
            !fits(entry->profile_id, buffers->profile_id)) {
            return set_error(*context, CRANKWAVE_STATUS_BUFFER_TOO_SMALL,
                             CRANKWAVE_ERROR_STAGE_ARGUMENT, CRANKWAVE_ERROR_BUFFER_CAPACITY,
                             "c-api-crankwave-identity-buffer-too-small",
                             "CRANKWAVE identity output buffer is too small");
        }
        (void)copy_text(entry->engine_id, buffers->engine_id);
        (void)copy_text(entry->profile_id, buffers->profile_id);
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

crankwave_status_t
crankwave_package_copy_bytes(crankwave_context_t *const context,
                             const crankwave_package_handle_t crankwave,
                             uint8_t *const output, const size_t capacity,
                             size_t *const out_byte_count) noexcept {
    if (context == nullptr) {
        return CRANKWAVE_STATUS_INVALID_ARGUMENT;
    }
    return crankwave::c_api::boundary(*context, [&]() -> crankwave_status_t {
        using namespace crankwave::c_api;
        if (out_byte_count == nullptr || (output == nullptr && capacity != 0U)) {
            return invalid_argument(*context, "carrier output arguments are invalid");
        }
        const auto *entry = context->crankwaves.get(crankwave);
        if (entry == nullptr) {
            return invalid_handle(
                *context, "CRANKWAVE handle is stale, invalid, or wrong-kind");
        }
        *out_byte_count = entry->bytes.size();
        if (output == nullptr) {
            clear_error(*context);
            return CRANKWAVE_STATUS_OK;
        }
        if (capacity < entry->bytes.size()) {
            return set_error(*context, CRANKWAVE_STATUS_BUFFER_TOO_SMALL,
                             CRANKWAVE_ERROR_STAGE_ARGUMENT, CRANKWAVE_ERROR_BUFFER_CAPACITY,
                             "c-api-crankwave-buffer-too-small",
                             "CRANKWAVE carrier output buffer is too small");
        }
        if (!entry->bytes.empty()) {
            std::memcpy(output, entry->bytes.data(), entry->bytes.size());
        }
        clear_error(*context);
        return CRANKWAVE_STATUS_OK;
    });
}

} // extern "C"
