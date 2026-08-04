#include "engine_sim_offline/artifacts/audio_package_directory_publisher.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

class IsolatedDirectory final {
  public:
    IsolatedDirectory() {
        static std::atomic_uint64_t sequence{0U};
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        for (unsigned attempt = 0U; attempt < 32U; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("engine-sim-offline-package-publish-" +
                     std::to_string(stamp) + "-" +
                     std::to_string(sequence.fetch_add(1U)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        throw std::runtime_error{"could not create isolated directory"};
    }

    ~IsolatedDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto view = std::as_bytes(std::span{text});
    return {view.begin(), view.end()};
}

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error{"could not read published file"};
    }
    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    return bytes(text);
}

void test_atomic_new_directory_publication() {
    IsolatedDirectory isolated;
    const auto wave_a = bytes("wave-a");
    const auto wave_b = bytes("wave-b");
    const auto manifest = bytes("{\"schema\":\"test\"}\n");
    const std::vector<AudioPackagePublicationFileView> files{
        {"audio/idle/master.wav", wave_a},
        {"audio/running/power/master.wav", wave_b},
    };

    const auto published = publish_audio_package_directory(
        isolated.path(), "bmw-package", files, manifest);
    const auto *success =
        std::get_if<AudioPackageDirectoryPublication>(&published);
    const auto destination = isolated.path() / "bmw-package";
    expect(success != nullptr && success->publication_path == destination &&
               success->manifest_sha256 == contract::sha256(manifest),
           "valid package was not atomically published");
    expect(read_bytes(destination / "package.json") == manifest &&
               read_bytes(destination / files[0].relative_path) == wave_a &&
               read_bytes(destination / files[1].relative_path) == wave_b,
           "published package bytes changed");

    const auto repeated = publish_audio_package_directory(
        isolated.path(), "bmw-package", files, manifest);
    const auto *repeat_error = std::get_if<RenderSinkError>(&repeated);
    expect(repeat_error != nullptr &&
               repeat_error->detail_code ==
                   "audio-package-publication-destination-exists" &&
               read_bytes(destination / "package.json") == manifest,
           "existing package destination was overwritten");

    const std::vector<AudioPackagePublicationFileView> invalid{
        {"../escape.wav", wave_a},
    };
    const auto rejected = publish_audio_package_directory(
        isolated.path(), "invalid-package", invalid, manifest);
    expect(std::holds_alternative<RenderSinkError>(rejected) &&
               !std::filesystem::exists(isolated.path() / "invalid-package"),
           "unsafe package payload path was admitted");
    for (const auto &entry : std::filesystem::directory_iterator(isolated.path())) {
        expect(!entry.path().filename().string().starts_with(
                   ".engine-sim-offline-stage-"),
               "publication left a private staging directory behind");
    }
}

} // namespace

int main() {
    try {
        test_atomic_new_directory_publication();
    } catch (const std::exception &error) {
        std::cerr << "audio package publication test failed: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
