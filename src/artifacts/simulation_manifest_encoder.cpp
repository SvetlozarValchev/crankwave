#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"

#include "simulation_manifest_encoder_impl.hpp"

#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::artifacts {
namespace detail {
namespace {

[[nodiscard]] bool
write_simulation_manifest_inputs(CanonicalJsonWriter &writer,
                                 const contract::SimulationManifestInputs &inputs) {
    const auto &resolved = inputs.resolved;
    return writer.begin_object() && writer.key("kind") &&
           writer.string_value("simulation_v1") && writer.key("value") &&
           writer.begin_object() && writer.key("resolved") && writer.begin_object() &&
           writer.key("engine") && write_engine_spec(writer, resolved.engine) &&
           writer.key("presentation") &&
           write_presentation_calibration(writer, resolved.presentation) &&
           writer.key("scenario") && write_render_scenario(writer, resolved.scenario) &&
           writer.end_object() && writer.end_object() && writer.end_object();
}

[[nodiscard]] RenderSinkError allocation_failure(std::string_view domain,
                                                 std::string_view subject) {
    return {
        RenderSinkErrorKind::publication_failure,
        std::string{domain} + "-encoding-allocation-failed",
        "canonical " + std::string{subject} + " encoding ran out of memory",
    };
}

[[nodiscard]] RenderSinkError exception_failure(std::string_view domain,
                                                std::string_view subject,
                                                const std::exception *exception) {
    auto message = "canonical " + std::string{subject} + " encoding threw";
    if (exception != nullptr) {
        message += ": ";
        message += exception->what();
    } else {
        message += " a non-standard exception";
    }
    return {
        RenderSinkErrorKind::publication_failure,
        std::string{domain} + "-encoding-threw",
        std::move(message),
    };
}

} // namespace
} // namespace detail

ManifestEncodingResult
encode_simulation_manifest_v2(const contract::RenderManifest &manifest) {
    if (!manifest.execution.has_value()) {
        return RenderSinkError{
            RenderSinkErrorKind::protocol_violation,
            "simulation-manifest-execution-missing",
            "a completed simulation manifest requires execution facts",
        };
    }

    try {
        detail::CanonicalJsonWriter writer;
        std::vector<std::byte> bytes;
        const bool encoded =
            writer.begin_object() && writer.key("wire_schema") &&
            writer.string_value(kSimulationManifestWireSchemaV2) &&
            writer.key("content") &&
            detail::write_completed_manifest_content(
                writer, manifest.content, detail::write_simulation_manifest_inputs) &&
            writer.key("execution") &&
            detail::write_execution_facts(writer, *manifest.execution) &&
            writer.end_object() && writer.finish(bytes);
        if (!encoded) {
            return detail::manifest_writer_error(writer, "simulation-manifest");
        }
        return ManifestEncoding{std::move(bytes)};
    } catch (const std::bad_alloc &) {
        return detail::allocation_failure("simulation-manifest", "simulation manifest");
    } catch (const std::exception &error) {
        return detail::exception_failure("simulation-manifest", "simulation manifest",
                                         &error);
    } catch (...) {
        return detail::exception_failure("simulation-manifest", "simulation manifest",
                                         nullptr);
    }
}

SimulationRequestIdentityEncodingResult
encode_simulation_request_identity_v1(const contract::EngineSpec &engine,
                                      const contract::RenderScenario &scenario,
                                      const contract::ProvenanceBundleRef &provenance) {
    try {
        detail::CanonicalJsonWriter writer;
        std::vector<std::byte> bytes;
        const bool encoded =
            writer.begin_object() && writer.key("wire_schema") &&
            writer.string_value(kSimulationRequestIdentityWireSchemaV1) &&
            writer.key("engine") && detail::write_engine_spec(writer, engine) &&
            writer.key("scenario") && detail::write_render_scenario(writer, scenario) &&
            writer.key("provenance") &&
            detail::write_provenance_bundle_ref(writer, provenance) &&
            writer.end_object() && writer.finish(bytes);
        if (!encoded) {
            return detail::manifest_writer_error(writer, "simulation-request-identity");
        }
        const auto sha256 = contract::sha256(bytes);
        return SimulationRequestIdentityEncoding{std::move(bytes), sha256};
    } catch (const std::bad_alloc &) {
        return detail::allocation_failure("simulation-request-identity",
                                          "simulation request identity");
    } catch (const std::exception &error) {
        return detail::exception_failure("simulation-request-identity",
                                         "simulation request identity", &error);
    } catch (...) {
        return detail::exception_failure("simulation-request-identity",
                                         "simulation request identity", nullptr);
    }
}

} // namespace engine_sim_offline::artifacts
