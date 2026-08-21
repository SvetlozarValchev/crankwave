#pragma once

#include "engine_sim_offline/responsive/native_package.hpp"

#include <filesystem>
#include <stop_token>
#include <variant>

namespace engine_sim_offline::responsive {

struct NativeResponsiveDirectoryPublication {
    std::filesystem::path path;
    std::size_t member_count = 0;

    friend bool operator==(const NativeResponsiveDirectoryPublication &,
                           const NativeResponsiveDirectoryPublication &) = default;
};

struct NativeResponsiveCarrierPublication {
    std::filesystem::path path;
    std::uint64_t byte_count = 0;
    contract::Sha256Digest sha256;

    friend bool operator==(const NativeResponsiveCarrierPublication &,
                           const NativeResponsiveCarrierPublication &) = default;
};

using NativeResponsiveDirectoryPublicationResult =
    std::variant<NativeResponsiveDirectoryPublication, NativeResponsivePackageError>;
using NativeResponsiveCarrierPublicationResult =
    std::variant<NativeResponsiveCarrierPublication, NativeResponsivePackageError>;

// Publishes a package tree beneath an existing real directory. The destination is
// created with no-replace semantics after a fully synced private sibling stage is
// complete. Cancellation/failure removes only that inode-scoped private stage.
[[nodiscard]] NativeResponsiveDirectoryPublicationResult
publish_native_responsive_package_atomic(const NativeResponsivePackageV2 &package,
                                         const std::filesystem::path &publication_root,
                                         std::string publication_name,
                                         std::stop_token stop_token = {});

// Publishes the already-built carrier independently with no-replace semantics.
// Directory and carrier publication are separate atomic operations by design.
[[nodiscard]] NativeResponsiveCarrierPublicationResult
publish_native_vehicleengine_atomic(const NativeResponsivePackageV2 &package,
                                const std::filesystem::path &output_file,
                                std::stop_token stop_token = {});

} // namespace engine_sim_offline::responsive
