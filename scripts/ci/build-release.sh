#!/usr/bin/env bash
set -euo pipefail

repository_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "${repository_dir}"

release_identity=${ESO_VERSION:?ESO_VERSION is required}
build_root=${ESO_BUILD_ROOT:?ESO_BUILD_ROOT is required}
expected_revision=${GITHUB_SHA:-$(git rev-parse HEAD)}
parallel_jobs=${ESO_JOBS:-4}

case "${release_identity}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "invalid ESO_VERSION: ${release_identity}" >&2; exit 64 ;;
esac
case "${build_root}" in
    .work/github-*) ;;
    *) echo "ESO_BUILD_ROOT must stay beneath .work/github-*: ${build_root}" >&2; exit 64 ;;
esac
case "${parallel_jobs}" in
    ''|*[!0-9]*) echo "ESO_JOBS must be a positive integer" >&2; exit 64 ;;
esac
if (( parallel_jobs < 1 || parallel_jobs > 16 )); then
    echo "ESO_JOBS must be in [1,16]" >&2
    exit 64
fi

actual_revision=$(git rev-parse HEAD)
if [[ "${actual_revision}" != "${expected_revision}" ]]; then
    echo "checked-out revision differs from GITHUB_SHA" >&2
    exit 65
fi
if [[ -n "$(git status --porcelain=v1 --untracked-files=all)" ]]; then
    echo "release checkout is not clean" >&2
    git status --short >&2
    exit 65
fi

git lfs fsck
node tools/ir-authoring-catalog/generate.mjs --check
node tools/ir-authoring-catalog/test.mjs
node --test tests/wasm/c-api-v9-layout.test.mjs

cmake -E remove_directory "${build_root}"
cmake -E make_directory \
    "${build_root}/home" \
    "${build_root}/tmp" \
    "${build_root}/extracted"

export HOME="${repository_dir}/${build_root}/home"
export TMPDIR="${repository_dir}/${build_root}/tmp"
export LANG=C
export LC_ALL=C
export TZ=UTC

wasm_build="${build_root}/wasm"
native_build="${build_root}/native"

emcmake cmake \
    -S . \
    -B "${wasm_build}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON \
    -DENGINE_SIM_OFFLINE_BUILD_CLI=OFF \
    -DENGINE_SIM_OFFLINE_BUILD_WASM=ON \
    -DENGINE_SIM_OFFLINE_RELEASE_IDENTITY="${release_identity}"
cmake \
    --build "${wasm_build}" \
    --parallel "${parallel_jobs}" \
    --target \
        engine_sim_offline_wasm \
        engine_sim_offline_wasm_parity_module \
        engine_sim_offline_wasm_numeric_contract_tests
ctest \
    --test-dir "${wasm_build}" \
    --output-on-failure \
    -R '^wasm.numeric_contract$'

cmake \
    -S . \
    -B "${native_build}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENGINE_SIM_OFFLINE_BUILD_TESTS=ON \
    -DENGINE_SIM_OFFLINE_BUILD_CLI=ON \
    -DENGINE_SIM_OFFLINE_BUILD_WASM=OFF \
    -DENGINE_SIM_OFFLINE_RELEASE_IDENTITY="${release_identity}" \
    -DENGINE_SIM_OFFLINE_INSTALL_WASM_DIRECTORY="${repository_dir}/${wasm_build}"
cmake --build "${native_build}" --parallel "${parallel_jobs}"
ctest \
    --test-dir "${native_build}" \
    --output-on-failure \
    --parallel 1

native_bundle="${build_root}/native.bundle"
wasm_bundle="${build_root}/wasm.bundle"
"${native_build}/tests/engine_sim_offline_wasm_parity_native" \
    data/engines/bmw-m52b28/engine.json \
    tests/wasm/bmw-m52b28-short-live-parity.json \
    smooth-39 \
    reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav \
    warm-stock-accessories \
    data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json \
    "${native_bundle}"
cmake -E copy_if_different \
    "${wasm_build}/engine-sim-offline.js" \
    "${wasm_build}/engine-sim-offline.mjs"
node tests/wasm/smoke_public_module.mjs \
    "${wasm_build}/engine-sim-offline.mjs"
node tests/wasm/compare_parity.mjs \
    "${wasm_build}/tests/engine-sim-offline-wasm-parity.mjs" \
    "${native_bundle}" \
    data/engines/bmw-m52b28/engine.json \
    tests/wasm/bmw-m52b28-short-live-parity.json \
    smooth-39 \
    reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav \
    warm-stock-accessories \
    data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json \
    "${wasm_bundle}" \
    tests/wasm/parity_expectations.json

cmake \
    --build "${native_build}" \
    --parallel "${parallel_jobs}" \
    --target engine_sim_offline_distribution

distribution_dir="${native_build}/distribution/Release"
archive="${distribution_dir}/engine-sim-offline-${release_identity}.tar"
sidecar="${archive}.sha256"
expected_archive_sha=$(tr -d '\n' < "${sidecar}")
actual_archive_sha=$(sha256sum "${archive}" | awk '{print $1}')
if [[ "${expected_archive_sha}" != "${actual_archive_sha}" ]]; then
    echo "distribution archive digest differs from sidecar" >&2
    exit 70
fi

tar -xf "${archive}" -C "${build_root}/extracted"
prefix="${build_root}/extracted/engine-sim-offline-${release_identity}"
node scripts/ci/verify-release.mjs \
    --archive "${archive}" \
    --sidecar "${sidecar}" \
    --prefix "${prefix}" \
    --release "${release_identity}" \
    --revision "${expected_revision}"

printf 'release_archive=%s\n' "${archive}"
printf 'release_archive_sha256=%s\n' "${actual_archive_sha}"
