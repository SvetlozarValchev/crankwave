#pragma once

#include "engine_sim_offline/render.hpp"

#include <memory>
#include <variant>

namespace engine_sim_offline::render_detail {

class CompiledPresentationJob final {
  public:
    ~CompiledPresentationJob();
    CompiledPresentationJob(CompiledPresentationJob &&) noexcept;
    CompiledPresentationJob &operator=(CompiledPresentationJob &&) = delete;
    CompiledPresentationJob(const CompiledPresentationJob &) = delete;
    CompiledPresentationJob &operator=(const CompiledPresentationJob &) = delete;

    [[nodiscard]] contract::RenderResult
    execute(RenderSink &sink, const RenderSpecification &specification,
            const contract::RenderScenario &scenario,
            RenderControl control = {}) &&;

  private:
    class Implementation;

    explicit CompiledPresentationJob(
        std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> implementation_;

    friend std::variant<CompiledPresentationJob, contract::RenderFailure>
    compile_presentation_job(const RenderSpecification &,
                             const contract::RenderScenario &);
};

using CompiledPresentationJobResult =
    std::variant<CompiledPresentationJob, contract::RenderFailure>;

[[nodiscard]] CompiledPresentationJobResult
compile_presentation_job(const RenderSpecification &specification,
                         const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::render_detail
