#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
workbench_dir="${repository_dir}/.work/browser-workbench/build/workbench"
server_log="${repository_dir}/.work/browser-workbench/server.log"
chrome_executable="${ENGINE_SIM_OFFLINE_CHROME:-google-chrome}"

node --check "${repository_dir}/web/app.js"
node --check "${repository_dir}/web/audio-worklet.js"
node --check "${repository_dir}/web/engine-worker.js"
node --test "${repository_dir}"/web/tests/runtime/*.test.mjs

"${repository_dir}/scripts/build-workbench.sh"

node \
    "${repository_dir}/web/tests/integration/operating-bench.integration.mjs" \
    "${workbench_dir}/web/engine-sim-offline.js" \
    "${repository_dir}/data/engines/bmw-m52tub28-cleanroom/engine.json" \
    "${repository_dir}/data/engines/bmw-m52tub28-cleanroom/scenarios/held-dyno-pull-lift-1500-6500rpm.json" \
    "${repository_dir}/data/engines/bmw-m52tub28-cleanroom/scenarios/free-vehicle-launch-first-second.json" \
    smooth-39 \
    "${repository_dir}/reference/fixtures/engine-sim-ir-library/presentation/smooth_39.wav" \
    warm-generic-accessories \
    "${repository_dir}/data/profiles/bmw-m52tub28-cleanroom/accessory-configurations/bmw-m52tub28-cleanroom-warm-generic-accessories-v1.json"

node \
    "${repository_dir}/web/tests/integration/browser-runtime.integration.mjs" \
    "${workbench_dir}/web/engine-sim-offline.js" \
    "${repository_dir}/data/engines/bmw-m52b28/engine.json" \
    "${repository_dir}/tests/wasm/bmw-m52b28-short-live-parity.json" \
    smooth-39 \
    "${repository_dir}/reference/fixtures/bmw-m52b28-p18/presentation/smooth_39.wav" \
    warm-stock-accessories \
    "${repository_dir}/data/profiles/bmw-m52b28/accessory-configurations/bmw-m52b28-warm-stock-accessories-v1.json"

node \
    "${repository_dir}/scripts/serve-workbench.mjs" \
    --port 0 \
    >"${server_log}" 2>&1 &
server_pid=$!

cleanup() {
    if kill -0 "${server_pid}" 2>/dev/null; then
        kill "${server_pid}"
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

workbench_url=""
for _attempt in $(seq 1 100); do
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        wait "${server_pid}" 2>/dev/null || true
        sed -n '1,120p' "${server_log}" >&2
        printf '%s\n' "workbench server exited before becoming ready" >&2
        exit 1
    fi
    workbench_url=$(
        sed -n \
            's/^Engine Sim Offline workbench: \(http:\/\/127\.0\.0\.1:[0-9][0-9]*\/\)$/\1/p' \
            "${server_log}" |
            tail -n 1
    )
    if [[ -n "${workbench_url}" ]]; then
        break
    fi
    sleep 0.1
done
if [[ -z "${workbench_url}" ]]; then
    sed -n '1,120p' "${server_log}" >&2
    printf '%s\n' "workbench server did not report its listening URL" >&2
    exit 1
fi

server_headers=""
for _attempt in $(seq 1 100); do
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        wait "${server_pid}" 2>/dev/null || true
        sed -n '1,120p' "${server_log}" >&2
        printf '%s\n' "workbench server exited during readiness checks" >&2
        exit 1
    fi
    if server_headers=$(curl --fail --silent --show-error --head \
        "${workbench_url}" 2>/dev/null); then
        break
    fi
    sleep 0.1
done
if [[ -z "${server_headers}" ]]; then
    sed -n '1,120p' "${server_log}" >&2
    printf '%s\n' "workbench server did not become ready" >&2
    exit 1
fi
if ! printf '%s\n' "${server_headers}" |
    rg --ignore-case --quiet '^cross-origin-opener-policy: same-origin\r?$'; then
    printf '%s\n' "workbench response omitted the required COOP header" >&2
    exit 1
fi
if ! printf '%s\n' "${server_headers}" |
    rg --ignore-case --quiet \
        '^cross-origin-embedder-policy: require-corp\r?$'; then
    printf '%s\n' "workbench response omitted the required COEP header" >&2
    exit 1
fi

node \
    "${repository_dir}/web/tests/integration/workbench-ui-smoke.mjs" \
    "${workbench_url}" \
    "${chrome_executable}"
