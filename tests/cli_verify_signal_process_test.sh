#!/usr/bin/env bash

set -u

if [[ $# -ne 5 ]]; then
    exit 99
fi

cli=$1
input=$2
standard_out=$3
standard_error=$4
signal_name=$5

"$cli" verify-revengine \
    --input "$input" \
    --result-format json >"$standard_out" 2>"$standard_error" &
child=$!
sleep 0.005
if ! kill -"$signal_name" "$child"; then
    wait "$child"
    exit 90
fi
wait "$child"
exit $?
