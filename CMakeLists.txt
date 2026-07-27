cmake_minimum_required(VERSION 3.21)

project(
    engine_sim_offline
    DESCRIPTION "Offline-native engine simulation and audio rendering"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are unsupported; use cmake -S . -B build")
endif()

option(
    ENGINE_SIM_OFFLINE_BUILD_TESTS
    "Build the focused engine-sim-offline test targets"
    "${PROJECT_IS_TOP_LEVEL}"
)

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(engine_sim_offline_build_options INTERFACE)
add_library(
    engine_sim_offline::build_options
    ALIAS engine_sim_offline_build_options
)

target_compile_features(engine_sim_offline_build_options INTERFACE cxx_std_20)

target_compile_options(
    engine_sim_offline_build_options
    INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-ffp-contract=off>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/fp:strict;/Zc:__cplusplus>
)

add_library(
    engine_sim_offline_contract
    STATIC
        src/contract/capture.cpp
        src/contract/common.cpp
        src/contract/engine.cpp
        src/contract/parity_model.cpp
        src/contract/presentation.cpp
        src/contract/provenance.cpp
        src/contract/render_manifest.cpp
        src/contract/result.cpp
        src/contract/scenario.cpp
        src/contract/source_matrix.cpp
        src/contract/torque.cpp
)
add_library(
    engine_sim_offline::contract
    ALIAS engine_sim_offline_contract
)

target_include_directories(
    engine_sim_offline_contract
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
)

target_link_libraries(
    engine_sim_offline_contract
    PUBLIC engine_sim_offline::build_options
)

if(ENGINE_SIM_OFFLINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
