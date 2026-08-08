#include "bake_revengine_command.hpp"

#include "bake_revengine_command_support.hpp"
#include "native_input_files.hpp"
#include "native_input_files_support.hpp"
#include "native_responsive_bake_identity.hpp"

#include "contract/sha256_stream.hpp"
#include "determinism/renderer_determinism_envelope.hpp"
#include "engine_sim_offline/artifacts/revengine_container.hpp"
#include "engine_sim_offline/authoring/json.hpp"
#include "engine_sim_offline/c_api.h"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/responsive/directional_cook.hpp"
#include "engine_sim_offline/responsive/finite_capture.hpp"
#include "engine_sim_offline/responsive/held_texture.hpp"
#include "engine_sim_offline/responsive/lifecycle.hpp"
#include "engine_sim_offline/responsive/native_package.hpp"
#include "engine_sim_offline/responsive/native_publication.hpp"
#include "engine_sim_offline/responsive/package_children.hpp"
#include "engine_sim_offline/responsive/presentation_transfer.hpp"
#include "engine_sim_offline/responsive/profile.hpp"
#include "engine_sim_offline/responsive/scenario_template.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::cli {
namespace {

using Error = BakeRevengineError;
using ErrorKind = BakeRevengineErrorKind;

[[nodiscard]] Error error(ErrorKind kind, std::string code, std::string stage,
                          std::string message, std::filesystem::path path = {}) {
    Error result;
    result.kind = kind;
    result.code = std::move(code);
    result.stage = std::move(stage);
    result.path = std::move(path);
    result.message = std::move(message);
    return result;
}

[[nodiscard]] Error cancelled(const std::string_view stage) {
    return error(ErrorKind::cancelled, "bake-revengine-cancelled", std::string{stage},
                 "native responsive bake was cancelled");
}

[[nodiscard]] Error validation_error(std::string code, std::string stage,
                                     contract::ValidationReport report) {
    auto result = error(ErrorKind::data_error, std::move(code), std::move(stage),
                        "trusted responsive planning or cooking was rejected");
    result.validation = std::move(report);
    return result;
}

[[nodiscard]] Error diagnostic_error(std::string stage,
                                     authoring::DiagnosticReport report) {
    auto kind = ErrorKind::data_error;
    for (const auto &diagnostic : report.diagnostics) {
        if (diagnostic.severity != authoring::DiagnosticSeverity::error) {
            continue;
        }
        if (diagnostic.code == authoring::DiagnosticCode::internal_failure) {
            kind = ErrorKind::software;
            break;
        }
        if (diagnostic.code == authoring::DiagnosticCode::unsupported_capability) {
            kind = ErrorKind::unavailable;
        }
    }
    auto result = error(kind, "authoring-diagnostics", std::move(stage),
                        "native responsive scenario compilation failed");
    result.diagnostics = std::move(report);
    return result;
}

[[nodiscard]] Error input_error(const std::string_view stage,
                                const NativeInputError &source) {
    ErrorKind kind = ErrorKind::software;
    switch (source.kind) {
    case NativeInputErrorKind::data_error:
        kind = ErrorKind::data_error;
        break;
    case NativeInputErrorKind::no_input:
        kind = ErrorKind::no_input;
        break;
    case NativeInputErrorKind::unavailable:
        kind = ErrorKind::unavailable;
        break;
    case NativeInputErrorKind::software:
        kind = ErrorKind::software;
        break;
    }
    auto result = error(kind, std::string{native_input_error_code_label(source.code)},
                        std::string{stage}, source.message, source.path);
    result.diagnostics = source.diagnostics;
    return result;
}

[[nodiscard]] Error output_error(const NativeOutputError &source) {
    return error(detail::bake_output_error_kind(source.kind),
                 std::string{native_output_error_code_label(source.code)},
                 "output preflight", source.message, source.path);
}

[[nodiscard]] Error capture_error(const std::string_view stage,
                                  const responsive::FiniteResponsiveCaptureError &e) {
    const auto kind =
        e.code == responsive::FiniteResponsiveCaptureErrorCode::cancelled
            ? ErrorKind::cancelled
            : (e.code == responsive::FiniteResponsiveCaptureErrorCode::invalid_request
                   ? ErrorKind::data_error
                   : ErrorKind::software);
    return error(kind,
                 e.detail_code.empty() ? "responsive-capture-failed" : e.detail_code,
                 std::string{stage}, e.message);
}

[[nodiscard]] Error lifecycle_error(const std::string_view stage,
                                    const responsive::LifecycleError &e) {
    const auto kind = e.code == responsive::LifecycleErrorCode::cancelled
                          ? ErrorKind::cancelled
                          : (e.code == responsive::LifecycleErrorCode::invalid_request
                                 ? ErrorKind::data_error
                                 : ErrorKind::software);
    return error(kind,
                 e.detail_code.empty() ? "responsive-lifecycle-failed" : e.detail_code,
                 std::string{stage}, e.message, e.path);
}

[[nodiscard]] Error package_error(const std::string_view stage,
                                  const responsive::NativeResponsivePackageError &e) {
    ErrorKind kind = ErrorKind::software;
    switch (e.code) {
    case responsive::NativeResponsivePackageErrorCode::cancelled:
        kind = ErrorKind::cancelled;
        break;
    case responsive::NativeResponsivePackageErrorCode::publication_failure:
    case responsive::NativeResponsivePackageErrorCode::output_conflict:
        kind = ErrorKind::cant_create;
        break;
    case responsive::NativeResponsivePackageErrorCode::unsupported_platform:
        kind = ErrorKind::unavailable;
        break;
    case responsive::NativeResponsivePackageErrorCode::invalid_argument:
    case responsive::NativeResponsivePackageErrorCode::invalid_identity:
    case responsive::NativeResponsivePackageErrorCode::invalid_member:
    case responsive::NativeResponsivePackageErrorCode::duplicate_member:
    case responsive::NativeResponsivePackageErrorCode::missing_member:
    case responsive::NativeResponsivePackageErrorCode::malformed_child_manifest:
    case responsive::NativeResponsivePackageErrorCode::topology_mismatch:
    case responsive::NativeResponsivePackageErrorCode::resource_limit:
    case responsive::NativeResponsivePackageErrorCode::pack_failure:
        kind = ErrorKind::software;
        break;
    }
    return error(kind,
                 e.detail_code.empty() ? "native-responsive-package-failed"
                                       : e.detail_code,
                 std::string{stage}, e.message, e.path);
}

struct ExecutableSnapshot {
    std::uint64_t byte_count = 0U;
    contract::Sha256Digest sha256;
};

using ExecutableSnapshotResult = std::variant<ExecutableSnapshot, Error>;

[[nodiscard]] ExecutableSnapshotResult
snapshot_current_executable(const std::stop_token stop_token) {
#if !defined(__linux__)
    static_cast<void>(stop_token);
    return error(
        ErrorKind::unavailable, "native-responsive-executable-identity-unavailable",
        "backend identity", "exact native executable identity requires Linux /proc");
#else
    constexpr std::uint64_t maximum_bytes = UINT64_C(512) * 1024U * 1024U;
    const int descriptor = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return error(ErrorKind::unavailable, "native-responsive-executable-open-failed",
                     "backend identity",
                     std::string{"could not open the running executable: "} +
                         std::strerror(errno),
                     "/proc/self/exe");
    }
    struct DescriptorGuard final {
        int value;
        ~DescriptorGuard() {
            static_cast<void>(::close(value));
        }
    } guard{descriptor};

