#include "native_input_files.hpp"
#include "native_input_files_support.hpp"

#include "engine_sim_offline/session.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::cli;

constexpr std::string_view kOriginalIrUri =
    "../../../reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav";
constexpr std::string_view kOriginalAccessoryUri =
    "../../profiles/bmw-m52b28/accessory-configurations/"
    "bmw-m52b28-warm-stock-accessories-v1.json";
constexpr std::string_view kSmooth39Sha256 =
    "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc";

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not read test source " + path.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream) {
        throw std::runtime_error("could not write test file " + path.string());
    }
}

void replace_once(std::string &text, std::string_view before, std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos ||
        text.find(before, position + before.size()) != std::string::npos) {
        throw std::runtime_error("test engine URI fixture was not unique");
    }
    text.replace(position, before.size(), after);
}

void replace_all(std::string &text, const std::string_view before,
                 const std::string_view after) {
    std::size_t position = 0;
    std::size_t replacement_count = 0;
    while ((position = text.find(before, position)) != std::string::npos) {
        text.replace(position, before.size(), after);
        position += after.size();
        ++replacement_count;
    }
    if (replacement_count == 0U) {
        throw std::runtime_error("test engine selection fixture was absent");
    }
}

class IsolatedDirectory {
  public:
    IsolatedDirectory() {
        static std::atomic_uint64_t sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 32U; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("engine-sim-offline-native-input-" + std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error("could not create isolated test directory");
    }

    ~IsolatedDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    IsolatedDirectory(const IsolatedDirectory &) = delete;
    IsolatedDirectory &operator=(const IsolatedDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class EngineFixture {
  public:
    EngineFixture(const std::filesystem::path &source_root,
                  std::string_view ir_uri = "assets/ir.wav",
                  std::string_view accessory_uri = "assets/accessory.json")
        : isolated_(), root_(isolated_.path() / "root"),
          engine_path_(root_ / "package/engine.json"),
          ir_path_(root_ / "package/assets/ir.wav"),
          accessory_path_(root_ / "package/assets/accessory.json") {
        auto json = read_text(source_root / "data/engines/bmw-m52b28/engine.json");
        replace_once(json, kOriginalIrUri, ir_uri);
        replace_once(json, kOriginalAccessoryUri, accessory_uri);
        write_text(engine_path_, json);
        write_text(ir_path_, "RIFF-owned-ir");
        write_text(accessory_path_, "owned-accessory");
    }

    [[nodiscard]] const std::filesystem::path &root() const noexcept {
        return root_;
    }
    [[nodiscard]] const std::filesystem::path &engine_path() const noexcept {
        return engine_path_;
    }
    [[nodiscard]] const std::filesystem::path &ir_path() const noexcept {
        return ir_path_;
    }
    [[nodiscard]] const std::filesystem::path &accessory_path() const noexcept {
        return accessory_path_;
    }
    [[nodiscard]] const std::filesystem::path &isolated_root() const noexcept {
        return isolated_.path();
    }

  private:
    IsolatedDirectory isolated_;
    std::filesystem::path root_;
    std::filesystem::path engine_path_;
    std::filesystem::path ir_path_;
    std::filesystem::path accessory_path_;
};

template <class Result>
const NativeInputError &require_input_error(const Result &result,
                                            NativeInputErrorCode code,
                                            std::string_view message) {
    const auto *error = std::get_if<NativeInputError>(&result);
    expect(error != nullptr && error->code == code, message);
    return *error;
}

std::string payload_text(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void test_success_and_owned_views(const std::filesystem::path &source_root) {
    NativeEngineInput owned;
    {
        EngineFixture fixture(source_root);
        auto loaded = load_native_engine_input(fixture.engine_path(), fixture.root());
        expect(std::holds_alternative<NativeEngineInput>(loaded),
               "valid engine input was rejected");
        owned = std::get<NativeEngineInput>(std::move(loaded));
    }

    expect(owned.document.engine.identity.id.value == "bmw-m52b28",
           "loader returned the wrong parsed engine");
    expect(!owned.source.bytes.empty() &&
               owned.source.sha256 == contract::sha256(owned.source.bytes),
           "loader did not retain the exact engine source identity");
    expect(owned.assets.size() == 2U, "loader did not own every declared asset");
    const auto views = owned.asset_views();
    expect(views.size() == 2U && views[0].kind == compile::AssetKind::audio &&
               views[0].asset_id == "smooth-39" &&
               payload_text(views[0].bytes) == "RIFF-owned-ir" &&
               views[0].bytes.data() == owned.assets[0].bytes.data(),
           "audio asset view does not borrow the owned exact bytes");
    expect(views[1].kind == compile::AssetKind::accessory_configuration &&
               views[1].asset_id == "warm-stock-accessories" &&
               payload_text(views[1].bytes) == "owned-accessory" &&
               views[1].bytes.data() == owned.assets[1].bytes.data(),
           "accessory asset view does not borrow the owned exact bytes");
}

void test_missing_and_nonregular_assets(const std::filesystem::path &source_root) {
    {
        EngineFixture fixture(source_root);
        std::filesystem::remove(fixture.ir_path());
        const auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        const auto &error =
            require_input_error(loaded, NativeInputErrorCode::path_not_found,
                                "missing asset did not produce path-not-found");
        expect(error.kind == NativeInputErrorKind::no_input &&
                   error.subject == NativeInputSubject::audio_asset &&
                   error.asset_id == "smooth-39",
               "missing asset error lost its typed subject");
    }
    {
        EngineFixture fixture(source_root);
        std::filesystem::remove(fixture.ir_path());
        std::filesystem::create_directory(fixture.ir_path());
        const auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        require_input_error(loaded, NativeInputErrorCode::not_regular_file,
                            "directory asset was admitted");
    }
}

void test_root_escape_and_symlink(const std::filesystem::path &source_root) {
    {
        EngineFixture fixture(source_root, "../../outside.wav");
        write_text(fixture.isolated_root() / "outside.wav", "outside");
        const auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        const auto &error =
            require_input_error(loaded, NativeInputErrorCode::asset_outside_root,
                                "asset-root escape was admitted");
        expect(error.kind == NativeInputErrorKind::data_error,
               "asset-root escape has the wrong error class");
    }
    {
        EngineFixture fixture(source_root);
        const auto outside = fixture.isolated_root() / "outside.wav";
        write_text(outside, "outside");
        std::filesystem::remove(fixture.ir_path());
        std::error_code symlink_error;
        std::filesystem::create_symlink(outside, fixture.ir_path(), symlink_error);
        if (!symlink_error) {
            const auto loaded =
                load_native_engine_input(fixture.engine_path(), fixture.root());
            require_input_error(loaded, NativeInputErrorCode::symbolic_link_not_allowed,
                                "symbolic-link asset was admitted");
        } else if (symlink_error != std::errc::operation_not_permitted &&
                   symlink_error != std::errc::permission_denied &&
                   symlink_error != std::errc::not_supported) {
            throw std::filesystem::filesystem_error(
                "could not create symlink test fixture", fixture.ir_path(),
                symlink_error);
        }
    }
    {
        EngineFixture fixture(source_root, "https://example.invalid/ir.wav");
        const auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        require_input_error(loaded, NativeInputErrorCode::invalid_local_asset_uri,
                            "non-local asset URI was admitted");
    }
}

#if defined(__linux__)
void test_opened_root_descriptor_is_authority(
    const std::filesystem::path &source_root) {
    EngineFixture fixture(source_root);
    auto opened_result = detail::open_asset_root(fixture.root());
    expect(std::holds_alternative<detail::OpenedAssetRoot>(opened_result),
           "test asset root could not be opened");
    auto opened = std::get<detail::OpenedAssetRoot>(std::move(opened_result));

    const auto original_root = fixture.root();
    const auto moved_root = fixture.isolated_root() / "opened-root";
    std::filesystem::rename(original_root, moved_root);
    write_text(original_root / "package/assets/ir.wav", "replacement-path-bytes");

    auto read = detail::read_confined_asset(
        opened, original_root / "package/engine.json", "assets/ir.wav",
        NativeInputSubject::audio_asset, "smooth-39", 1024U);
    const auto *file = std::get_if<detail::ReadFile>(&read);
    expect(file != nullptr && payload_text(file->bytes) == "RIFF-owned-ir",
           "asset traversal reopened a replaced root pathname");
}
#endif

void test_resource_limits(const std::filesystem::path &source_root) {
    EngineFixture fixture(source_root);
    NativeInputLimits limits;
    limits.maximum_document_bytes = 16U;
    auto document_limited =
        load_native_engine_input(fixture.engine_path(), fixture.root(), limits);
    require_input_error(document_limited, NativeInputErrorCode::file_too_large,
                        "document byte limit was not enforced");

    limits = {};
    limits.maximum_asset_count = 1U;
    auto count_limited =
        load_native_engine_input(fixture.engine_path(), fixture.root(), limits);
    require_input_error(count_limited, NativeInputErrorCode::asset_count_limit_exceeded,
                        "asset count limit was not enforced");

    limits = {};
    limits.maximum_asset_bytes = 4U;
    auto asset_limited =
        load_native_engine_input(fixture.engine_path(), fixture.root(), limits);
    require_input_error(asset_limited, NativeInputErrorCode::file_too_large,
                        "per-asset byte limit was not enforced");

    limits = {};
    limits.maximum_total_asset_bytes = 13U;
    auto total_limited =
        load_native_engine_input(fixture.engine_path(), fixture.root(), limits);
    require_input_error(total_limited,
                        NativeInputErrorCode::total_asset_bytes_limit_exceeded,
                        "total asset byte limit was not enforced");
}

void test_builtin_catalog_covers_tracked_engines(
    const std::filesystem::path &source_root,
    const std::filesystem::path &catalog_path) {
    constexpr std::array engine_directories{
        std::string_view{"bmw-m52b28"},
        std::string_view{"bmw-m52tub28-cleanroom"},
        std::string_view{"harley-evolution-1340-cleanroom"},
        std::string_view{"honda-b18c5-cleanroom"},
        std::string_view{"kohler-ch750-cleanroom"},
        std::string_view{"radial-5-cleanroom"},
        std::string_view{"raspy-muscle-620-cleanroom"},
        std::string_view{"sequoia-3ur-fe-cleanroom"},
        std::string_view{"shovelhead-bank-local-heads"},
        std::string_view{"subaru-ej25-cleanroom"},
    };
    for (const auto directory : engine_directories) {
        const auto engine_path =
            source_root / "data/engines" / directory / "engine.json";
        auto loaded =
            load_native_engine_input_from_builtin_catalog(engine_path, catalog_path);
        expect(std::holds_alternative<NativeEngineInput>(loaded),
               "tracked engine is outside built-in catalog coverage: " +
                   std::string{directory});
        auto input = std::get<NativeEngineInput>(std::move(loaded));
        expect(!input.assets.empty(),
               "tracked engine resolved no built-in assets: " + std::string{directory});
        const auto views = input.asset_views();
        const auto compiled = compile::compile_engine(input.document, views);
        expect(std::holds_alternative<compile::CompiledEngine>(compiled),
               "tracked catalog-backed engine did not compile: " +
                   std::string{directory});
    }
}

void test_builtin_catalog_strict_parser(const std::filesystem::path &source_root) {
    const auto engine_path = source_root / "data/engines/bmw-m52b28/engine.json";
    IsolatedDirectory isolated;
    const auto catalog_path = isolated.path() / "catalog.v1.json";

    constexpr std::array invalid_catalogs{
        std::string_view{R"({"schema":"wrong","assets":[]})"},
        std::string_view{
            R"({"schema":"engine-sim-offline/builtin-asset-catalog.v1","assets":[],"extra":true})"},
        std::string_view{
            R"({"schema":"engine-sim-offline/builtin-asset-catalog.v1","assets":[{"kind":"audio","id":"smooth-39","sha256":"75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"},{"kind":"audio","id":"smooth-39","sha256":"75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc"}]})"},
    };
    for (const auto catalog : invalid_catalogs) {
        write_text(catalog_path, catalog);
        const auto loaded =
            load_native_engine_input_from_builtin_catalog(engine_path, catalog_path);
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::invalid_builtin_asset_catalog,
            "malformed built-in catalog was admitted");
        expect(error.kind == NativeInputErrorKind::unavailable &&
                   error.subject == NativeInputSubject::builtin_asset_catalog,
               "invalid catalog lost its installed-data error class");
    }
}

void test_builtin_catalog_engine_binding(const std::filesystem::path &source_root,
                                         const std::filesystem::path &catalog_path) {
    const auto canonical_engine = source_root / "data/engines/bmw-m52b28/engine.json";
    {
        IsolatedDirectory isolated;
        auto engine_text = read_text(canonical_engine);
        replace_once(engine_text,
                     "\"sha256\": \"" + std::string{kSmooth39Sha256} + "\"",
                     "\"sha256\": null");
        const auto engine_path = isolated.path() / "engine.json";
        write_text(engine_path, engine_text);
        const auto loaded =
            load_native_engine_input_from_builtin_catalog(engine_path, catalog_path);
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::builtin_asset_digest_required,
            "digest-free engine asset was admitted by the built-in catalog");
        expect(error.kind == NativeInputErrorKind::data_error &&
                   error.subject == NativeInputSubject::audio_asset &&
                   error.asset_id == "smooth-39",
               "missing engine digest lost its typed asset identity");
    }
    {
        IsolatedDirectory isolated;
        auto engine_text = read_text(canonical_engine);
        replace_once(engine_text, kSmooth39Sha256, std::string(64U, '0'));
        const auto engine_path = isolated.path() / "engine.json";
        write_text(engine_path, engine_text);
        const auto loaded =
            load_native_engine_input_from_builtin_catalog(engine_path, catalog_path);
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::builtin_asset_not_cataloged,
            "unknown content identity was admitted by the built-in catalog");
        expect(error.kind == NativeInputErrorKind::unavailable &&
                   error.asset_id == "smooth-39",
               "catalog coverage failure lost its typed asset identity");
    }
    {
        IsolatedDirectory isolated;
        const auto bundle = isolated.path() / "engine-sim-offline-assets";
        std::filesystem::create_directories(bundle / "payloads");
        std::filesystem::copy_file(catalog_path, bundle / "catalog.v1.json");
        write_text(bundle / "payloads" / kSmooth39Sha256, "corrupt-payload");
        const auto loaded = load_native_engine_input_from_builtin_catalog(
            canonical_engine, bundle / "catalog.v1.json");
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::builtin_asset_payload_hash_mismatch,
            "corrupt content-addressed payload was admitted");
        expect(error.kind == NativeInputErrorKind::unavailable &&
                   error.asset_id == "smooth-39",
               "payload corruption lost its typed asset identity");
    }
}

