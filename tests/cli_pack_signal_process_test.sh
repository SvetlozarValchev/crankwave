#!/usr/bin/env bash

set -u

if [[ $# -ne 7 ]]; then
    exit 99
fi

cli=$1
package_directory=$2
output=$3
standard_out=$4
standard_error=$5
output_parent=$6
signal_name=$7

"$cli" pack-vehicleengine \
    --package-directory "$package_directory" \
    --output "$output" \
    --result-format json >"$standard_out" 2>"$standard_error" &
child=$!

stage_observed=false
for ((attempt = 0; attempt < 30000; ++attempt)); do
    if compgen -G "$output_parent/.engine-sim-offline-stage-*" >/dev/null; then
        stage_observed=true
        break
    fi
    if ! kill -0 "$child" 2>/dev/null; then
        wait "$child"
        exit 90
    fi
    sleep 0.0001
done

if [[ "$stage_observed" != true ]]; then
    kill -TERM "$child" 2>/dev/null || true
    wait "$child"
    exit 91
fi
if ! kill -"$signal_name" "$child"; then
    wait "$child"
    exit 92
fi
wait "$child"
exit $?
