#!/usr/bin/env bash
set -euo pipefail

repository_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "${repository_dir}"

release_identity=${CRANKWAVE_VERSION:?CRANKWAVE_VERSION is required}
build_root=${CRANKWAVE_BUILD_ROOT:?CRANKWAVE_BUILD_ROOT is required}
expected_revision=${GITHUB_SHA:-$(git rev-parse HEAD)}
parallel_jobs=${CRANKWAVE_JOBS:-4}

case "${release_identity}" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "invalid CRANKWAVE_VERSION: ${release_identity}" >&2; exit 64 ;;
esac
case "${build_root}" in
    .work/github-*) ;;
    *) echo "CRANKWAVE_BUILD_ROOT must stay beneath .work/github-*: ${build_root}" >&2; exit 64 ;;
esac
case "${parallel_jobs}" in
    ''|*[!0-9]*) echo "CRANKWAVE_JOBS must be a positive integer" >&2; exit 64 ;;
esac
if (( parallel_jobs < 1 || parallel_jobs > 16 )); then
    echo "CRANKWAVE_JOBS must be in [1,16]" >&2
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

node scripts/source-assets.mjs verify
node tools/ir-authoring-catalog/generate.mjs --check
node tools/ir-authoring-catalog/test.mjs

native_build="${build_root}/native"

cmake \
    -S . \
    -B "${native_build}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCRANKWAVE_BUILD_TESTS=ON \
    -DCRANKWAVE_BUILD_CLI=ON \
    -DCRANKWAVE_BUILD_WASM=OFF \
    -DCRANKWAVE_RELEASE_IDENTITY="${release_identity}"
cmake --build "${native_build}" --parallel "${parallel_jobs}"

cli_executable="${native_build}/crankwave"
max_required_version() {
    local namespace=$1
    readelf --version-info "${cli_executable}" |
        grep -o "${namespace}_[0-9.]*" |
        sort -Vu |
        tail -n 1
}
require_version_at_most() {
    local actual=$1
    local ceiling=$2
    local label=$3
    local highest
    highest=$(printf '%s\n' "${actual}" "${ceiling}" | sort -V | tail -n 1)
    if [[ -z "${actual}" || "${highest}" != "${ceiling}" ]]; then
        echo "${label} requirement ${actual:-absent} exceeds runtime ceiling ${ceiling}" >&2
        exit 70
    fi
}
max_glibcxx=$(max_required_version GLIBCXX)
max_glibc=$(max_required_version GLIBC)
require_version_at_most "${max_glibcxx}" "GLIBCXX_3.4.30" "libstdc++"
require_version_at_most "${max_glibc}" "GLIBC_2.36" "glibc"
printf 'native_abi_max_glibcxx=%s\n' "${max_glibcxx}"
printf 'native_abi_max_glibc=%s\n' "${max_glibc}"

ctest \
    --test-dir "${native_build}" \
    --output-on-failure \
    --parallel 1

cmake \
    --build "${native_build}" \
    --parallel "${parallel_jobs}" \
    --target crankwave_distribution

distribution_dir="${native_build}/distribution/Release"
archive="${distribution_dir}/crankwave-${release_identity}.tar"
sidecar="${archive}.sha256"
expected_archive_sha=$(tr -d '\n' < "${sidecar}")
actual_archive_sha=$(sha256sum "${archive}" | awk '{print $1}')
if [[ "${expected_archive_sha}" != "${actual_archive_sha}" ]]; then
    echo "distribution archive digest differs from sidecar" >&2
    exit 70
fi

tar -xf "${archive}" -C "${build_root}/extracted"
test -x \
    "${build_root}/extracted/crankwave-${release_identity}/bin/crankwave"

printf 'release_archive=%s\n' "${archive}"
printf 'release_archive_sha256=%s\n' "${actual_archive_sha}"