    struct stat before{};
    if (::fstat(descriptor, &before) != 0 || !S_ISREG(before.st_mode) ||
        before.st_size <= 0 ||
        static_cast<std::uint64_t>(before.st_size) > maximum_bytes) {
        return error(
            ErrorKind::unavailable, "native-responsive-executable-metadata-invalid",
            "backend identity", "running executable is not a bounded regular file",
            "/proc/self/exe");
    }
    contract::detail::Sha256Stream hash;
    std::array<std::byte, 256U * 1024U> buffer{};
    std::uint64_t offset = 0U;
    while (offset < static_cast<std::uint64_t>(before.st_size)) {
        if (stop_token.stop_requested()) {
            return cancelled("backend identity");
        }
        const auto remaining = static_cast<std::uint64_t>(before.st_size) - offset;
        const auto request =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const auto count =
            ::pread(descriptor, buffer.data(), request, static_cast<off_t>(offset));
        if (count > 0) {
            const auto received = static_cast<std::size_t>(count);
            hash.update(std::span<const std::byte>{buffer}.first(received));
            offset += received;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return error(ErrorKind::unavailable,
                         "native-responsive-executable-read-failed", "backend identity",
                         "running executable could not be read completely",
                         "/proc/self/exe");
        }
    }
    struct stat after{};
    if (::fstat(descriptor, &after) != 0 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec) {
        return error(ErrorKind::unavailable, "native-responsive-executable-changed",
                     "backend identity",
                     "running executable identity changed while it was hashed",
                     "/proc/self/exe");
    }
    return ExecutableSnapshot{offset, hash.finish()};
#endif
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

using CompiledScenarioResult = std::variant<compile::CompiledScenario, Error>;

[[nodiscard]] CompiledScenarioResult
compile_trusted_scenario(const compile::CompiledEngine &engine,
                         const authoring::ScenarioDocument &document, std::string stage,
                         const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled(stage);
    }
    auto compiled = compile::compile_scenario(engine, document);
    if (auto *report = std::get_if<authoring::DiagnosticReport>(&compiled)) {
        return diagnostic_error(std::move(stage), std::move(*report));
    }
    return std::get<compile::CompiledScenario>(std::move(compiled));
}

using CaptureResult = std::variant<responsive::FiniteResponsiveCapture, Error>;

[[nodiscard]] CaptureResult
capture_trusted_scenario(const compile::CompiledEngine &engine,
                         const authoring::ScenarioDocument &document,
                         const std::span<const std::string_view> buses,
                         std::string stage, const std::stop_token stop_token) {
    auto scenario = compile_trusted_scenario(engine, document, stage, stop_token);
    if (auto *failure = std::get_if<Error>(&scenario)) {
        return std::move(*failure);
    }
    auto captured = responsive::capture_finite_responsive_session(
        std::get<compile::CompiledScenario>(scenario), buses, stop_token);
    if (auto *failure =
            std::get_if<responsive::FiniteResponsiveCaptureError>(&captured)) {
        return capture_error(stage, *failure);
    }
    return std::get<responsive::FiniteResponsiveCapture>(std::move(captured));
}

