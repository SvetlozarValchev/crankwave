#include "reference/bmw_m52b28_torque_sweep_evidence.hpp"

#include "artifacts/secure_filesystem_support.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using namespace engine_sim_offline;
using artifacts::detail::FileDescriptor;

[[nodiscard]] std::string
validation_report_text(const contract::ValidationReport &report) {
    std::string text;
    for (const auto &issue : report.issues) {
        text += "\n  ";
        text += issue.path;
        text += ": ";
        text += issue.message;
    }
    return text;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::string result(64U, '0');
    for (std::size_t index = 0U; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kHexDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kHexDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] profiles::BmwM52b28FullThrottleTorqueSweepRequestSet
canonical_requests() {
    auto result = profiles::make_bmw_m52b28_full_throttle_torque_sweep_request_set();
    if (auto *requests =
            std::get_if<profiles::BmwM52b28FullThrottleTorqueSweepRequestSet>(
                &result)) {
        return std::move(*requests);
    }
    throw std::runtime_error{
        "canonical BMW torque-sweep request construction failed" +
        validation_report_text(std::get<contract::ValidationReport>(result))};
}

[[noreturn]] void
throw_evidence_error(const reference::BmwM52b28TorqueSweepEvidenceError &error) {
    std::string message = error.detail_code + ": " + error.message;
    if (error.point_index < profiles::kBmwM52b28FullThrottleTorqueSweepPointCount) {
        message += " (point ";
        message += std::to_string(error.point_index + 1U);
        message += ", ";
        message += std::to_string(static_cast<std::uint32_t>(
            profiles::kBmwM52b28FullThrottleTorqueSweepEngineSpeedsRpm
                [error.point_index]));
        message += " rpm)";
    }
    throw std::runtime_error{std::move(message)};
}

void sync_file(int descriptor, std::string_view description) {
    while (::fsync(descriptor) == -1) {
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error{artifacts::detail::errno_message(
            "could not sync " + std::string{description}, errno)};
    }
}

void write_file(int staging_fd, std::string_view filename,
                std::span<const std::byte> bytes) {
    auto created = artifacts::detail::create_file_beneath(staging_fd, filename);
    if (const auto *sink_error = std::get_if<RenderSinkError>(&created)) {
        throw std::runtime_error{sink_error->detail_code + ": " + sink_error->message};
    }
    auto file = std::move(std::get<FileDescriptor>(created));
    if (!artifacts::detail::write_all_at(file.get(), 0U, bytes)) {
        throw std::runtime_error{artifacts::detail::errno_message(
            "could not write staged evidence file", errno)};
    }
    sync_file(file.get(), "staged evidence file");
}

void preflight_new_output_directory(const std::filesystem::path &output_directory) {
    const auto publication_name = output_directory.filename().string();
    auto publication_parent = output_directory.parent_path();
    if (output_directory.empty() || publication_name.empty() ||
        publication_name == "." || publication_name == "..") {
        throw std::invalid_argument{
            "new output directory must end in one ordinary name component"};
    }
    if (publication_name.front() == '-') {
        throw std::invalid_argument{
            "publication directory name must not begin with '-'"};
    }
    if (publication_parent.empty()) {
        publication_parent = ".";
    }
    FileDescriptor parent(::open(publication_parent.c_str(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent.valid()) {
        throw std::invalid_argument{artifacts::detail::errno_message(
            "output parent must be an existing non-symbolic-link directory", errno)};
    }
    struct stat destination_status{};
    if (::fstatat(parent.get(), publication_name.c_str(), &destination_status,
                  AT_SYMLINK_NOFOLLOW) == 0) {
        throw std::invalid_argument{
            "output destination already exists; evidence is never merged or "
            "overwritten"};
    }
    if (errno != ENOENT) {
        throw std::invalid_argument{artifacts::detail::errno_message(
            "could not preflight output destination", errno)};
    }
}

void publish_atomically(
    const std::filesystem::path &output_directory,
    const reference::BmwM52b28TorqueSweepEvidenceEncoding &encoding) {
    const auto publication_name = output_directory.filename().string();
    auto publication_parent = output_directory.parent_path();
    if (output_directory.empty() || publication_name.empty() ||
        publication_name == "." || publication_name == "..") {
        throw std::invalid_argument{
            "new output directory must end in one ordinary name component"};
    }
    if (publication_parent.empty()) {
        publication_parent = ".";
    }

    FileDescriptor parent(::open(publication_parent.c_str(),
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent.valid()) {
        throw std::invalid_argument{artifacts::detail::errno_message(
            "output parent must be an existing non-symbolic-link directory", errno)};
    }

    std::string staging_name;
    for (std::size_t attempt = 0U; attempt < 16U; ++attempt) {
        staging_name = artifacts::detail::random_stage_name();
        if (::mkdirat(parent.get(), staging_name.c_str(), 0700) == 0) {
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error{artifacts::detail::errno_message(
                "could not create evidence staging directory", errno)};
        }
        staging_name.clear();
    }
    if (staging_name.empty()) {
        throw std::runtime_error{
            "could not allocate a unique evidence staging directory"};
    }

    FileDescriptor staging(::openat(parent.get(), staging_name.c_str(),
                                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    struct stat staging_status{};
    if (!staging.valid() || ::fstat(staging.get(), &staging_status) == -1) {
        const auto failure_errno = errno;
        static_cast<void>(artifacts::detail::remove_tree_entry_at(
            parent.get(), staging_name.c_str()));
        throw std::runtime_error{artifacts::detail::errno_message(
            "could not open owned evidence staging directory", failure_errno)};
    }

    bool renamed = false;
    try {
        write_file(staging.get(), reference::kBmwM52b28TorqueSweepEvidenceFilename,
                   encoding.bytes);
        write_file(staging.get(),
                   reference::kBmwM52b28TorqueSweepEvidenceSha256Filename,
                   encoding.sha256_sidecar_bytes);
        const auto inventory =
            artifacts::detail::inventory_directory_tree(staging.get(), 3U);
        const auto *entries =
            std::get_if<std::vector<artifacts::detail::DirectoryTreeEntry>>(&inventory);
        const bool has_json =
            entries != nullptr && std::ranges::any_of(*entries, [](const auto &entry) {
                return !entry.directory &&
                       entry.relative_path ==
                           reference::kBmwM52b28TorqueSweepEvidenceFilename;
            });
        const bool has_sha256 =
            entries != nullptr && std::ranges::any_of(*entries, [](const auto &entry) {
                return !entry.directory &&
                       entry.relative_path ==
                           reference::kBmwM52b28TorqueSweepEvidenceSha256Filename;
            });
        if (entries == nullptr || entries->size() != 2U ||
            std::ranges::any_of(*entries,
                                [](const auto &entry) { return entry.directory; }) ||
            !has_json || !has_sha256) {
            throw std::runtime_error{
                "staging inventory is not exactly the two frozen evidence files"};
        }
        if (!artifacts::detail::sync_directory_tree(staging.get())) {
            throw std::runtime_error{artifacts::detail::errno_message(
                "could not sync evidence staging directory", errno)};
        }
        staging.reset();
        if (artifacts::detail::rename_noreplace(parent.get(), staging_name.c_str(),
                                                publication_name.c_str()) == -1) {
            throw std::runtime_error{artifacts::detail::errno_message(
                "could not atomically publish the new evidence directory", errno)};
        }
        renamed = true;
        sync_file(parent.get(), "evidence publication parent directory");
    } catch (...) {
        // Remove only the directory whose device/inode identity was captured after
        // this process exclusively created it. This never follows a replaced name.
        static_cast<void>(artifacts::detail::remove_tree_entry_by_identity(
            parent.get(), static_cast<std::uintmax_t>(staging_status.st_dev),
            static_cast<std::uintmax_t>(staging_status.st_ino)));
        throw;
    }
    if (!renamed) {
        throw std::logic_error{"evidence publication transaction did not rename"};
    }
}

void print_summary(const reference::BmwM52b28TorqueSweepEvidence &evidence,
                   const reference::BmwM52b28TorqueSweepEvidenceEncoding &encoding,
                   const std::filesystem::path &output_directory) {
    std::cout << "\nBMW M52B28 canonical full-throttle torque sweep\n\n"
              << "  RPM     indicated       loss    starter        net   BMEP bar"
                 "   power kW  first cyc   last cyc    time ms\n";
    std::cout << std::fixed;
    for (const auto &point : evidence.points) {
        std::cout << std::setw(5) << std::setprecision(0) << point.engine_speed_rpm
                  << std::setw(14) << std::setprecision(3)
                  << point.indicated_gas_cycle_mean_torque_nm << std::setw(11)
                  << point.aggregate_loss_cycle_mean_torque_nm << std::setw(11)
                  << point.starter_cycle_mean_torque_nm << std::setw(11)
                  << point.net_shaft_cycle_mean_torque_nm << std::setw(11)
                  << point.net_bmep_pa / 100000.0 << std::setw(11)
                  << point.mean_power_w / 1000.0 << std::setw(11)
                  << point.sample_first_cycle << std::setw(11)
                  << point.sample_last_cycle << std::setw(11)
                  << static_cast<double>(point.elapsed_ns) / 1000000.0 << '\n';
    }
    const auto &comparison = evidence.comparisons;
    std::cout << "\nLandmarks (comparison only; not BMW calibration):\n"
              << "  3950 rpm net torque / 280 N*m: " << std::setprecision(6)
              << comparison.torque_at_3950_to_280_ratio << '\n'
              << "  5300 rpm mean power / 142 kW: "
              << comparison.power_at_5300_to_142000_ratio << '\n'
              << "  sampled max torque: " << std::setprecision(3)
              << comparison.sampled_maximum_torque_nm << " N*m at "
              << std::setprecision(0) << comparison.sampled_maximum_torque_rpm
              << " rpm (ratio " << std::setprecision(6)
              << comparison.sampled_maximum_torque_to_280_ratio << ")\n"
              << "  sampled max power: " << std::setprecision(3)
              << comparison.sampled_maximum_power_w / 1000.0 << " kW at "
              << std::setprecision(0) << comparison.sampled_maximum_power_rpm
              << " rpm (ratio " << std::setprecision(6)
              << comparison.sampled_maximum_power_to_142000_ratio << ")\n";
    if (evidence.warnings.empty()) {
        std::cout << "  warning-only gross-error tripwires: none\n";
    } else {
        for (const auto &warning : evidence.warnings) {
            std::cout << "  WARNING: " << warning << '\n';
        }
    }
    std::cout << "\npoint_count=" << evidence.points.size() << '\n'
              << "total_seconds=" << std::setprecision(3)
              << static_cast<double>(evidence.total_elapsed_ns) / 1000000000.0 << '\n'
              << "output=" << output_directory.string() << '\n'
              << "json="
              << (output_directory / reference::kBmwM52b28TorqueSweepEvidenceFilename)
                     .string()
              << '\n'
              << "sha256=" << digest_hex(encoding.sha256) << '\n';
}

int run(int argc, char **argv) {
    if (argc != 2) {
        throw std::invalid_argument{"usage: engine-sim-offline-m4-bmw-torque-sweep "
                                    "<new-output-directory>"};
    }
    const std::filesystem::path output_directory{argv[1]};
    preflight_new_output_directory(output_directory);
    auto requests = canonical_requests();
    auto evidence_result =
        reference::run_bmw_m52b28_full_throttle_torque_sweep(requests);
    const auto *evidence =
        std::get_if<reference::BmwM52b28TorqueSweepEvidence>(&evidence_result);
    if (evidence == nullptr) {
        throw_evidence_error(
            std::get<reference::BmwM52b28TorqueSweepEvidenceError>(evidence_result));
    }
    auto encoding_result =
        reference::encode_bmw_m52b28_torque_sweep_evidence(*evidence);
    const auto *encoding =
        std::get_if<reference::BmwM52b28TorqueSweepEvidenceEncoding>(&encoding_result);
    if (encoding == nullptr) {
        throw_evidence_error(
            std::get<reference::BmwM52b28TorqueSweepEvidenceError>(encoding_result));
    }
    publish_atomically(output_directory, *encoding);
    print_summary(*evidence, *encoding, output_directory);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "M4 BMW torque sweep failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
