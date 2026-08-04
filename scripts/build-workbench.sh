#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
build_dir="${repository_dir}/.work/browser-workbench/build"
output_dir="${build_dir}/workbench"
emsdk_image="emscripten/emsdk@sha256:3a0d11e50f072dc2c4bc92e3b05ab1340fb7d4dd152f80b8af35fc1c6f15e644"

cmake -E remove_directory "${repository_dir}/.work/browser-workbench"
cmake -E make_directory "${repository_dir}/.work/browser-workbench"

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "${repository_dir}:/src" \
    -w /src \
    "${emsdk_image}" \
    sh -lc '
        emcmake cmake \
            -S . \
            -B .work/browser-workbench/build \
            -DCMAKE_BUILD_TYPE=Release \
            -DENGINE_SIM_OFFLINE_BUILD_TESTS=OFF \
            -DENGINE_SIM_OFFLINE_BUILD_CLI=OFF \
            -DENGINE_SIM_OFFLINE_BUILD_WASM=ON &&
        cmake \
            --build .work/browser-workbench/build \
            --target engine_sim_offline_web_workbench \
            --parallel 4
    '

responsive_package_source="${repository_dir}/artifacts/listening/bmw-m52tub28-responsive-package-first-audition"
responsive_package_output="${output_dir}/packages/bmw-m52tub28-responsive"
if [[ -f "${responsive_package_source}/package.json" ]]; then
    cmake -E make_directory "${responsive_package_output}"
    cmake -E copy_directory \
        "${responsive_package_source}" \
        "${responsive_package_output}"
fi

printf '%s\n' "${output_dir}"
