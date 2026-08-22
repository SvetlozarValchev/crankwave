#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
build_dir="${repository_dir}/.work/browser-workbench/build"
output_dir="${build_dir}/workbench"
responsive_fixture_dir="${repository_dir}/reference/fixtures/responsive-audio"
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
            -DCRANKWAVE_BUILD_TESTS=OFF \
            -DCRANKWAVE_BUILD_CLI=OFF \
            -DCRANKWAVE_BUILD_WASM=ON &&
        cmake \
            --build .work/browser-workbench/build \
            --target crankwave_web_workbench \
            --parallel 4
    '

responsive_fixture_count=0
for responsive_package_dir in "${responsive_fixture_dir}"/*; do
    if [[ ! -d "${responsive_package_dir}" ]]; then
        continue
    fi
    if [[ ! -f "${responsive_package_dir}/runtime.json" ]]; then
        printf '%s\n' \
            "responsive-audio fixture '${responsive_package_dir}' is incomplete: runtime.json is missing" \
            >&2
        exit 1
    fi
    responsive_package_leaf=$(basename -- "${responsive_package_dir}")
    cmake -E copy_directory \
        "${responsive_package_dir}" \
        "${output_dir}/packages/${responsive_package_leaf}"
    responsive_fixture_count=$((responsive_fixture_count + 1))
done
if ((responsive_fixture_count == 0)); then
    printf '%s\n' "no responsive-audio fixtures were found" >&2
    exit 1
fi

printf '%s\n' "${output_dir}"
