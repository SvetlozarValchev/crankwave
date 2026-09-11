#include "bake_diesel_command.hpp"

#include "crankwave/artifacts/crankwave_container.hpp"
#include "crankwave/authoring/json.hpp"
#include "crankwave/responsive/native_publication.hpp"
#include "determinism/renderer_numeric_environment.hpp"
#include "diesel/diesel_sound.hpp"
#include "native_input_files_support.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

namespace crankwave::cli {
namespace {
BakeCrankwaveError failure(BakeCrankwaveErrorKind kind, std::string message,
                           std::filesystem::path path = {}) {
    BakeCrankwaveError e;
    e.kind = kind;
    e.code = "diesel-bake-failed";
    e.stage = "mechanical diesel";
    e.message = std::move(message);
    e.path = std::move(path);
    return e;
}
std::string hex(const contract::Sha256Digest &digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto v : digest.bytes) {
        result += digits[v >> 4U];
        result += digits[v & 15U];
    }
    return result;
}
} // namespace

std::optional<BakeCrankwaveResult>
try_bake_mechanical_diesel(const BakeCrankwaveRequest &request,
                           std::stop_token stop_token) {
    auto read = detail::read_exact_regular_file(request.engine_path,
                                                NativeInputSubject::engine_document, {},
                                                4U * 1024U * 1024U);
    if (!std::holds_alternative<detail::ReadFile>(read))
        return {};
    const auto &source = std::get<detail::ReadFile>(read).bytes;
    const std::string_view source_json{reinterpret_cast<const char *>(source.data()),
                                       source.size()};
    auto syntax = authoring::parse_json(source_json);
    if (!std::holds_alternative<authoring::JsonDocument>(syntax))
        return {};
    const auto model =
        std::get<authoring::JsonDocument>(syntax).root().find("model").string();
    if (!model || *model != diesel::kMechanicalDieselModel)
        return {};
    auto parsed = diesel::parse_sound_definition(source_json);
    if (const auto *error = std::get_if<diesel::SoundError>(&parsed))
        return failure(BakeCrankwaveErrorKind::data_error,
                       error->path + ": " + error->message, request.engine_path);
    const auto &definition = std::get<diesel::SoundDefinition>(parsed);
    // Experimental model identity is explicit. It does not claim the gasoline
    // parity renderer's clean-source or real-engine calibration admission.
    const auto numeric = determinism::renderer_numeric_environment();
    if (!std::holds_alternative<determinism::RendererNumericEnvironment>(numeric))
        return failure(BakeCrankwaveErrorKind::unavailable,
                       "renderer numeric environment is not admitted");
    std::error_code ec;
    const auto executable = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec)
        return failure(BakeCrankwaveErrorKind::unavailable,
                       "cannot identify native baker executable");
    auto executable_read = detail::read_exact_regular_file(
        executable, NativeInputSubject::engine_document, {}, 512U * 1024U * 1024U);
    if (!std::holds_alternative<detail::ReadFile>(executable_read))
        return failure(BakeCrankwaveErrorKind::unavailable,
                       "cannot hash native baker executable");
    const auto executable_sha =
        contract::sha256(std::get<detail::ReadFile>(executable_read).bytes);
    const auto &environment =
        std::get<determinism::RendererNumericEnvironment>(numeric);
    const std::string backend =
        "{\"kind\":\"native-cpp-experimental\",\"executable_sha256\":\"" +
        hex(executable_sha) + "\",\"numeric_policy\":\"" +
        std::string{environment.build_policy_id} + "\"}";
    auto baked = diesel::bake_sound(source_json, backend, stop_token);
    if (const auto *error = std::get_if<diesel::SoundError>(&baked))
        return failure(error->cancelled ? BakeCrankwaveErrorKind::cancelled
                                        : BakeCrankwaveErrorKind::data_error,
                       error->path + ": " + error->message);
    auto material = std::get<diesel::BakedSound>(std::move(baked));
    responsive::NativeResponsivePackageV2 package;
    package.members = std::move(material.members);
    std::sort(package.members.begin(), package.members.end(),
              [](const auto &a, const auto &b) { return a.path < b.path; });
    std::vector<artifacts::CrankwavePackEntry> entries;
    for (const auto &member : package.members)
        entries.push_back({member.path, member.bytes});
    auto packed = artifacts::pack_crankwave_v1(entries);
    if (const auto *error = std::get_if<artifacts::CrankwaveContainerError>(&packed))
        return failure(BakeCrankwaveErrorKind::software, error->message);
    package.crankwave_v1 = std::get<std::vector<std::byte>>(std::move(packed));
    package.crankwave_sha256 = contract::sha256(package.crankwave_v1);
    auto published = responsive::publish_native_crankwave_atomic(
        package, request.output_file, stop_token);
    if (const auto *error =
            std::get_if<responsive::NativeResponsivePackageError>(&published))
        return failure(error->code ==
                               responsive::NativeResponsivePackageErrorCode::cancelled
                           ? BakeCrankwaveErrorKind::cancelled
                           : BakeCrankwaveErrorKind::cant_create,
                       error->message, request.output_file);
    BakedCrankwaveFile result;
    result.output_path = request.output_file;
    result.engine_id = definition.engine_id;
    result.profile_id = std::string{diesel::kMechanicalDieselModel};
    result.container_byte_count = package.crankwave_v1.size();
    result.entry_count = entries.size();
    result.held_cell_count = material.cell_count;
    result.container_sha256 = package.crankwave_sha256;
    result.cache_identity_sha256 = package.crankwave_sha256;
    result.verified = true;
    return result;
}
} // namespace crankwave::cli
