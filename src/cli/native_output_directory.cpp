#include "native_input_files_support.hpp"

#include <new>
#include <utility>

namespace crankwave::cli::detail {

bool valid_portable_component(std::string_view component) noexcept {
    if (component.empty() || component.size() > 255U || component == "." ||
        component == "..") {
        return false;
    }
    const auto alphanumeric = [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z') ||
               (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    if (!alphanumeric(static_cast<unsigned char>(component.front())) ||
        component.back() == '.') {
        return false;
    }
    for (std::size_t index = 0; index < component.size(); ++index) {
        const auto byte = static_cast<unsigned char>(component[index]);
        if (alphanumeric(byte) || byte == '-' || byte == '_' || byte == '.') {
            continue;
        }
        if (byte == '%' && index + 2U < component.size() &&
            component[index + 1U] == '2' && component[index + 2U] == 'f') {
            index += 2U;
            continue;
        }
        return false;
    }

    auto stem = component.substr(0, component.find('.'));
    std::string folded;
    folded.reserve(stem.size());
    for (const char character : stem) {
        folded.push_back(character >= 'a' && character <= 'z'
                             ? static_cast<char>(character - 'a' + 'A')
                             : character);
    }
    if (folded == "CON" || folded == "PRN" || folded == "AUX" ||
        folded == "NUL") {
        return false;
    }
    return !(folded.size() == 4U &&
             (folded.starts_with("COM") || folded.starts_with("LPT")) &&
             folded.back() >= '1' && folded.back() <= '9');
}

NativeOutputError output_error(NativeOutputErrorKind kind,
                               NativeOutputErrorCode code,
                               std::filesystem::path path,
                               std::string message) {
    return {kind, code, std::move(path), std::move(message)};
}

} // namespace crankwave::cli::detail

namespace crankwave::cli {

NativeOutputDirectoryResult preflight_native_output_directory(
    const std::filesystem::path &output_directory) {
    try {
        if (output_directory.empty()) {
            return detail::output_error(NativeOutputErrorKind::cant_create,
                                        NativeOutputErrorCode::empty_path,
                                        output_directory,
                                        "output directory must not be empty");
        }
        const auto leaf = output_directory.filename().string();
        if (!detail::valid_portable_component(leaf)) {
            return detail::output_error(
                NativeOutputErrorKind::cant_create,
                NativeOutputErrorCode::invalid_final_component,
                output_directory,
                "output directory must end in one conservative portable component");
        }

        auto parent = output_directory.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        std::error_code filesystem_error;
        const auto parent_status =
            std::filesystem::symlink_status(parent, filesystem_error);
        if (filesystem_error ||
            parent_status.type() == std::filesystem::file_type::not_found) {
            const auto code = !filesystem_error ||
                                      detail::not_found(filesystem_error)
                                  ? NativeOutputErrorCode::parent_not_found
                                  : NativeOutputErrorCode::
                                        parent_canonicalization_failed;
            return detail::output_error(
                NativeOutputErrorKind::cant_create, code, parent,
                "output parent directory is unavailable");
        }

        const auto canonical_parent =
            std::filesystem::canonical(parent, filesystem_error);
        if (filesystem_error) {
            return detail::output_error(
                NativeOutputErrorKind::cant_create,
                NativeOutputErrorCode::parent_canonicalization_failed, parent,
                "output parent directory could not be resolved");
        }
        const auto canonical_parent_status =
            std::filesystem::status(canonical_parent, filesystem_error);
        if (filesystem_error ||
            !std::filesystem::is_directory(canonical_parent_status)) {
            return detail::output_error(
                NativeOutputErrorKind::cant_create,
                NativeOutputErrorCode::parent_not_directory, canonical_parent,
                "output parent is not a directory");
        }

        const auto destination = canonical_parent / leaf;
        const auto destination_status =
            std::filesystem::symlink_status(destination, filesystem_error);
        if (filesystem_error && !detail::not_found(filesystem_error)) {
            return detail::output_error(
                NativeOutputErrorKind::temp_fail,
                NativeOutputErrorCode::destination_status_failed, destination,
                "output destination could not be inspected");
        }
        if (!filesystem_error &&
            destination_status.type() != std::filesystem::file_type::not_found) {
            return detail::output_error(
                NativeOutputErrorKind::cant_create,
                NativeOutputErrorCode::destination_exists, destination,
                "output destination already exists and will not be overwritten");
        }
        return NativeOutputDirectory{canonical_parent, leaf};
    } catch (const std::bad_alloc &) {
        return detail::output_error(NativeOutputErrorKind::temp_fail,
                                    NativeOutputErrorCode::filesystem_failure,
                                    output_directory,
                                    "output preflight allocation failed");
    } catch (const std::filesystem::filesystem_error &) {
        return detail::output_error(
            NativeOutputErrorKind::temp_fail,
            NativeOutputErrorCode::filesystem_failure, output_directory,
            "output preflight filesystem operation failed");
    } catch (...) {
        return detail::output_error(
            NativeOutputErrorKind::temp_fail,
            NativeOutputErrorCode::filesystem_failure, output_directory,
            "output preflight failed unexpectedly");
    }
}

} // namespace crankwave::cli
