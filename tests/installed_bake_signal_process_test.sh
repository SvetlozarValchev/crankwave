#!/usr/bin/env bash

set -u

if [[ $# -ne 6 ]]; then
    exit 99
fi

cli=$1
engine=$2
output=$3
standard_out=$4
standard_error=$5
signal_name=$6

env \
    AWS_ACCESS_KEY_ID=must-not-be-consumed \
    AWS_SECRET_ACCESS_KEY=must-not-be-consumed \
    HTTP_PROXY=http://127.0.0.1:1 \
    HTTPS_PROXY=http://127.0.0.1:1 \
    NODE_OPTIONS=must-not-be-consumed \
    "$cli" bake-vehicleengine \
        --engine "$engine" \
        --output "$output" \
        --result-format json >"$standard_out" 2>"$standard_error" &
child=$!

# Let the native cooker enter finite capture. No child-runtime readiness marker is
# involved because the v2 production path does not spawn a renderer process.
sleep 0.1
if ! kill -"$signal_name" "$child"; then
    wait "$child"
    exit 90
fi
wait "$child"
exit $?