void test_builtin_catalog_discovery(
    const std::filesystem::path &installed_asset_relative_path) {
    {
        IsolatedDirectory isolated;
        const auto executable = isolated.path() / "build/bin/engine-sim-offline";
        const auto catalog = isolated.path() / "build/bin/engine-sim-offline-assets/"
                                               "catalog.v1.json";
        write_text(catalog, "{}");
        const auto discovered = discover_builtin_asset_catalog(executable);
        expect(std::holds_alternative<std::filesystem::path>(discovered) &&
                   std::get<std::filesystem::path>(discovered) == catalog,
               "build-tree catalog layout was not discovered");
    }
    {
        IsolatedDirectory isolated;
        const auto executable = isolated.path() / "prefix/bin/engine-sim-offline";
        const auto catalog = (executable.parent_path() / installed_asset_relative_path /
                              "catalog.v1.json")
                                 .lexically_normal();
        write_text(catalog, "{}");
        const auto discovered = discover_builtin_asset_catalog(executable);
        expect(std::holds_alternative<std::filesystem::path>(discovered) &&
                   std::get<std::filesystem::path>(discovered) == catalog,
               "installed-prefix catalog layout was not discovered");
    }
    {
        IsolatedDirectory isolated;
        const auto discovered =
            discover_builtin_asset_catalog(isolated.path() / "bin/engine-sim-offline");
        require_input_error(discovered,
                            NativeInputErrorCode::builtin_asset_catalog_not_found,
                            "missing built-in catalog did not fail discovery");
    }
}

