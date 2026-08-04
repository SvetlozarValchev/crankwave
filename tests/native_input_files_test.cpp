#include "native_input_files.hpp"
#include "native_input_files_support.hpp"

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
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path &path, std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!stream) {
        throw std::runtime_error("could not write test file " + path.string());
    }
}

void replace_once(std::string &text, std::string_view before,
                  std::string_view after) {
    const auto position = text.find(before);
    if (position == std::string::npos ||
        text.find(before, position + before.size()) != std::string::npos) {
        throw std::runtime_error("test engine URI fixture was not unique");
    }
    text.replace(position, before.size(), after);
}

class IsolatedDirectory {
  public:
    IsolatedDirectory() {
        static std::atomic_uint64_t sequence{0};
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        for (unsigned attempt = 0; attempt < 32U; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("engine-sim-offline-native-input-" +
                     std::to_string(stamp) + "-" +
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
        auto json =
            read_text(source_root / "data/engines/bmw-m52b28/engine.json");
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
        auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        expect(std::holds_alternative<NativeEngineInput>(loaded),
               "valid engine input was rejected");
        owned = std::get<NativeEngineInput>(std::move(loaded));
    }

    expect(owned.document.engine.identity.id.value == "bmw-m52b28",
           "loader returned the wrong parsed engine");
    expect(!owned.source.bytes.empty() &&
               owned.source.sha256 == contract::sha256(owned.source.bytes),
           "loader did not retain the exact engine source identity");
    expect(owned.assets.size() == 2U,
           "loader did not own every declared asset");
    const auto views = owned.asset_views();
    expect(views.size() == 2U &&
               views[0].kind == compile::AssetKind::audio &&
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

void test_package_bake_sources(const std::filesystem::path &source_root) {
    const auto package_path =
        source_root /
        "data/engines/bmw-m52tub28-cleanroom/package-bake.json";
    auto loaded =
        load_native_package_bake_input(package_path, source_root);
    const auto *package = std::get_if<NativePackageBakeInput>(&loaded);
    expect(package != nullptr,
           "valid package bake input was rejected");
    expect(package->document.id.value ==
                   "bmw-m52tub28-cleanroom-normal-running" &&
               package->source.canonical_path ==
                   std::filesystem::canonical(package_path) &&
               package->source.sha256 ==
                   contract::sha256(package->source.bytes),
           "package loader did not retain the exact bake plan identity");
    expect(package->scenarios.size() == 4U &&
               package->scenarios[0].source_id == "coast-fall-source" &&
               package->scenarios[1].source_id == "part-rise-source" &&
               package->scenarios[2].source_id == "power-rise-source" &&
               package->scenarios[3].source_id == "idle-source",
           "package scenario sources lost authored order");
    for (const auto &scenario : package->scenarios) {
        expect(!scenario.source.bytes.empty() &&
                   scenario.source.sha256 ==
                       contract::sha256(scenario.source.bytes),
               "package scenario source identity is not exact");
    }
}

void test_missing_and_nonregular_assets(
    const std::filesystem::path &source_root) {
    {
        EngineFixture fixture(source_root);
        std::filesystem::remove(fixture.ir_path());
        const auto loaded =
            load_native_engine_input(fixture.engine_path(), fixture.root());
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::path_not_found,
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
        const auto &error = require_input_error(
            loaded, NativeInputErrorCode::asset_outside_root,
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
        std::filesystem::create_symlink(outside, fixture.ir_path(),
                                        symlink_error);
        if (!symlink_error) {
            const auto loaded =
                load_native_engine_input(fixture.engine_path(), fixture.root());
            require_input_error(
                loaded, NativeInputErrorCode::symbolic_link_not_allowed,
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
        require_input_error(loaded,
                            NativeInputErrorCode::invalid_local_asset_uri,
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
    auto opened =
        std::get<detail::OpenedAssetRoot>(std::move(opened_result));

    const auto original_root = fixture.root();
    const auto moved_root = fixture.isolated_root() / "opened-root";
    std::filesystem::rename(original_root, moved_root);
    write_text(original_root / "package/assets/ir.wav",
               "replacement-path-bytes");

    auto read = detail::read_confined_asset(
        opened, original_root / "package/engine.json", "assets/ir.wav",
        NativeInputSubject::audio_asset, "smooth-39", 1024U);
    const auto *file = std::get_if<detail::ReadFile>(&read);
    expect(file != nullptr &&
               payload_text(file->bytes) == "RIFF-owned-ir",
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
    require_input_error(
        count_limited, NativeInputErrorCode::asset_count_limit_exceeded,
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
    require_input_error(
        total_limited,
        NativeInputErrorCode::total_asset_bytes_limit_exceeded,
        "total asset byte limit was not enforced");
}

void test_scenario_diagnostics(const std::filesystem::path &source_root) {
    const auto canonical_scenario =
        source_root /
        "data/engines/bmw-m52b28/scenarios/"
        "inertial-dyno-1500-6500rpm.json";
    const auto valid = load_native_scenario_input(canonical_scenario);
    expect(std::holds_alternative<authoring::ScenarioDocument>(valid),
           "valid scenario input was rejected");

    IsolatedDirectory isolated;
    const auto invalid_path = isolated.path() / "scenario.json";
    write_text(invalid_path, "{}");
    const auto invalid = load_native_scenario_input(invalid_path);
    const auto &error = require_input_error(
        invalid, NativeInputErrorCode::invalid_scenario_document,
        "invalid scenario was admitted");
    expect(error.kind == NativeInputErrorKind::data_error &&
               error.diagnostics && !error.diagnostics->diagnostics.empty() &&
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
               existing_error->code ==
                   NativeOutputErrorCode::destination_exists,
           "existing output destination was not rejected");

    const auto invalid =
        preflight_native_output_directory(isolated.path() / "bad output");
    expect(std::get<NativeOutputError>(invalid).code ==
               NativeOutputErrorCode::invalid_final_component,
           "nonportable output component was admitted");

    const auto missing_parent = preflight_native_output_directory(
        isolated.path() / "missing-parent/render-02");
    expect(std::get<NativeOutputError>(missing_parent).code ==
               NativeOutputErrorCode::parent_not_found,
           "missing output parent was admitted");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected repository root argument");
        }
        const auto source_root = std::filesystem::canonical(argv[1]);
        test_success_and_owned_views(source_root);
        test_missing_and_nonregular_assets(source_root);
        test_root_escape_and_symlink(source_root);
#if defined(__linux__)
        test_opened_root_descriptor_is_authority(source_root);
#endif
        test_resource_limits(source_root);
        test_scenario_diagnostics(source_root);
        test_package_bake_sources(source_root);
        test_output_preflight();
    } catch (const std::exception &error) {
        std::cerr << "native input files test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
