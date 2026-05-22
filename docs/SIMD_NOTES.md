# SIMD techniques applicable to Lime

This document summarises what techniques from the SIMD literature
(in particular Daniel Lemire's
[`fastfloat/int_serialization_benchmark`](https://github.com/fastfloat/int_serialization_benchmark)
and the broader `simdjson` family) Lime already uses and which ones
remain on the table.

The goal is to keep `src/tokenize_simd.c` honest: every claim in the
README (or in the load-bearing comment block at the top of that file)
should be backed by an actual technique someone can review.

## What Lime currently does

| Technique | Where | Source |
|-----------|-------|--------|
| Per-byte parallel range comparisons | `classify_simd_avx2` (x86_64), `classify_simd_neon` (ARM), `classify_simd_rvv` (RISC-V) | Standard SIMD pattern; equivalent to `simdjson::find_structurals_avx2`. |
| Compress per-byte mask -> 32-bit bitmask | `_mm256_movemask_epi8` (AVX2), `neon_movemask` (NEON), `__riscv_vsm_v_b8` (RVV) | `simdjson` and `fastfloat` use exactly this packing. |
| Function-attributed AVX2 codegen with runtime CPUID dispatch | `__attribute__((target("avx2")))` + `cpu_supports_avx2()` | Standard practice, matches `fastfloat`'s `BMI2`-vs-fallback runtime selection. |
| Vector-length-agnostic RVV | `__riscv_vsetvl_e8m1(32)` driving the same intrinsic stream | Aligns with the [RVV 1.0 specification](https://github.com/riscv/riscv-v-spec)'s recommended pattern. |

These cover the floor of what a SIMD character classifier should do:
parallel compare + bitmask compress + runtime dispatch.

## What Lime does NOT yet use, and could

### 1. Vectorised table lookup via `vpshufb` / `vqtbl1q_u8`

The `simdjson::structural_or_whitespace` lookup uses `vpshufb` (x86)
or `vqtbl1q_u8` (NEON) to do a per-byte table lookup that returns
the character's class as a small bitmask.  Then a single `vand` +
`vpcmpeq` can test "is this byte in class X?".

This is a slight win over our current per-byte range comparisons
because the lookup table can encode multi-class membership in a
single instruction.  For Lime's use case (alpha vs digit vs space
vs high-byte) the saving is on the order of 3-5 instructions per
32-byte block.  Worth pursuing if the bench shows the SIMD path
becoming a hot spot.

Source-tree placeholder: not implemented; would slot into a new
`classify_simd_avx2_v2` next to the existing function.

### 2. Wide AVX-512 (64-byte block) variant

`fastfloat` notes a measurable speedup from AVX-512 on Skylake-X
class hardware.  The catch is AVX-512 increases CPU frequency
throttling on older Xeon parts, so the runtime dispatcher needs to
consider both feature support *and* whether the host has good
AVX-512 throughput (`__cpuid` can read this).

Lower priority for Lime because most production CPUs Lime would
run on (cloud workers, Apple silicon, RISC-V edge) either don't
have AVX-512 or already have AVX2 and don't gain much from
expanding to 64-byte blocks.

### 3. Lookup-table acceleration of `find_first_not_in_set`

`simdjson` uses a 16-byte LUT plus `pshufb` to find the first byte
in a buffer that's *not* in some character class.  This pattern
maps onto Lime's identifier-scan path (skip alpha-and-underscore
runs) and would replace the current `__builtin_ctz` over the
classification mask with a single shuffle.

Where it would land: `src/tokenize.c::scan_identifier_simd` -- the
loop that consumes the bitmask Lime already produces.  This is a
real candidate but the numbers are small.

### 4. Pdep / Pext (BMI2) for unpacking action-table indices

Outside of tokenisation, the JIT codegen could use `_pdep_u64` /
`_pext_u64` to scatter/gather action-table entries when emitting
per-state action lookups.  This is what `fastfloat` uses for
fast integer-to-string (digit extraction).

This is purely a JIT-internal optimisation and only relevant once
the JIT-into-parse_token integration in
[ROADMAP.md](ROADMAP.md) §2 is done.

## Verification

`bench/bench_simd_classify` is the gate: any change to
`src/tokenize_simd.c` must keep the SIMD-vs-scalar speedup at >=1.5x
(on hardware that has SIMD).  CI runs the bench; if a refactor
regresses below the threshold the bench prints `[FAIL]` and the
commit needs review.