void test_ir_authoring_catalog_binding(
    const std::filesystem::path &authoring_catalog_path,
    const std::string_view release_identity) {
    const auto loaded =
        load_ir_authoring_catalog(authoring_catalog_path, release_identity);
    const auto *catalog = std::get_if<IrAuthoringCatalogDocument>(&loaded);
    expect(catalog != nullptr && catalog->release_identity == release_identity &&
               catalog->entry_count == 73U && !catalog->json.empty() &&
               catalog->sha256 ==
                   contract::sha256(std::span{
                       reinterpret_cast<const std::byte *>(catalog->json.data()),
                       catalog->json.size()}),
           "valid IR authoring catalog did not preserve its release and exact bytes");

    const auto wrong_release =
        load_ir_authoring_catalog(authoring_catalog_path, "0.0.0-wrong");
    const auto &error = require_input_error(
        wrong_release, NativeInputErrorCode::invalid_ir_authoring_catalog,
        "IR authoring catalog was admitted for the wrong release");
    expect(error.subject == NativeInputSubject::ir_authoring_catalog &&
               error.kind == NativeInputErrorKind::unavailable,
           "wrong-release IR catalog lost its installed-data error class");

    IsolatedDirectory isolated;
    const auto isolated_catalog =
        isolated.path() / "assets/ir-authoring-catalog.v1.json";
    const auto isolated_technical = isolated.path() / "assets/catalog.v1.json";
    std::filesystem::create_directories(isolated_catalog.parent_path());
    std::filesystem::copy_file(authoring_catalog_path, isolated_catalog);
    std::filesystem::copy_file(authoring_catalog_path.parent_path() / "catalog.v1.json",
                               isolated_technical);
    auto mismatched = read_text(isolated_catalog);
    const auto digest_position = mismatched.find("\"sha256\": \"");
    expect(digest_position != std::string::npos,
           "IR authoring catalog fixture has no selection digest");
    mismatched.replace(digest_position + std::string_view{"\"sha256\": \""}.size(), 64U,
                       std::string(64U, '0'));
    write_text(isolated_catalog, mismatched);
    require_input_error(
        load_ir_authoring_catalog(isolated_catalog, release_identity),
        NativeInputErrorCode::invalid_ir_authoring_catalog,
        "authoring selection outside the technical catalog was admitted");

    std::filesystem::remove(isolated_catalog);
    const auto missing = load_ir_authoring_catalog(isolated_catalog, release_identity);
    const auto &missing_error = require_input_error(
        missing, NativeInputErrorCode::ir_authoring_catalog_not_found,
        "missing IR authoring catalog did not fail closed");
    expect(missing_error.kind == NativeInputErrorKind::unavailable,
           "missing IR authoring catalog has the wrong failure class");
}