using LifecycleAuditionBusResult = std::variant<std::string, Error>;

[[nodiscard]] LifecycleAuditionBusResult
resolve_lifecycle_audition_bus(const compile::CompiledScenario &template_scenario,
                               const std::stop_token stop_token) {
    constexpr std::string_view stage = "lifecycle audition bus resolution";
    if (stop_token.stop_requested()) {
        return cancelled(stage);
    }
    auto created = create_engine_session(template_scenario,
                                         EngineSessionExecutionKind::finite_scenario);
    if (auto *failure = std::get_if<EngineSessionError>(&created)) {
        return error(ErrorKind::software,
                     failure->detail_code.empty()
                         ? "responsive-lifecycle-bus-session-create-failed"
                         : failure->detail_code,
                     std::string{stage},
                     "finite template session creation failed while resolving the "
                     "lifecycle audition bus");
    }
    auto session = std::get<EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    std::string resolved_id;
    std::size_t match_count = 0U;
    for (const auto &bus : descriptor.audio_buses) {
        if (bus.kind != EngineAudioBusKind::engine_audition_master) {
            continue;
        }
        ++match_count;
        resolved_id = bus.id;
    }
    if (descriptor.execution_kind != EngineSessionExecutionKind::finite_scenario ||
        descriptor.engine_id != template_scenario.engine().id() ||
        descriptor.scenario_id != template_scenario.id() || match_count != 1U ||
        resolved_id.empty()) {
        return error(ErrorKind::software,
                     "responsive-lifecycle-audition-bus-resolution-invalid",
                     std::string{stage},
                     "finite template session must expose exactly one resolved engine "
                     "audition master bus");
    }
    if (stop_token.stop_requested()) {
        return cancelled(stage);
    }
    return resolved_id;
}

[[nodiscard]] std::size_t responsive_worker_capacity() noexcept {
    const auto available = std::thread::hardware_concurrency();
#if defined(__linux__)
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (::sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
        std::size_t affinity_count = 0U;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            affinity_count += CPU_ISSET(cpu, &affinity) != 0 ? 1U : 0U;
        }
        if (affinity_count != 0U) {
            return std::min<std::size_t>(
                6U, available == 0U ? affinity_count
                                    : std::min<std::size_t>(available, affinity_count));
        }
    }
#endif
    return std::min<std::size_t>(6U, available == 0U ? 1U : available);
}

template <class Output, class Input, class Operation>
[[nodiscard]] std::variant<std::vector<Output>, Error>
parallel_map_ordered(const std::span<const Input> inputs, const std::string_view stage,
                     const std::stop_token parent_stop, Operation operation) {
    return detail::parallel_map_ordered_with_workers<Output>(
        inputs, stage, parent_stop, responsive_worker_capacity(), std::move(operation));
}

struct CookedLifecycle {
    responsive::LifecycleCookedPackage package;
    std::size_t published_capture_count = 0U;
};

using CookedLifecycleResult = std::variant<CookedLifecycle, Error>;

