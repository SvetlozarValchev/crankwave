include_guard(GLOBAL)

include(CheckCXXSourceCompiles)

function(crankwave_require_wasm32_numeric_contract)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
                "the browser runtime requires the Emscripten wasm32 toolchain")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
                "the browser runtime requires the wasm32 32-bit pointer ABI")
    endif()

    set(_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    string(APPEND CMAKE_REQUIRED_FLAGS
           " -std=c++20 -fno-fast-math -ffp-contract=off -fno-lto")
    check_cxx_source_compiles(
        [=[
            #include <cstddef>
            #include <limits>

            static_assert(sizeof(void *) == 4);
            static_assert(sizeof(long double) == 16);
            static_assert(std::numeric_limits<long double>::is_iec559);
            static_assert(std::numeric_limits<long double>::has_infinity);
            static_assert(std::numeric_limits<long double>::has_quiet_NaN);
            static_assert(std::numeric_limits<long double>::round_style ==
                          std::round_to_nearest);
            static_assert(std::numeric_limits<long double>::radix == 2);
            static_assert(std::numeric_limits<long double>::digits == 113);
            static_assert(std::numeric_limits<long double>::min_exponent == -16381);
            static_assert(std::numeric_limits<long double>::max_exponent == 16384);

            int main() {
                return 0;
            }
        ]=]
        CRANKWAVE_WASM32_BINARY128_ADMITTED
    )
    set(CMAKE_REQUIRED_FLAGS "${_saved_required_flags}")

    if(NOT CRANKWAVE_WASM32_BINARY128_ADMITTED)
        message(FATAL_ERROR
                "the wasm32 toolchain does not provide the required IEEE "
                "binary128 long-double contract")
    endif()
endfunction()

function(crankwave_configure_wasm_numeric_target _target)
    if(NOT TARGET "${_target}")
        message(FATAL_ERROR
                "wasm numeric policy requires an existing target '${_target}'")
    endif()

    crankwave_require_wasm32_numeric_contract()
    target_compile_options(
        "${_target}"
        PRIVATE
            -fno-fast-math
            -ffp-contract=off
            -fno-lto
    )
    target_link_options("${_target}" PRIVATE -fno-lto)
    set_property(TARGET "${_target}" PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
endfunction()
