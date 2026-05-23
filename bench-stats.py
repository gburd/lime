#!/usr/bin/env python3
"""bench-stats.py -- read line-oriented numeric measurements from the
benchmark output files we collected and emit median/mean/stddev/min/max/p95.

Each binary prints one or more "min=NN.NN ms mean=NN.NN ms" or
similar lines; we extract the relevant column per pattern.  The
patterns are codified per-benchmark below so we get the right
field for each.
"""

import glob
import math
import re
import statistics
import sys


def parse_arith(path):
    """Extract per-tool min and mean from a flex_bison_compare arith run."""
    out = {}
    text = open(path).read()
    for tool in ("lime", "bison", "lime+jit"):
        m = re.search(
            rf"^\s*{re.escape(tool)}\s+min=(\d+\.\d+)\s+ms\s+mean=(\d+\.\d+)",
            text, re.MULTILINE,
        )
        if m:
            out[tool + ".min"] = float(m.group(1))
            out[tool + ".mean"] = float(m.group(2))
    return out


def parse_json_arith(path):
    """JSON section of bench_flex_bison_compare."""
    out = {}
    text = open(path).read()
    for tool in ("lime_json", "bison_json", "lime_json+jit"):
        m = re.search(
            rf"^\s*{re.escape(tool)}\s+min=(\d+\.\d+)\s+ms\s+mean=(\d+\.\d+)",
            text, re.MULTILINE,
        )
        if m:
            out[tool + ".min"] = float(m.group(1))
            out[tool + ".mean"] = float(m.group(2))
    return out


def parse_simdjson(path):
    """bench_simdjson_compare: Median across N trials section."""
    out = {}
    text = open(path).read()
    rows = re.findall(
        r"^\s*(lime\+jit\s+\S+|simdjson\s+\S+)\s+(\d+\.\d+)\s+ms\s+(\d+\.\d+)\s+MB/s\s+(\d+)\s+docs/s",
        text, re.MULTILINE,
    )
    for label, ms, mbs, docs in rows:
        key = label.strip().replace(" ", ".").replace("+", "_")
        out[f"{key}.ms"] = float(ms)
        out[f"{key}.mbs"] = float(mbs)
        out[f"{key}.docs"] = int(docs)
    return out


def percentile(xs, p):
    if not xs:
        return float("nan")
    s = sorted(xs)
    k = (len(s) - 1) * (p / 100.0)
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return s[int(k)]
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def report(label, files, parser):
    samples = [parser(f) for f in files]
    keys = sorted({k for s in samples for k in s.keys()})
    if not keys:
        print(f"\n[{label}] no parseable output in {len(files)} files")
        return
    print(f"\n[{label}] across {len(samples)} runs")
    print(f"{'metric':<32}  {'min':>9} {'med':>9} {'mean':>9} {'p95':>9} {'max':>9} {'std':>8}")
    for k in keys:
        xs = [s[k] for s in samples if k in s]
        if not xs:
            continue
        std = statistics.pstdev(xs) if len(xs) > 1 else 0.0
        print(
            f"{k:<32}  "
            f"{min(xs):9.2f} "
            f"{statistics.median(xs):9.2f} "
            f"{statistics.mean(xs):9.2f} "
            f"{percentile(xs, 95):9.2f} "
            f"{max(xs):9.2f} "
            f"{std:8.2f}"
        )


def main():
    if len(sys.argv) < 2:
        print("usage: bench-stats.py <bench-runs-dir> [prefix]", file=sys.stderr)
        sys.exit(1)
    d = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) >= 3 else ""

    arith_files = sorted(glob.glob(f"{d}/{prefix}arith-*.txt"))
    json_arith_files = sorted(glob.glob(f"{d}/{prefix}jsonarith-*.txt"))
    simd_files = sorted(glob.glob(f"{d}/{prefix}simdjson-*.txt"))

    if arith_files:
        report(f"{prefix}arith (parser-only)", arith_files, parse_arith)
    if json_arith_files:
        report(f"{prefix}json (lex+parse)", json_arith_files, parse_json_arith)
    if simd_files:
        report(f"{prefix}simdjson", simd_files, parse_simdjson)


if __name__ == "__main__":
    main()