[[nodiscard]] CookedLifecycleResult
cook_lifecycle(const responsive::ResponsiveBakeProfile &profile,
               const authoring::EnginePackageDocument &engine_document,
               const responsive::ResponsiveScenarioTemplate &scenario_template,
               const compile::CompiledEngine &engine,
               const std::string_view audition_bus_id,
               const std::uint64_t maximum_presentation_transfer_coefficient_count,
               const std::stop_token stop_token) {
    const responsive::LifecycleScenarioPlanRequest request{
        profile, engine_document, scenario_template.document,
        scenario_template.identity_sha256,
        maximum_presentation_transfer_coefficient_count};
    const std::array<std::string_view, 1U> buses{audition_bus_id};

    const auto capture_role = [&](const responsive::LifecycleScenarioSpec &spec,
                                  const std::string_view label)
        -> std::variant<responsive::LifecycleCaptureEvidence, Error> {
        auto captured = capture_trusted_scenario(
            engine, spec.scenario, buses,
            "lifecycle " + std::string{label} + " capture", stop_token);
        if (auto *failure = std::get_if<Error>(&captured)) {
            return std::move(*failure);
        }
        auto mapped = responsive::map_lifecycle_capture(
            spec, std::get<responsive::FiniteResponsiveCapture>(captured), 0U,
            stop_token);
        if (auto *failure = std::get_if<responsive::LifecycleError>(&mapped)) {
            return lifecycle_error("lifecycle " + std::string{label} + " mapping",
                                   *failure);
        }
        return std::get<responsive::LifecycleCaptureEvidence>(std::move(mapped));
    };
    const auto plan_role =
        [&](const responsive::LifecycleScenarioRole role, const std::string_view label,
            const std::optional<responsive::LifecycleDynamicStarterRelease> &release =
                std::nullopt)
        -> std::variant<responsive::LifecycleScenarioSpec, Error> {
        auto planned = responsive::plan_lifecycle_scenario(request, role, release);
        if (auto *failure = std::get_if<responsive::LifecycleError>(&planned)) {
            return lifecycle_error("lifecycle " + std::string{label} + " planning",
                                   *failure);
        }
        return std::get<responsive::LifecycleScenarioSpec>(std::move(planned));
    };

    auto starter_spec_result =
        plan_role(responsive::LifecycleScenarioRole::starter, "starter");
    if (auto *failure = std::get_if<Error>(&starter_spec_result)) {
        return std::move(*failure);
    }
    auto starter_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(starter_spec_result));
    auto starter_capture_result = capture_role(starter_spec, "starter");
    if (auto *failure = std::get_if<Error>(&starter_capture_result)) {
        return std::move(*failure);
    }
    auto starter_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(starter_capture_result));

    auto probe_spec_result =
        plan_role(responsive::LifecycleScenarioRole::startup_probe, "startup probe");
    if (auto *failure = std::get_if<Error>(&probe_spec_result)) {
        return std::move(*failure);
    }
    auto probe_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(probe_spec_result));
    auto probe_capture_result = capture_role(probe_spec, "startup probe");
    if (auto *failure = std::get_if<Error>(&probe_capture_result)) {
        return std::move(*failure);
    }
    auto probe_capture =
        std::get<responsive::LifecycleCaptureEvidence>(std::move(probe_capture_result));
    auto release_result = responsive::choose_dynamic_starter_release(
        probe_capture, profile.rpm.outer_minimum_rpm, stop_token);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&release_result)) {
        return lifecycle_error("lifecycle starter release selection", *failure);
    }
    auto release =
        std::get<responsive::LifecycleDynamicStarterRelease>(std::move(release_result));

    auto startup_spec_result =
        plan_role(responsive::LifecycleScenarioRole::startup, "startup", release);
    if (auto *failure = std::get_if<Error>(&startup_spec_result)) {
        return std::move(*failure);
    }
    auto startup_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(startup_spec_result));
    auto startup_capture_result = capture_role(startup_spec, "startup");
    if (auto *failure = std::get_if<Error>(&startup_capture_result)) {
        return std::move(*failure);
    }
    auto startup_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(startup_capture_result));

    auto shutdown_spec_result =
        plan_role(responsive::LifecycleScenarioRole::shutdown, "shutdown");
    if (auto *failure = std::get_if<Error>(&shutdown_spec_result)) {
        return std::move(*failure);
    }
    auto shutdown_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(shutdown_spec_result));
    auto shutdown_capture_result = capture_role(shutdown_spec, "shutdown");
    if (auto *failure = std::get_if<Error>(&shutdown_capture_result)) {
        return std::move(*failure);
    }
    auto shutdown_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(shutdown_capture_result));

    auto elevated_spec_result = plan_role(
        responsive::LifecycleScenarioRole::shutdown_elevated, "elevated shutdown");
    if (auto *failure = std::get_if<Error>(&elevated_spec_result)) {
        return std::move(*failure);
    }
    auto elevated_spec =
        std::get<responsive::LifecycleScenarioSpec>(std::move(elevated_spec_result));
    auto elevated_capture_result = capture_role(elevated_spec, "elevated shutdown");
    if (auto *failure = std::get_if<Error>(&elevated_capture_result)) {
        return std::move(*failure);
    }
    auto elevated_capture = std::get<responsive::LifecycleCaptureEvidence>(
        std::move(elevated_capture_result));

    auto starter_result =
        responsive::cook_lifecycle_starter(starter_capture, stop_token);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&starter_result)) {
        return lifecycle_error("lifecycle starter cooking", *failure);
    }
    auto starter =
        std::get<responsive::LifecycleCookedStarter>(std::move(starter_result));
    auto startup_result = responsive::cook_lifecycle_startup(
        startup_spec, startup_capture, starter_capture, starter, stop_token);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&startup_result)) {
        return lifecycle_error("lifecycle startup cooking", *failure);
    }
    auto shutdown_result = responsive::cook_lifecycle_shutdown(
        shutdown_spec, shutdown_capture, stop_token);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&shutdown_result)) {
        return lifecycle_error("lifecycle shutdown cooking", *failure);
    }
    auto elevated_result = responsive::cook_lifecycle_elevated_shutdown(
        elevated_spec, elevated_capture, stop_token);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&elevated_result)) {
        return lifecycle_error("lifecycle elevated shutdown cooking", *failure);
    }
    auto admission_result =
        responsive::make_lifecycle_startup_admission_seed(profile, release);
    if (auto *failure = std::get_if<responsive::LifecycleError>(&admission_result)) {
        return lifecycle_error("lifecycle startup admission", *failure);
    }
    auto assembled = responsive::assemble_lifecycle_package(
        std::move(starter_capture), std::move(startup_capture),
        std::move(shutdown_capture), std::move(elevated_capture), starter,
        std::get<responsive::LifecycleStartupPresentation>(std::move(startup_result)),
        std::get<responsive::LifecycleShutdownPresentation>(std::move(shutdown_result)),
        std::get<responsive::LifecycleShutdownPresentation>(std::move(elevated_result)),
        std::get<responsive::LifecycleStartupAdmissionSeed>(
            std::move(admission_result)));
    if (auto *failure = std::get_if<responsive::LifecycleError>(&assembled)) {
        return lifecycle_error("lifecycle package assembly", *failure);
    }
    return CookedLifecycle{
        std::get<responsive::LifecycleCookedPackage>(std::move(assembled)), 4U};
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

