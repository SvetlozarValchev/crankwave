#ifndef ENGINE_SIM_OFFLINE_TESTS_WASM_PARITY_DRIVER_H
#define ENGINE_SIM_OFFLINE_TESTS_WASM_PARITY_DRIVER_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Test-only adapter. Its implementation invokes only the public C ABI and
 * emits one target-neutral parity bundle:
 *
 *   8-byte magic, four little-endian u32 counts,
 *   UTF-8 semantic metadata, Float64 telemetry values, Float32 PCM.
 *
 * Every input/output extent is fixed-width so Node can invoke the same adapter
 * in wasm32 without mirroring host size_t layouts.
 */
uint32_t eso_wasm_parity_run(
    const uint8_t *engine_json, uint32_t engine_json_size, const uint8_t *scenario_json,
    uint32_t scenario_json_size, const uint8_t *impulse_response_id,
    uint32_t impulse_response_id_size, const uint8_t *impulse_response_bytes,
    uint32_t impulse_response_byte_count, const uint8_t *accessory_configuration_id,
    uint32_t accessory_configuration_id_size,
    const uint8_t *accessory_configuration_bytes,
    uint32_t accessory_configuration_byte_count, uint8_t *output,
    uint32_t output_capacity, uint32_t *out_output_size);

#if defined(__cplusplus)
}
#endif

#endif
