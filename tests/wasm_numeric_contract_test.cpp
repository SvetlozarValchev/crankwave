#include "engine_sim_offline/contract/common.hpp"
#include "numeric/target_extended_precision.hpp"
#include "presentation/presentation_method_registry.hpp"

#include <optional>
#include <stdexcept>
#include <string_view>

#if !defined(__wasm32__)
#error "wasm_numeric_contract_test.cpp must be compiled for wasm32"
#endif

namespace {

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

} // namespace

int main() {
    using namespace engine_sim_offline;

    expect(numeric::target_extended_precision_format_is_admitted(),
           "wasm32 IEEE binary128 format was not admitted");
    expect(numeric::kTargetExtendedPrecisionIdentity ==
               "wasm32-ieee754-binary128-strict-v1",
           "wasm32 extended-precision identity changed");

    const auto &methods = presentation::implemented_presentation_method_identities();
    expect(methods.impulse_response_conversion.id ==
               "static-ir-blackman-sinc-24tap-4096phase-44100-to-192000-"
               "binary64-wasm32-binary128-v1",
           "wasm32 static-IR method ID does not identify binary128");
    expect(methods.audition_mix.id ==
               "ordered-n-route-serial-float32-quarter-sine-pcm24-wave-master-"
               "wasm32-binary128-v2",
           "wasm32 audition method ID does not identify binary128 duration "
           "resolution");

    expect(presentation::static_ir_conversion_method_descriptor().find(
               "extended_execution=wasm32-ieee754-binary128-radix2-113-"
               "significand-bits") != std::string_view::npos,
           "wasm32 static-IR descriptor omits its binary128 execution");
    expect(presentation::ordered_route_audition_method_descriptor().find(
               "duration_resolution_arithmetic=wasm32-ieee754-binary128") !=
               std::string_view::npos,
           "wasm32 audition descriptor omits binary128 duration resolution");

    expect(contract::resolve_frame_index(1001.0 / 30000.0,
                                         contract::RationalRateHz{30000, 1001}) ==
               std::optional<std::uint64_t>{1},
           "wasm32 binary128 duration resolution rejected an integral frame");
    expect(!contract::resolve_frame_index(0.5 / 48000.0,
                                          contract::RationalRateHz{48000, 1})
                .has_value(),
           "wasm32 binary128 duration resolution rounded a half frame");
    return 0;
}
