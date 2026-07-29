#include "engine_sim_offline/request_identity.hpp"

#include "identity/simulation_request_identity_writer.hpp"

#include <exception>
#include <new>
#include <string>
#include <utility>

namespace engine_sim_offline::identity {
namespace {

using detail::CanonicalJsonWriter;

[[nodiscard]] SimulationRequestIdentityError
writer_error(const CanonicalJsonWriter &writer) {
    auto code = SimulationRequestIdentityErrorCode::wire_unrepresentable;
    std::string detail_code = "simulation-request-identity-wire-unrepresentable";
    switch (writer.error()) {
    case CanonicalJsonWriter::Error::size_limit:
        code = SimulationRequestIdentityErrorCode::wire_size_exceeded;
        detail_code = "simulation-request-identity-wire-size-exceeded";
        break;
    case CanonicalJsonWriter::Error::invalid_utf8:
        code = SimulationRequestIdentityErrorCode::wire_invalid_utf8;
        detail_code = "simulation-request-identity-wire-invalid-utf8";
        break;
    case CanonicalJsonWriter::Error::non_finite_binary64:
        code = SimulationRequestIdentityErrorCode::wire_nonfinite;
        detail_code = "simulation-request-identity-wire-nonfinite";
        break;
    case CanonicalJsonWriter::Error::none:
    case CanonicalJsonWriter::Error::invalid_state:
    case CanonicalJsonWriter::Error::unsupported_value:
        break;
    }
    return {
        code,
        std::move(detail_code),
        std::string(writer.error_message()),
    };
}

[[nodiscard]] SimulationRequestIdentityError allocation_error() {
    return {
        SimulationRequestIdentityErrorCode::allocation_failure,
        "simulation-request-identity-encoding-allocation-failed",
        "canonical simulation request identity encoding ran out of memory",
    };
}

[[nodiscard]] SimulationRequestIdentityError
exception_error(const std::exception *exception) {
    std::string message = "canonical simulation request identity encoding threw";
    if (exception != nullptr) {
        message += ": ";
        message += exception->what();
    } else {
        message += " a non-standard exception";
    }
    return {
        SimulationRequestIdentityErrorCode::encoding_exception,
        "simulation-request-identity-encoding-threw",
        std::move(message),
    };
}

} // namespace

SimulationRequestIdentityEncodingResult
encode_simulation_request_identity_v3(const contract::EngineSpec &engine,
                                      const contract::RenderScenario &scenario,
                                      const contract::ProvenanceBundleRef &provenance) {
    try {
        CanonicalJsonWriter writer;
        std::vector<std::byte> bytes;
        const bool encoded =
            writer.begin_object() && writer.key("wire_schema") &&
            writer.string_value(kSimulationRequestIdentityWireSchemaV3) &&
            writer.key("engine") && detail::write_engine_spec(writer, engine) &&
            writer.key("scenario") && detail::write_render_scenario(writer, scenario) &&
            writer.key("provenance") &&
            detail::write_provenance_bundle_ref(writer, provenance) &&
            writer.end_object() && writer.finish(bytes);
        if (!encoded) {
            return writer_error(writer);
        }
        const auto sha256 = contract::sha256(bytes);
        return SimulationRequestIdentityEncoding{
            std::move(bytes),
            sha256,
        };
    } catch (const std::bad_alloc &) {
        return allocation_error();
    } catch (const std::exception &error) {
        return exception_error(&error);
    } catch (...) {
        return exception_error(nullptr);
    }
}

} // namespace engine_sim_offline::identity
