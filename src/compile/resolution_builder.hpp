#pragma once

#include "engine_sim_offline/compile.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine_sim_offline::compile::detail {

using ProvenanceBuildResult = CompileResult<contract::ProvenanceLedger>;

class ResolutionProvenanceBuilder final {
  public:
    explicit ResolutionProvenanceBuilder(
        std::string scope, contract::ProvenanceLedger base = {});

    void add_authored(std::string resolved_parameter_path);

    void add_derived(std::string resolved_parameter_path,
                     contract::MethodIdentity method,
                     std::span<const std::string_view> dependency_parameter_paths);

    [[nodiscard]] ProvenanceBuildResult finish() && noexcept;

  private:
    struct PendingResolution {
        std::string parameter_path;
        contract::ResolutionMode mode = contract::ResolutionMode::authored;
        std::optional<contract::MethodIdentity> method;
        std::vector<std::string> dependency_parameter_paths;
    };

    std::string scope_;
    contract::ProvenanceLedger base_;
    std::vector<PendingResolution> resolutions_;
};

} // namespace engine_sim_offline::compile::detail