void test_exact_pcm24_catalog_selection_processes(
    const std::filesystem::path &source_root,
    const std::filesystem::path &technical_catalog_path) {
    constexpr std::string_view selected_id = "archive-test-engine";
    constexpr std::string_view selected_sha256 =
        "7910907c1d2dbd3c4a52eb428cb89a40ab3655d1e0e62003a308bd41f42dbb74";
    IsolatedDirectory isolated;
    auto engine_json = read_text(source_root / "data/engines/bmw-m52b28/engine.json");
    replace_all(engine_json, "smooth-39", selected_id);
    replace_all(engine_json, kSmooth39Sha256, selected_sha256);
    const auto engine_path = isolated.path() / "engine.json";
    write_text(engine_path, engine_json);

    auto loaded = load_native_engine_input_from_builtin_catalog(engine_path,
                                                                technical_catalog_path);
    auto *input = std::get_if<NativeEngineInput>(&loaded);
    expect(input != nullptr && input->assets.size() == 2U,
           "exact PCM24 IR catalog selection did not resolve its payload");
    auto compiled_engine =
        compile::compile_engine(input->document, input->asset_views());
    auto *engine = std::get_if<compile::CompiledEngine>(&compiled_engine);
    expect(engine != nullptr,
           "exact PCM24 IR catalog selection did not compile an engine");

    auto scenario_document = load_native_scenario_input(
        source_root /
        "data/engines/bmw-m52b28/scenarios/inertial-dyno-1500-6500rpm.json");
    const auto *scenario_input =
        std::get_if<authoring::ScenarioDocument>(&scenario_document);
    expect(scenario_input != nullptr, "exact-selection session scenario did not load");
    auto compiled_scenario = compile::compile_scenario(*engine, *scenario_input);
    auto *scenario = std::get_if<compile::CompiledScenario>(&compiled_scenario);
    expect(scenario != nullptr,
           "exact PCM24 IR catalog selection did not compile a scenario");
    auto session_result = create_engine_session(
        *scenario, EngineSessionExecutionKind::finite_scenario);
    auto *session = std::get_if<EngineSession>(&session_result);
    expect(session != nullptr,
           "exact PCM24 IR catalog selection did not create a session");
    const auto first_block = session->process_block();
    expect(std::holds_alternative<EngineSessionBlockView>(first_block),
           "exact PCM24 IR catalog selection did not process its first 20 ms block");
}

