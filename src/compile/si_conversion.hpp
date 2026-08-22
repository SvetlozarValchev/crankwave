#pragma once

#include "crankwave/compile.hpp"

#include <string_view>

namespace crankwave::compile::detail {

using SiQuantityResult = CompileResult<SiQuantity>;
using SiRateResult = CompileResult<SiRate>;

[[nodiscard]] SiQuantityResult
convert_quantity_to_si(const authoring::Quantity &quantity,
                       authoring::QuantityDimension expected_dimension,
                       std::string_view json_pointer) noexcept;

[[nodiscard]] SiRateResult convert_rate_to_si(const authoring::RationalRate &rate,
                                              std::string_view json_pointer) noexcept;

} // namespace crankwave::compile::detail
