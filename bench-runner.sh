#!/usr/bin/env bash
# bench-runner.sh -- run a benchmark N times with CPU pinning,
# capture per-run output, compute median/mean/stddev/min/max/p95.
#
# Usage:
#   ./bench-runner.sh <runs> <cpu_id> <label> <command> [args...]
#
# Per-run output goes to ./bench-runs/<label>-NN.txt.  A summary
# line is appended to ./bench-runs/<label>-summary.txt with the
# stats fields.
#
# Designed to be portable between FreeBSD (cpuset) and Linux (taskset).

set -u

if [ $# -lt 4 ]; then
    echo "usage: $0 <runs> <cpu_id> <label> <command> [args...]" >&2
    exit 1
fi

runs=$1; shift
cpu_id=$1; shift
label=$1; shift

mkdir -p bench-runs

# CPU pinning: prefer cpuset (FreeBSD) over taskset (Linux).
pin_cmd=""
if command -v cpuset >/dev/null 2>&1; then
    pin_cmd="cpuset -l ${cpu_id}"
elif command -v taskset >/dev/null 2>&1; then
    pin_cmd="taskset -c ${cpu_id}"
fi

echo "=== ${label} : ${runs} runs on CPU ${cpu_id} ==="
echo "    command: $*"
echo "    pinning: ${pin_cmd:-<none>}"

# Warm the page cache by running once and discarding.
${pin_cmd} "$@" > /dev/null 2>&1 || true

# Collect runs.
for i in $(seq 1 "${runs}"); do
    out_file="bench-runs/${label}-$(printf '%02d' "${i}").txt"
    ${pin_cmd} "$@" > "${out_file}" 2>&1
    echo -n "  run ${i}: "
    grep -E '^.*median|^.*mean|^speedup|=== Median' "${out_file}" \
        | head -1 | tr -s ' '
done
echo "    raw outputs: bench-runs/${label}-*.txt"