void test_scenario_diagnostics(const std::filesystem::path &source_root) {
    const auto canonical_scenario = source_root / "data/engines/bmw-m52b28/scenarios/"
                                                  "inertial-dyno-1500-6500rpm.json";
    const auto valid = load_native_scenario_input(canonical_scenario);
    expect(std::holds_alternative<authoring::ScenarioDocument>(valid),
           "valid scenario input was rejected");

    IsolatedDirectory isolated;
    const auto invalid_path = isolated.path() / "scenario.json";
    write_text(invalid_path, "{}");
    const auto invalid = load_native_scenario_input(invalid_path);
    const auto &error =
        require_input_error(invalid, NativeInputErrorCode::invalid_scenario_document,
                            "invalid scenario was admitted");
    expect(error.kind == NativeInputErrorKind::data_error && error.diagnostics &&
               !error.diagnostics->diagnostics.empty() &&
               !error.diagnostics->diagnostics.front().json_pointer.empty(),
           "scenario parser diagnostics were not retained");
}

void test_output_preflight() {
    IsolatedDirectory isolated;
    const auto destination = isolated.path() / "render-01";
    const auto ready = preflight_native_output_directory(destination);
    const auto *output = std::get_if<NativeOutputDirectory>(&ready);
    expect(output != nullptr &&
               output->publication_root ==
                   std::filesystem::canonical(isolated.path()) &&
               output->publication_name == "render-01" &&
               !std::filesystem::exists(destination),
           "new output directory was not split without side effects");

    std::filesystem::create_directory(destination);
    const auto existing = preflight_native_output_directory(destination);
    const auto *existing_error = std::get_if<NativeOutputError>(&existing);
    expect(existing_error &&
               existing_error->kind == NativeOutputErrorKind::cant_create &&
               existing_error->code == NativeOutputErrorCode::destination_exists,
           "existing output destination was not rejected");

    const auto invalid =
        preflight_native_output_directory(isolated.path() / "bad output");
    expect(std::get<NativeOutputError>(invalid).code ==
               NativeOutputErrorCode::invalid_final_component,
           "nonportable output component was admitted");

    const auto missing_parent =
        preflight_native_output_directory(isolated.path() / "missing-parent/render-02");
    expect(std::get<NativeOutputError>(missing_parent).code ==
               NativeOutputErrorCode::parent_not_found,
           "missing output parent was admitted");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 6) {
            throw std::runtime_error(
                "expected repository root, technical and authoring catalogs, "
                "release identity, and configured install asset path arguments");
        }
        const auto source_root = std::filesystem::canonical(argv[1]);
        const auto catalog_path = std::filesystem::canonical(argv[2]);
        const auto authoring_catalog_path = std::filesystem::canonical(argv[3]);
        const std::string_view release_identity{argv[4]};
        const std::filesystem::path installed_asset_relative_path{argv[5]};
        if (installed_asset_relative_path.empty() ||
            installed_asset_relative_path.is_absolute()) {
            throw std::runtime_error("configured install asset path must be relative");
        }
        test_success_and_owned_views(source_root);
        test_missing_and_nonregular_assets(source_root);
        test_root_escape_and_symlink(source_root);
#if defined(__linux__)
        test_opened_root_descriptor_is_authority(source_root);
#endif
        test_resource_limits(source_root);
        test_builtin_catalog_covers_tracked_engines(source_root, catalog_path);
        test_builtin_catalog_strict_parser(source_root);
        test_builtin_catalog_engine_binding(source_root, catalog_path);
        test_builtin_catalog_discovery(installed_asset_relative_path);
        test_ir_authoring_catalog_binding(authoring_catalog_path, release_identity);
        test_exact_pcm24_catalog_selection_processes(source_root, catalog_path);
        test_scenario_diagnostics(source_root);
        test_output_preflight();
    } catch (const std::exception &error) {
        std::cerr << "native input files test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