[[nodiscard]] std::string asset_kind_name(const compile::AssetKind kind) {
    switch (kind) {
    case compile::AssetKind::audio:
        return "audio";
    case compile::AssetKind::accessory_configuration:
        return "accessory-configuration";
    }
    return "unknown";
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < digest.bytes.size(); ++index) {
        result[index * 2U] = digits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::string bytes_text(const std::vector<std::byte> &bytes) {
    return bytes.empty() ? std::string{}
                         : std::string{reinterpret_cast<const char *>(bytes.data()),
                                       bytes.size()};
}

struct InstalledSharedStarter {
    responsive::ResponsiveOptionalChildPackageV1 child;
    responsive::SharedRecordedStarterIdentityV1 identity;
};

using InstalledSharedStarterResult = std::variant<InstalledSharedStarter, Error>;

[[nodiscard]] InstalledSharedStarterResult load_installed_shared_starter(
    const BuiltinAssetCatalogIdentity &catalog,
    const responsive::ResponsivePackageProvenanceV1 &provenance,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled("shared recorded starter input");
    }
    auto root_result = detail::open_asset_root(catalog.canonical_path.parent_path());
    if (auto *failure = std::get_if<NativeInputError>(&root_result)) {
        return input_error("shared recorded starter input", *failure);
    }
    auto root = std::get<detail::OpenedAssetRoot>(std::move(root_result));
    constexpr std::string_view runtime_uri =
        "runtime-audio/shared-recorded-starter/runtime.json";
    auto runtime_result = detail::read_confined_asset(
        root, catalog.canonical_path, runtime_uri, NativeInputSubject::audio_asset,
        "shared-recorded-starter", 16U * 1024U * 1024U);
    if (auto *failure = std::get_if<NativeInputError>(&runtime_result)) {
        return input_error("shared recorded starter runtime", *failure);
    }
    auto runtime_file = std::get<detail::ReadFile>(std::move(runtime_result));
    auto parsed = authoring::parse_json(bytes_text(runtime_file.bytes));
    const auto *document = std::get_if<authoring::JsonDocument>(&parsed);
    if (document == nullptr) {
        return error(ErrorKind::unavailable, "shared-recorded-starter-runtime-invalid",
                     "shared recorded starter input",
                     "installed shared starter runtime is not valid JSON",
                     runtime_file.canonical_path);
    }
    const auto runtime = document->root();
    const auto schema = runtime.find("schema").string();
    const auto id = runtime.find("id").string();
    const auto audio = runtime.find("audio");
    const auto relative_path = audio.find("relative_path").string();
    const auto byte_count = audio.find("byte_count").number();
    const auto payload_sha256 = audio.find("payload_sha256").string();
    if (!schema || *schema != "engine-sim-offline/shared-recorded-starter" || !id ||
        *id != "shared-recorded-starter-licensed" || !relative_path || !byte_count ||
        !payload_sha256 || !artifacts::is_portable_revengine_path(*relative_path) ||
        !relative_path->starts_with("audio/") || *byte_count < 1.0 ||
        *byte_count > static_cast<double>(UINT64_C(1) << 32U) ||
        std::floor(*byte_count) != *byte_count || payload_sha256->size() != 64U ||
        !std::all_of(payload_sha256->begin(), payload_sha256->end(), [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
        return error(ErrorKind::unavailable, "shared-recorded-starter-contract-invalid",
                     "shared recorded starter input",
                     "installed shared starter contract is incomplete or invalid",
                     runtime_file.canonical_path);
    }
    const std::string payload_uri =
        "runtime-audio/shared-recorded-starter/" + std::string{*relative_path};
    auto payload_result = detail::read_confined_asset(
        root, catalog.canonical_path, payload_uri, NativeInputSubject::audio_asset,
        "shared-recorded-starter-audio", static_cast<std::uintmax_t>(*byte_count));
    if (auto *failure = std::get_if<NativeInputError>(&payload_result)) {
        return input_error("shared recorded starter payload", *failure);
    }
    auto payload_file = std::get<detail::ReadFile>(std::move(payload_result));
    const auto payload_digest = contract::sha256(payload_file.bytes);
    if (payload_file.bytes.size() != static_cast<std::size_t>(*byte_count) ||
        digest_hex(payload_digest) != *payload_sha256) {
        return error(ErrorKind::unavailable, "shared-recorded-starter-payload-mismatch",
                     "shared recorded starter input",
                     "installed shared starter payload differs from its manifest",
                     payload_file.canonical_path);
    }

    auto encoded = responsive::encode_responsive_shared_recorded_starter_v1(
        {runtime_file.bytes, payload_file.bytes}, provenance, stop_token);
    if (auto *failure =
            std::get_if<responsive::NativeResponsivePackageError>(&encoded)) {
        return package_error("shared recorded starter encoding", *failure);
    }
    auto starter = std::get<responsive::EncodedResponsiveSharedRecordedStarterV1>(
        std::move(encoded));
    return InstalledSharedStarter{std::move(starter.child), starter.identity};
}

} // namespace

std::string_view
bake_revengine_error_kind_label(const BakeRevengineErrorKind kind) noexcept {
    switch (kind) {
    case BakeRevengineErrorKind::data_error:
        return "data-error";
    case BakeRevengineErrorKind::no_input:
        return "no-input";
    case BakeRevengineErrorKind::unavailable:
        return "unavailable";
    case BakeRevengineErrorKind::software:
        return "software-error";
    case BakeRevengineErrorKind::cant_create:
        return "cant-create";
    case BakeRevengineErrorKind::temporary_failure:
        return "temporary-failure";
    case BakeRevengineErrorKind::cancelled:
        return "cancelled";
    }
    return "software-error";
}

BakeRevengineResult bake_revengine_native(const BakeRevengineRequest &request,
                                          const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled("request admission");
    }
    auto output_result = preflight_native_output_directory(request.output_file);
    if (const auto *failure = std::get_if<NativeOutputError>(&output_result)) {
        return output_error(*failure);
    }
    const auto output = std::get<NativeOutputDirectory>(std::move(output_result));
    const auto canonical_output = output.publication_root / output.publication_name;

    auto catalog_result = load_builtin_asset_catalog_identity_with_builtin_assets();
    if (const auto *failure = std::get_if<NativeInputError>(&catalog_result)) {
        return input_error("built-in asset catalog identity", *failure);
    }
    const auto catalog =
        std::get<BuiltinAssetCatalogIdentity>(std::move(catalog_result));

    auto engine_input_result =
        request.asset_root.has_value()
            ? load_native_engine_input(request.engine_path, *request.asset_root)
            : load_native_engine_input_from_builtin_catalog(request.engine_path,
                                                            catalog.canonical_path);
    if (const auto *failure = std::get_if<NativeInputError>(&engine_input_result)) {
        return input_error("engine input", *failure);
    }
    auto engine_input = std::get<NativeEngineInput>(std::move(engine_input_result));
    if (engine_input.builtin_asset_catalog_sha256.has_value() &&
        *engine_input.builtin_asset_catalog_sha256 != catalog.sha256) {
        return error(ErrorKind::unavailable, "builtin-asset-catalog-changed",
                     "engine input",
                     "built-in asset catalog changed during engine resolution",
                     catalog.canonical_path);
    }
    if (stop_token.stop_requested()) {
        return cancelled("engine input");
    }

    auto determinism_result = determinism::renderer_determinism_envelope();
    const auto *determinism =
        std::get_if<determinism::RendererDeterminismEnvelope>(&determinism_result);
    if (determinism == nullptr || !determinism->production_observation()) {
        return error(ErrorKind::unavailable,
                     "native-responsive-backend-identity-unavailable",
                     "backend identity",
                     "current process cannot prove a production native renderer "
                     "identity");
    }
    auto executable_result = snapshot_current_executable(stop_token);
    if (auto *failure = std::get_if<Error>(&executable_result)) {
        return std::move(*failure);
    }
    const auto executable = std::get<ExecutableSnapshot>(executable_result);

    auto selected_profile =
        responsive::derive_engine_redline_affine_profile(engine_input.document);
    if (auto *failure = std::get_if<contract::ValidationReport>(&selected_profile)) {
        return validation_error("responsive-profile-selection-failed",
                                "profile selection", std::move(*failure));
    }
    auto profile =
        std::get<responsive::ResponsiveBakeProfile>(std::move(selected_profile));
    auto template_result =
        responsive::make_responsive_scenario_template(engine_input.document, profile);
    if (auto *failure = std::get_if<contract::ValidationReport>(&template_result)) {
        return validation_error("responsive-scenario-template-failed",
                                "scenario template", std::move(*failure));
    }
    auto scenario_template =
        std::get<responsive::ResponsiveScenarioTemplate>(std::move(template_result));

    const auto asset_views = engine_input.asset_views();
    auto engine_result = compile::compile_engine(engine_input.document, asset_views);
    if (auto *failure = std::get_if<authoring::DiagnosticReport>(&engine_result)) {
        return diagnostic_error("engine compilation", std::move(*failure));
    }
    auto engine = std::get<compile::CompiledEngine>(std::move(engine_result));
    if (stop_token.stop_requested()) {
        return cancelled("engine compilation");
    }

    auto template_scenario_result = compile_trusted_scenario(
        engine, scenario_template.document, "presentation compilation", stop_token);
    if (auto *failure = std::get_if<Error>(&template_scenario_result)) {
        return std::move(*failure);
    }
    const auto &template_scenario =
        std::get<compile::CompiledScenario>(template_scenario_result);
    auto presentation_result =
        responsive::compile_responsive_presentation_transfer(template_scenario);
    if (auto *failure = std::get_if<contract::ValidationReport>(&presentation_result)) {
        return validation_error("responsive-presentation-compilation-failed",
                                "presentation compilation", std::move(*failure));
    }
    auto presentation = std::get<responsive::ResponsiveCompiledPresentation>(
        std::move(presentation_result));
    const auto maximum_presentation_transfer_coefficient_count =
        std::max_element(presentation.transfers.begin(), presentation.transfers.end(),
                         [](const auto &left, const auto &right) {
                             return left.coefficient_count < right.coefficient_count;
                         })
            ->coefficient_count;
    const auto dry_bus_views = string_views(presentation.audition_dry_bus_order);
    auto lifecycle_bus_result =
        resolve_lifecycle_audition_bus(template_scenario, stop_token);
    if (auto *failure = std::get_if<Error>(&lifecycle_bus_result)) {
        return std::move(*failure);
    }
    auto lifecycle_audition_bus_id =
        std::get<std::string>(std::move(lifecycle_bus_result));

    auto held_plan_result = responsive::plan_held_state_scenarios(
        {profile, scenario_template.document, scenario_template.identity_sha256,
         held_capture_mode(engine_input.document)});
    if (auto *failure = std::get_if<contract::ValidationReport>(&held_plan_result)) {
        return validation_error("responsive-held-planning-failed", "held planning",
                                std::move(*failure));
    }
    auto held_specs = std::get<std::vector<responsive::HeldStateScenarioSpec>>(
        std::move(held_plan_result));
    auto held_cells_result = parallel_map_ordered<responsive::HeldCookedCell>(
        std::span<const responsive::HeldStateScenarioSpec>{held_specs},
        "held capture and cooking", stop_token,
        [&](const responsive::HeldStateScenarioSpec &spec,
            const std::stop_token worker_stop)
            -> std::variant<responsive::HeldCookedCell, Error> {
            auto captured =
                capture_trusted_scenario(engine, spec.scenario, dry_bus_views,
                                         "held capture " + spec.id, worker_stop);
            if (auto *failure = std::get_if<Error>(&captured)) {
                return std::move(*failure);
            }
            auto cooked = responsive::cook_held_state(
                spec, std::get<responsive::FiniteResponsiveCapture>(captured));
            if (auto *failure = std::get_if<contract::ValidationReport>(&cooked)) {
                return validation_error("responsive-held-cook-failed",
                                        "held cooking " + spec.id, std::move(*failure));
            }
            return std::get<responsive::HeldCookedCell>(std::move(cooked));
        });
    if (auto *failure = std::get_if<Error>(&held_cells_result)) {
        return std::move(*failure);
    }
    auto held_cells =
        std::get<std::vector<responsive::HeldCookedCell>>(std::move(held_cells_result));
    auto held_grid_result = responsive::cook_held_texture_grid(profile, held_cells);
    if (auto *failure = std::get_if<contract::ValidationReport>(&held_grid_result)) {
        return validation_error("responsive-held-grid-failed", "held grid cooking",
                                std::move(*failure));
    }
    auto held_grid = std::get<responsive::HeldCookedGrid>(std::move(held_grid_result));

    auto directional_plan_result = responsive::plan_directional_sweep_scenarios(
        {profile, scenario_template.document, scenario_template.identity_sha256});
    if (auto *failure =
            std::get_if<contract::ValidationReport>(&directional_plan_result)) {
        return validation_error("responsive-directional-planning-failed",
                                "directional planning", std::move(*failure));
    }
    auto directional_specs =
        std::get<std::vector<responsive::DirectionalSweepScenarioSpec>>(
            std::move(directional_plan_result));
    const auto directional_capture_count = directional_specs.size();
    auto directional_captures_result =
        parallel_map_ordered<responsive::DirectionalCookedCapture>(
            std::span<const responsive::DirectionalSweepScenarioSpec>{
                directional_specs},
            "directional capture and cooking", stop_token,
            [&](const responsive::DirectionalSweepScenarioSpec &spec,
                const std::stop_token worker_stop)
                -> std::variant<responsive::DirectionalCookedCapture, Error> {
                auto captured = capture_trusted_scenario(
                    engine, spec.scenario, dry_bus_views,
                    "directional capture " + spec.id, worker_stop);
                if (auto *failure = std::get_if<Error>(&captured)) {
                    return std::move(*failure);
                }
                auto cooked = responsive::cook_directional_capture(
                    spec, std::get<responsive::FiniteResponsiveCapture>(captured));
                if (auto *failure = std::get_if<contract::ValidationReport>(&cooked)) {
                    return validation_error("responsive-directional-cook-failed",
                                            "directional cooking " + spec.id,
                                            std::move(*failure));
                }
                return std::get<responsive::DirectionalCookedCapture>(
                    std::move(cooked));
            });
    if (auto *failure = std::get_if<Error>(&directional_captures_result)) {
        return std::move(*failure);
    }
    auto directional_captures =
        std::get<std::vector<responsive::DirectionalCookedCapture>>(
            std::move(directional_captures_result));
    auto directional_model_result = responsive::assemble_directional_model(
        profile, std::move(directional_captures));
    if (auto *failure =
            std::get_if<contract::ValidationReport>(&directional_model_result)) {
        return validation_error("responsive-directional-model-failed",
                                "directional model assembly", std::move(*failure));
    }
    auto directional_model = std::get<responsive::DirectionalCookedModel>(
        std::move(directional_model_result));

    auto lifecycle_result =
        cook_lifecycle(profile, engine_input.document, scenario_template, engine,
                       lifecycle_audition_bus_id,
                       maximum_presentation_transfer_coefficient_count, stop_token);
    if (auto *failure = std::get_if<Error>(&lifecycle_result)) {
        return std::move(*failure);
    }
    auto lifecycle = std::get<CookedLifecycle>(std::move(lifecycle_result));

    const responsive::ResponsivePackageProvenanceV1 provenance{
        std::string{engine.id()}, engine.provenance().bundle.sha256,
        "engine-sim-offline-renderer-build",
        determinism->source_stamp().source_closure_sha256};
    responsive::ResponsivePackageChildrenViewV1 children;
    children.profile = &profile;
    children.held = &held_grid;
    children.directional = &directional_model;
    children.presentation = &presentation;
    children.provenance = provenance;
    children.canonical_offline_bake = false;
    auto core_result =
        responsive::encode_responsive_package_children_v1(children, stop_token);
    if (auto *failure =
            std::get_if<responsive::NativeResponsivePackageError>(&core_result)) {
        return package_error("responsive child encoding", *failure);
    }
    auto core = std::get<responsive::EncodedResponsivePackageChildrenV1>(
        std::move(core_result));

    auto starter_result =
        load_installed_shared_starter(catalog, provenance, stop_token);
    if (auto *failure = std::get_if<Error>(&starter_result)) {
        return std::move(*failure);
    }
    auto shared_starter = std::get<InstalledSharedStarter>(std::move(starter_result));

    responsive::NativeResponsiveBakeIdentityInputV1 identity;
    identity.backend.release_identity = request.release_identity;
    identity.backend.c_api_version = ESO_C_API_VERSION;
    identity.backend.executable_sha256 = executable.sha256;
    identity.backend.source_closure_sha256 =
        determinism->source_stamp().source_closure_sha256;
    const auto authority = native_responsive_bake_authority_v1();
    identity.backend.method_registry_sha256 = authority.method_authority_sha256;
    identity.engine_id = std::string{engine.id()};
    identity.engine_source_sha256 = engine_input.source.sha256;
    identity.profile_id = profile.id;
    identity.profile_sha256 = profile.selection_identity_sha256;
    identity.bake_recipe_sha256 = authority.bake_recipe_sha256;
    identity.builtin_asset_catalog_sha256 = catalog.sha256;
    identity.resolved_assets.reserve(engine_input.assets.size());
    for (const auto &asset : engine_input.assets) {
        identity.resolved_assets.push_back(
            {asset_kind_name(asset.kind), asset.id, contract::sha256(asset.bytes)});
    }
    identity.shared_recorded_starter = shared_starter.identity;

    auto lifecycle_child_result = responsive::encode_responsive_lifecycle_child_v1(
        lifecycle.package, provenance, core.held_manifest_sha256, stop_token);
    if (auto *failure = std::get_if<responsive::NativeResponsivePackageError>(
            &lifecycle_child_result)) {
        return package_error("lifecycle child encoding", *failure);
    }
    std::vector<responsive::ResponsiveOptionalChildPackageV1> optional_children;
    optional_children.reserve(2U);
    optional_children.push_back(std::get<responsive::ResponsiveOptionalChildPackageV1>(
        std::move(lifecycle_child_result)));
    optional_children.push_back(std::move(shared_starter.child));
    auto attached_result = responsive::attach_responsive_optional_children_v1(
        std::move(core), std::move(optional_children), stop_token);
    if (auto *failure =
            std::get_if<responsive::NativeResponsivePackageError>(&attached_result)) {
        return package_error("responsive child attachment", *failure);
    }
    auto attached = std::get<responsive::EncodedResponsivePackageChildrenV1>(
        std::move(attached_result));

    auto built_result = responsive::build_native_responsive_package_from_encoded_v2(
        {std::move(identity), std::move(attached), std::nullopt}, stop_token);
    if (auto *failure =
            std::get_if<responsive::NativeResponsivePackageError>(&built_result)) {
        return package_error("REVENGINE package construction", *failure);
    }
    auto built =
        std::get<responsive::NativeResponsiveCookedPackageV2>(std::move(built_result));
    const auto member_count = built.package.members.size();
    const auto cache_identity_sha256 = built.package.cache_identity.sha256;
    auto publication_result = responsive::publish_native_revengine_atomic(
        built.package, canonical_output, stop_token);
    if (auto *failure = std::get_if<responsive::NativeResponsivePackageError>(
            &publication_result)) {
        return package_error("REVENGINE publication", *failure);
    }
    auto publication = std::get<responsive::NativeResponsiveCarrierPublication>(
        std::move(publication_result));
    return BakedRevengineFile{
        std::move(publication.path),
        std::string{engine.id()},
        profile.id,
        publication.byte_count,
        member_count,
        held_grid.cells.size(),
        directional_capture_count,
        lifecycle.published_capture_count,
        publication.sha256,
        cache_identity_sha256,
        true,
    };
}

} // namespace engine_sim_offline::cli
