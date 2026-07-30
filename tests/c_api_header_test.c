#include "engine_sim_offline/c_api.h"

#include <stddef.h>
#include <stdint.h>

_Static_assert(ESO_C_API_VERSION == 1, "unexpected C ABI version");
_Static_assert(sizeof(eso_engine_handle_t) == sizeof(uint64_t),
               "engine handle width changed");
_Static_assert(sizeof(eso_scenario_handle_t) == sizeof(uint64_t),
               "scenario handle width changed");
_Static_assert(sizeof(eso_session_handle_t) == sizeof(uint64_t),
               "session handle width changed");

int main(void) {
    eso_context_t *context = (eso_context_t *)(uintptr_t)1;
    if (eso_context_create(ESO_C_API_VERSION + 1U, &context) !=
            ESO_STATUS_ABI_VERSION_MISMATCH ||
        context != NULL) {
        return 1;
    }
    if (eso_context_create(ESO_C_API_VERSION, &context) != ESO_STATUS_OK ||
        context == NULL) {
        return 2;
    }

    eso_abi_layout_t layout = {0};
    if (eso_get_abi_layout(&layout) != ESO_STATUS_OK ||
        layout.api_version != ESO_C_API_VERSION ||
        layout.pointer_size_bytes != sizeof(void *) ||
        layout.size_type_size_bytes != sizeof(size_t) ||
        layout.float_size_bytes != sizeof(float) ||
        layout.double_size_bytes != sizeof(double)) {
        return 3;
    }

    {
        static const char malformed[] = "{";
        const eso_utf8_view_t json = {malformed, sizeof(malformed) - 1U};
        eso_engine_handle_t engine = ESO_INVALID_HANDLE;
        if (eso_compile_engine_json(context, json, NULL, 0U, &engine) !=
                ESO_STATUS_ENGINE_PARSE_FAILED ||
            engine != ESO_INVALID_HANDLE) {
            return 4;
        }

        eso_error_info_t error = {0};
        if (eso_context_get_last_error(context, &error) != ESO_STATUS_OK ||
            error.status != ESO_STATUS_ENGINE_PARSE_FAILED ||
            error.stage != ESO_ERROR_STAGE_ENGINE_PARSE ||
            error.diagnostic_count == 0U) {
            return 5;
        }

        eso_diagnostic_info_t diagnostic = {0};
        if (eso_context_get_diagnostic(context, 0U, &diagnostic) != ESO_STATUS_OK ||
            diagnostic.severity != ESO_DIAGNOSTIC_ERROR) {
            return 6;
        }
    }

    if (eso_context_destroy(context) != ESO_STATUS_OK ||
        eso_context_destroy(NULL) != ESO_STATUS_OK) {
        return 7;
    }
    return 0;
}
