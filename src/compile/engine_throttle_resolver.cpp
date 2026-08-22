#include "compile/engine_resolver_internal.hpp"

#include <variant>

namespace crankwave::compile::detail::engine_resolution {

void resolve_throttle_controller(const ModelContext &context,
                                 ResolutionEmitter &emitter,
                                 contract::LowOrderEngineCoreV1 &core) {
    const auto &source = context.throttle_controller->kind;
    if (const auto *direct =
            std::get_if<authoring::DirectThrottleController>(&source)) {
        core.throttle_controller = contract::DirectThrottleControllerV1{
            emitter.authored(direct->gamma,
                             profile_path("throttle_controller.direct.gamma")),
        };
        return;
    }

    const auto &governor =
        std::get<authoring::GovernorThrottleController>(source);
    const auto base = profile_path("throttle_controller.governor");
    core.throttle_controller = contract::GovernorThrottleControllerV1{
        emitter.authored(legacy_si_value(governor.minimum_engine_speed),
                         base + ".minimum_engine_speed_rad_s"),
        emitter.authored(legacy_si_value(governor.maximum_engine_speed),
                         base + ".maximum_engine_speed_rad_s"),
        emitter.authored(governor.minimum_velocity,
                         base + ".minimum_velocity_per_s"),
        emitter.authored(governor.maximum_velocity,
                         base + ".maximum_velocity_per_s"),
        emitter.authored(governor.k_s, base + ".k_s"),
        emitter.authored(governor.k_d, base + ".k_d_per_s"),
        emitter.authored(governor.gamma, base + ".gamma"),
    };
}

} // namespace crankwave::compile::detail::engine_resolution
