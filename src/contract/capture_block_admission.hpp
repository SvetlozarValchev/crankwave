#pragma once

#include "crankwave/contract/capture.hpp"

namespace crankwave::contract::detail {

/**
 * Validate the block-varying portion of a capture view without allocating.
 *
 * The caller must already have validated the layout and must guarantee that the
 * identity storage borrowed by the view is immutable for the producer's lifetime.
 * A false result is deliberately diagnostic-free; callers should invoke the owning
 * public validate(CaptureBlockView) path only after failure.
 */
[[nodiscard]] bool
valid_capture_block_after_layout_admission(const CaptureBlockView &block) noexcept;

} // namespace crankwave::contract::detail
