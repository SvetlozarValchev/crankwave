#!/usr/bin/env bash

set -u

if [[ $# -ne 7 ]]; then
    exit 99
fi

cli=$1
engine=$2
scenario=$3
output=$4
standard_out=$5
standard_error=$6
signal_name=$7

"$cli" render \
    --engine "$engine" \
    --scenario "$scenario" \
    --output-directory "$output" \
    --result-format json >"$standard_out" 2>"$standard_error" &
child=$!
sleep 0.001
if ! kill -"$signal_name" "$child"; then
    wait "$child"
    exit 90
fi
wait "$child"
exit $?
