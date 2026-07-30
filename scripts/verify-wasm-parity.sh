#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
work_dir="${repository_dir}/.work/wasm-parity"
native_build_dir="${work_dir}/native"
wasm_build_dir="${work_dir}/wasm"
native_bundle="${work_dir}/native.bundle"
wasm_bundle="${work_dir}/wasm.bundle"
emsdk_image="emscripten/emsdk@sha256:3a0d11e50f072dc2c4bc92e3b05ab1340fb7d4dd152f80b8af35fc1c6f15e644"

engine_json="${repository_dir}/data/engines/bmw-m52b28/engine.json"
scenario_json="${repository_dir}/tests/wasm/bmw-m52b28-short-live-parity.json"
impulse_response="${repository_dir}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav"
accessory_configuration="${repository_dir}/data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json"
expectations="${repository_dir}/tests/wasm/parity_expectations.json"

cmake -E remove_directory "${work_dir}"
cmake -E make_directory "${work_dir}"

cmake \
    -S "${repository_dir}" \
    -B "${native_build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON \
    -DENGINE_SIM_OFFLINE_BUILD_CLI=OFF \
    -DENGINE_SIM_OFFLINE_BUILD_WASM=OFF
cmake \
    --build "${native_build_dir}" \
    --target engine_sim_offline_wasm_parity_native \
    --parallel 4

"${native_build_dir}/tests/engine_sim_offline_wasm_parity_native" \
    "${engine_json}" \
    "${scenario_json}" \
    smooth-39 \
    "${impulse_response}" \
    warm-stock-accessories \
    "${accessory_configuration}" \
    "${native_bundle}"

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "${repository_dir}:/src" \
    -w /src \
    "${emsdk_image}" \
    sh -lc '
        emcmake cmake \
            -S . \
            -B .work/wasm-parity/wasm \
            -DCMAKE_BUILD_TYPE=Release \
            -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON \
            -DENGINE_SIM_OFFLINE_BUILD_CLI=OFF \
            -DENGINE_SIM_OFFLINE_BUILD_WASM=ON &&
        cmake \
            --build .work/wasm-parity/wasm \
            --target \
                engine_sim_offline_wasm \
                engine_sim_offline_wasm_parity_module \
                engine_sim_offline_wasm_numeric_contract_tests \
            --parallel 4 &&
        ctest \
            --test-dir .work/wasm-parity/wasm \
            --output-on-failure \
            -R "^wasm.numeric_contract$"
    '

cmake -E copy_if_different \
    "${wasm_build_dir}/engine-sim-offline.js" \
    "${wasm_build_dir}/engine-sim-offline.mjs"
node "${repository_dir}/tests/wasm/smoke_public_module.mjs" \
    "${wasm_build_dir}/engine-sim-offline.mjs"

node "${repository_dir}/tests/wasm/compare_parity.mjs" \
    "${wasm_build_dir}/tests/engine-sim-offline-wasm-parity.mjs" \
    "${native_bundle}" \
    "${engine_json}" \
    "${scenario_json}" \
    smooth-39 \
    "${impulse_response}" \
    warm-stock-accessories \
    "${accessory_configuration}" \
    "${wasm_bundle}" \
    "${expectations}"
