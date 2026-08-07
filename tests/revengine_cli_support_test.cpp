#include "revengine_cli_support.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::cli;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

struct TemporaryDirectory {
    TemporaryDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned ordinal = 0; ordinal < 100; ++ordinal) {
            path = base / ("eso-revengine-cli-test-" + std::to_string(nonce) + "-" +
                           std::to_string(ordinal));
            std::error_code error;
            if (std::filesystem::create_directory(path, error)) {
                return;
            }
        }
        throw std::runtime_error{"could not create isolated test directory"};
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

    std::filesystem::path path;
};

void write_file(const std::filesystem::path &path, const std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"test file write failed"};
    }
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path &path) {
    const auto size = std::filesystem::file_size(path);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream stream{path, std::ios::binary};
    stream.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error{"test file read failed"};
    }
    return bytes;
}

[[nodiscard]] std::string hex(const contract::Sha256Digest &digest) {
    return sha256_lower_hex(digest);
}

[[nodiscard]] contract::Sha256Digest digest(const std::string_view bytes) {
    return contract::sha256(std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

void make_valid_package(const std::filesystem::path &root) {
    constexpr std::string_view runtime =
        "{\"schema\":\"engine-sim-offline/responsive-audio-preview\"}\n";
    write_file(root / "runtime.json", runtime);
    write_file(root / "audio" / "idle.pcm", "deterministic-audio-fixture");
    write_file(root / "revengine.json",
               "{\"schema\":\"engine-sim-offline/revengine-package\",\"version\":1,"
               "\"engine_id\":\"test-engine-v1\",\"runtime\":{"
               "\"kind\":\"responsive-audio\",\"manifest_path\":\"runtime.json\","
               "\"manifest_sha256\":\"" +
                   hex(digest(runtime)) + "\"}}\n");
}

[[nodiscard]] PackedRevengineFile require_pack(PackRevengineFileResult result) {
    if (const auto *error = std::get_if<RevengineCliError>(&result)) {
        throw std::runtime_error{"pack failed: " + error->message};
    }
    return std::get<PackedRevengineFile>(std::move(result));
}

[[nodiscard]] LoadedRevengineFile require_load(LoadRevengineFileResult result) {
    if (const auto *error = std::get_if<RevengineCliError>(&result)) {
        throw std::runtime_error{"load failed: " + error->message};
    }
    return std::get<LoadedRevengineFile>(std::move(result));
}

void test_pack_inspect_verify_and_no_overwrite() {
    TemporaryDirectory temporary;
    const auto package = temporary.path / "package";
    std::filesystem::create_directory(package);
    make_valid_package(package);
    const auto first_path = temporary.path / "first.revengine";
    const auto second_path = temporary.path / "second.revengine";

    const auto first =
        require_pack(pack_revengine_package_directory(package, first_path));
    expect(first.output_path == first_path && first.entry_count == 3 &&
               first.container_byte_count == std::filesystem::file_size(first_path) &&
               first.container_sha256 == contract::sha256(read_file(first_path)),
           "pack summary does not bind the exact published container");

    const auto inspected = require_load(inspect_revengine_file(first_path, false));
    expect(!inspected.fully_verified && inspected.index.entries.size() == 3 &&
               std::holds_alternative<std::monostate>(inspected.package),
           "structural inspection unexpectedly claimed package verification");
    const auto verified = require_load(inspect_revengine_file(first_path, true));
    const auto *descriptor =
        std::get_if<artifacts::RevenginePackageDescriptor>(&verified.package);
    expect(verified.fully_verified && descriptor != nullptr &&
               descriptor->engine_id == "test-engine-v1" &&
               descriptor->runtime.kind == "responsive-audio",
           "full verification did not retain the authenticated package descriptor");

    const auto second =
        require_pack(pack_revengine_package_directory(package, second_path));
    expect(first.container_sha256 == second.container_sha256 &&
               read_file(first_path) == read_file(second_path),
           "filesystem enumeration changed deterministic container bytes");

    const auto overwrite = pack_revengine_package_directory(package, first_path);
    const auto *overwrite_error = std::get_if<RevengineCliError>(&overwrite);
    expect(overwrite_error != nullptr &&
               overwrite_error->kind == RevengineCliErrorKind::cant_create,
           "pack command overwrote an existing output");
    expect(!std::filesystem::exists(temporary.path / ".first.revengine.tmp"),
           "successful publication left its temporary file behind");

    const auto nested =
        pack_revengine_package_directory(package, package / "nested.revengine");
    expect(std::holds_alternative<RevengineCliError>(nested),
           "output inside the source package tree was admitted");
}

void test_package_tree_rejections() {
    TemporaryDirectory temporary;

    const auto missing_descriptor = temporary.path / "missing";
    std::filesystem::create_directory(missing_descriptor);
    write_file(missing_descriptor / "runtime.json", "runtime");
    const auto missing = pack_revengine_package_directory(
        missing_descriptor, temporary.path / "missing.revengine");
    expect(std::holds_alternative<RevengineCliError>(missing),
           "package without revengine.json was admitted");

    const auto bad_digest = temporary.path / "bad-digest";
    std::filesystem::create_directory(bad_digest);
    make_valid_package(bad_digest);
    write_file(bad_digest / "runtime.json", "changed");
    const auto mismatch = pack_revengine_package_directory(
        bad_digest, temporary.path / "bad-digest.revengine");
    expect(std::holds_alternative<RevengineCliError>(mismatch),
           "runtime manifest hash mismatch was admitted");

    const auto nonportable = temporary.path / "nonportable";
    std::filesystem::create_directory(nonportable);
    make_valid_package(nonportable);
    write_file(nonportable / "Upper.bin", "x");
    const auto invalid_path = pack_revengine_package_directory(
        nonportable, temporary.path / "nonportable.revengine");
    expect(std::holds_alternative<RevengineCliError>(invalid_path),
           "nonportable package filename was admitted");

    const auto symlink_tree = temporary.path / "symlink";
    std::filesystem::create_directory(symlink_tree);
    make_valid_package(symlink_tree);
    std::error_code symlink_error;
    std::filesystem::create_symlink(symlink_tree / "runtime.json",
                                    symlink_tree / "linked.json", symlink_error);
    expect(!symlink_error, "test environment could not create a symlink");
    const auto linked = pack_revengine_package_directory(
        symlink_tree, temporary.path / "symlink.revengine");
    expect(std::holds_alternative<RevengineCliError>(linked),
           "package symlink was followed or admitted");
}

void test_corrupt_and_symlink_container_rejections() {
    TemporaryDirectory temporary;
    const auto package = temporary.path / "package";
    std::filesystem::create_directory(package);
    make_valid_package(package);
    const auto output = temporary.path / "valid.revengine";
    static_cast<void>(require_pack(pack_revengine_package_directory(package, output)));

    auto corrupt = read_file(output);
    corrupt.back() ^= std::byte{0x80};
    const auto corrupt_path = temporary.path / "corrupt.revengine";
    {
        std::ofstream stream{corrupt_path, std::ios::binary};
        stream.write(reinterpret_cast<const char *>(corrupt.data()),
                     static_cast<std::streamsize>(corrupt.size()));
    }
    expect(!std::holds_alternative<RevengineCliError>(
               inspect_revengine_file(corrupt_path, false)),
           "structural inspection unexpectedly authenticated payload bytes");
    expect(std::holds_alternative<RevengineCliError>(
               inspect_revengine_file(corrupt_path, true)),
           "full verification admitted corrupt container payload");

    const auto linked_path = temporary.path / "linked.revengine";
    std::filesystem::create_symlink(output, linked_path);
    const auto linked = inspect_revengine_file(linked_path, false);
    const auto *linked_error = std::get_if<RevengineCliError>(&linked);
    expect(linked_error != nullptr &&
               linked_error->kind == RevengineCliErrorKind::data_error,
           "container input symlink was followed");
}

} // namespace

int main() {
    try {
        test_pack_inspect_verify_and_no_overwrite();
        test_package_tree_rejections();
        test_corrupt_and_symlink_container_rejections();
    } catch (const std::exception &error) {
        std::cerr << "REVENGINE CLI support test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
