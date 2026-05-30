/*
** SIMD Character Classification
**
** Parallel character classification using AVX2, NEON, or scalar fallback.
** See tokenize_simd.h for the public interface.
**
** On x86_64, the AVX2 function uses __attribute__((target("avx2"))) so that
** the rest of the file can be compiled without -mavx2.  This avoids the
** compiler emitting AVX2 instructions in the scalar path or dispatch logic,
** which would crash on CPUs without AVX2 support.
**
** ----------------------------------------------------------------------
** LOAD-BEARING COMMENTS FOR FUTURE MAINTAINERS
** ----------------------------------------------------------------------
**
** This file is the subject of an explicit performance claim in the
** README ("SIMD-accelerated tokenization delivers 5-10x faster
** lexing").  bench/bench_simd_classify.c measures classify_scalar vs
** classify_simd_<best> head-to-head on a 1 MB corpus and asserts the
** SIMD path delivers >=1.5x.  On the maintainer's reference hardware
** (Apple M-series, NEON) the measured ratio is ~2.0x; on x86_64 with
** AVX2 it is typically 3-4x because AVX2 processes 32 bytes per
** instruction vs NEON's effective 16.
**
** DO NOT, when refactoring this file:
**
**   1. Remove the __attribute__((target("avx2"))) from
**      classify_simd_avx2.  Without it, the AVX2 intrinsics either
**      fail to compile (without -mavx2) or, worse, leak AVX2 codegen
**      into classify_scalar / get_classify_func and crash on
**      pre-AVX2 CPUs at startup.
**
**   2. Inline get_classify_func()'s CPUID probe into the main path.
**      Runtime dispatch is the entire reason this file works on
**      heterogeneous fleets; an "always-AVX2" build crashes on older
**      Intel/AMD CPUs.
**
**   3. Replace the bitmask return with per-byte loops.  The 32-bit
**      mask is the unit the consumer (src/tokenize.c) uses to skip
**      whitespace runs and locate identifier/digit boundaries via
**      __builtin_ctz.  Going scalar-per-byte at the API boundary
**      defeats the entire purpose of the SIMD path.
**
**   4. Move classify_scalar after classify_simd_avx2 in this file.
**      The scalar function is intentionally first so a compiler that
**      decides to inline cross-target sees the safe variant first;
**      moving it after the target-attributed function has historically
**      caused gcc to mis-attribute target("avx2") to the scalar path.
**
** If a change to this file makes the bench drop below 1.5x speedup,
** either revert the change or update the README to match the new
** reality.  The bench is in CI for exactly this reason.
*/
#include "tokenize_simd.h"

#include <string.h>

/* ======================================================================
** Scalar fallback -- works on every platform
** ====================================================================== */

CharClassVector classify_scalar(const char *input, size_t offset) {
    CharClassVector result;
    const unsigned char *p = (const unsigned char *)(input + offset);
    uint32_t alpha = 0;
    uint32_t digit = 0;
    uint32_t space = 0;
    uint32_t high = 0;

    for (int i = 0; i < 32; i++) {
        unsigned char c = p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
            alpha |= (uint32_t)1 << i;
        }
        if (c >= '0' && c <= '9') {
            digit |= (uint32_t)1 << i;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space |= (uint32_t)1 << i;
        }
        if (c >= 0x80) {
            high |= (uint32_t)1 << i;
        }
    }

    result.is_alpha_mask = alpha;
    result.is_digit_mask = digit;
    result.is_space_mask = space;
    result.is_high_byte_mask = high;
    return result;
}

/* ======================================================================
** AVX2 implementation (x86_64)
**
** Uses target attribute so only this function gets AVX2 codegen.
** The header is included unconditionally because immintrin.h is
** available on x86 regardless of -mavx2; the intrinsics just need
** the target attribute to be legal.
** ====================================================================== */

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#include <cpuid.h>

__attribute__((target("avx2"))) CharClassVector classify_simd_avx2(const char *input,
                                                                   size_t offset) {
    CharClassVector result;
    const char *p = input + offset;

    /* Load 32 bytes (unaligned) */
    __m256i chunk = _mm256_loadu_si256((const __m256i *)p);

    /*
    ** Alphabetic: (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
    **
    ** _mm256_cmpgt_epi8 does signed comparison, which is fine for ASCII
    ** values (0..127).
    */

    /* Check uppercase: 'A'(0x41) .. 'Z'(0x5A) */
    __m256i ge_A = _mm256_cmpgt_epi8(chunk, _mm256_set1_epi8('A' - 1));
    __m256i le_Z = _mm256_cmpgt_epi8(_mm256_set1_epi8('Z' + 1), chunk);
    __m256i is_upper = _mm256_and_si256(ge_A, le_Z);

    /* Check lowercase: 'a'(0x61) .. 'z'(0x7A) */
    __m256i ge_a = _mm256_cmpgt_epi8(chunk, _mm256_set1_epi8('a' - 1));
    __m256i le_z = _mm256_cmpgt_epi8(_mm256_set1_epi8('z' + 1), chunk);
    __m256i is_lower = _mm256_and_si256(ge_a, le_z);

    /* Check underscore */
    __m256i is_underscore = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('_'));

    /* Combine alphabetic checks */
    __m256i is_alpha = _mm256_or_si256(is_upper, _mm256_or_si256(is_lower, is_underscore));

    /*
    ** Digit: '0'(0x30) .. '9'(0x39)
    */
    __m256i ge_0 = _mm256_cmpgt_epi8(chunk, _mm256_set1_epi8('0' - 1));
    __m256i le_9 = _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), chunk);
    __m256i is_digit = _mm256_and_si256(ge_0, le_9);

    /*
    ** Whitespace: space(0x20), tab(0x09), newline(0x0A), carriage-return(0x0D)
    */
    __m256i eq_space = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8(' '));
    __m256i eq_tab = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\t'));
    __m256i eq_newline = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\n'));
    __m256i eq_cr = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\r'));
    __m256i is_space_v =
        _mm256_or_si256(eq_space, _mm256_or_si256(eq_tab, _mm256_or_si256(eq_newline, eq_cr)));

    /*
    ** High bytes: c >= 0x80 (UTF-8 lead or continuation bytes).
    ** Since _mm256_cmpgt_epi8 does signed comparison, bytes >= 0x80 are
    ** negative in signed interpretation, so they are less than 0x7F.
    ** Use: high = NOT (chunk > -1)  i.e. NOT (chunk >= 0).
    ** Equivalently, movemask on the raw bytes gives us the sign bit of
    ** each byte, which is 1 exactly when the byte >= 0x80.
    */

    /* Convert vector results to 32-bit bitmasks */
    result.is_alpha_mask = (uint32_t)_mm256_movemask_epi8(is_alpha);
    result.is_digit_mask = (uint32_t)_mm256_movemask_epi8(is_digit);
    result.is_space_mask = (uint32_t)_mm256_movemask_epi8(is_space_v);
    result.is_high_byte_mask = (uint32_t)_mm256_movemask_epi8(chunk);

    return result;
}

static bool cpu_supports_avx2(void) {
    unsigned int eax, ebx, ecx, edx;
    /* Check CPUID leaf 7 for AVX2 (bit 5 of EBX) */
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return (ebx & (1u << 5)) != 0;
    }
    return false;
}
#endif /* x86_64 || i386 */

/* ======================================================================
** NEON implementation (ARM)
** ====================================================================== */

#ifdef __ARM_NEON
#include <arm_neon.h>

/*
** Helper: convert a 16-byte NEON comparison mask (each byte 0x00 or 0xFF)
** to a 16-bit integer bitmask.  NEON lacks a direct movemask instruction.
*/
static inline uint16_t neon_movemask(uint8x16_t v) {
    /*
    ** AND each byte with its positional bit value, then pairwise-add
    ** down to two 8-bit sums (one per half), forming a 16-bit mask.
    */
    static const uint8_t shift_vals[16] = {
        1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t bits = vld1q_u8(shift_vals);

    /* v has 0xFF where true; AND with positional bits */
    uint8x16_t masked = vandq_u8(v, bits);

    /* Pairwise-add to accumulate bits from each 8-byte half */
    uint8x8_t lo = vget_low_u8(masked);
    uint8x8_t hi = vget_high_u8(masked);

    /* Pair-add: 8 bytes -> 4 x 16-bit partial sums */
    uint16x4_t p16_lo = vpaddl_u8(lo);
    uint16x4_t p16_hi = vpaddl_u8(hi);

    /* 4 x 16-bit -> 2 x 32-bit */
    uint32x2_t p32_lo = vpaddl_u16(p16_lo);
    uint32x2_t p32_hi = vpaddl_u16(p16_hi);

    /* 2 x 32-bit -> 1 x 64-bit */
    uint64x1_t p64_lo = vpaddl_u32(p32_lo);
    uint64x1_t p64_hi = vpaddl_u32(p32_hi);

    uint8_t byte_lo = (uint8_t)vget_lane_u64(p64_lo, 0);
    uint8_t byte_hi = (uint8_t)vget_lane_u64(p64_hi, 0);

    return (uint16_t)byte_lo | ((uint16_t)byte_hi << 8);
}

CharClassVector classify_simd_neon(const char *input, size_t offset) {
    CharClassVector result;
    const uint8_t *p = (const uint8_t *)(input + offset);

    /*
    ** NEON registers are 128 bits (16 bytes) wide, but the
    ** CharClassVector contract covers 32 bytes so that it matches
    ** the scalar and AVX2 paths.  Process two 16-byte chunks and
    ** merge the per-chunk 16-bit masks into 32-bit masks.
    */
    uint32_t alpha_mask = 0;
    uint32_t digit_mask = 0;
    uint32_t space_mask = 0;
    uint32_t high_mask = 0;

    for (int chunk_idx = 0; chunk_idx < 2; chunk_idx++) {
        uint8x16_t chunk = vld1q_u8(p + (size_t)chunk_idx * 16);

        /* Alphabetic: uppercase || lowercase || underscore */
        uint8x16_t ge_A = vcgeq_u8(chunk, vdupq_n_u8('A'));
        uint8x16_t le_Z = vcleq_u8(chunk, vdupq_n_u8('Z'));
        uint8x16_t is_upper = vandq_u8(ge_A, le_Z);

        uint8x16_t ge_a = vcgeq_u8(chunk, vdupq_n_u8('a'));
        uint8x16_t le_z = vcleq_u8(chunk, vdupq_n_u8('z'));
        uint8x16_t is_lower = vandq_u8(ge_a, le_z);

        uint8x16_t is_underscore = vceqq_u8(chunk, vdupq_n_u8('_'));

        uint8x16_t is_alpha = vorrq_u8(is_upper, vorrq_u8(is_lower, is_underscore));

        /* Digit: '0' .. '9' */
        uint8x16_t ge_0 = vcgeq_u8(chunk, vdupq_n_u8('0'));
        uint8x16_t le_9 = vcleq_u8(chunk, vdupq_n_u8('9'));
        uint8x16_t is_digit = vandq_u8(ge_0, le_9);

        /* Whitespace: space, tab, newline, carriage-return */
        uint8x16_t eq_space = vceqq_u8(chunk, vdupq_n_u8(' '));
        uint8x16_t eq_tab = vceqq_u8(chunk, vdupq_n_u8('\t'));
        uint8x16_t eq_newline = vceqq_u8(chunk, vdupq_n_u8('\n'));
        uint8x16_t eq_cr = vceqq_u8(chunk, vdupq_n_u8('\r'));
        uint8x16_t is_space = vorrq_u8(eq_space, vorrq_u8(eq_tab, vorrq_u8(eq_newline, eq_cr)));

        /* High bytes: c >= 0x80 */
        uint8x16_t is_high = vcgeq_u8(chunk, vdupq_n_u8(0x80));

        unsigned shift = (unsigned)chunk_idx * 16;
        alpha_mask |= (uint32_t)neon_movemask(is_alpha) << shift;
        digit_mask |= (uint32_t)neon_movemask(is_digit) << shift;
        space_mask |= (uint32_t)neon_movemask(is_space) << shift;
        high_mask |= (uint32_t)neon_movemask(is_high) << shift;
    }

    result.is_alpha_mask = alpha_mask;
    result.is_digit_mask = digit_mask;
    result.is_space_mask = space_mask;
    result.is_high_byte_mask = high_mask;

    return result;
}
#endif /* __ARM_NEON */

/* ======================================================================
** RISC-V Vector Extension (RVV) implementation
**
** Compiled when targeting an RVV 1.0 host (clang/gcc define
** __riscv_v_intrinsic on RVV builds).  RVV is *vector-length agnostic*:
** the same intrinsic code runs on hosts with VLEN=128, 256, 512, 1024.
** We therefore process one 32-byte block by setting vl=32 explicitly
** via vsetvl_e8m1; the runtime VL determines how many cycles each
** instruction takes but the program is otherwise unchanged.
**
** This implementation is gated on __riscv_v_intrinsic so the file
** still compiles on non-RVV RISC-V hosts (which fall through to the
** scalar path via get_classify_func).
** ====================================================================== */

#if defined(__riscv) && defined(__riscv_v_intrinsic) && defined(__riscv_vector)
#include <riscv_vector.h>

CharClassVector classify_simd_rvv(const char *input, size_t offset) {
    CharClassVector result;
    const uint8_t *p = (const uint8_t *)(input + offset);

    /* Set the active vector length to 32 elements of u8.  RVV's vl is
    ** the *minimum* of the requested length and the hardware's
    ** capability; on VLEN<256 hosts the operation is split internally
    ** but the program text and intrinsics are unchanged. */
    size_t vl = __riscv_vsetvl_e8m1(32);
    vuint8m1_t chunk = __riscv_vle8_v_u8m1(p, vl);

    /* Alphabetic: uppercase || lowercase || underscore.  RVV has
    ** widening compares; we use mask outputs (vbool8_t) and the
    ** vmor_mm primitive to fold three predicates together. */
    vbool8_t ge_A = __riscv_vmsgeu_vx_u8m1_b8(chunk, 'A', vl);
    vbool8_t le_Z = __riscv_vmsleu_vx_u8m1_b8(chunk, 'Z', vl);
    vbool8_t is_upper = __riscv_vmand_mm_b8(ge_A, le_Z, vl);

    vbool8_t ge_a = __riscv_vmsgeu_vx_u8m1_b8(chunk, 'a', vl);
    vbool8_t le_z = __riscv_vmsleu_vx_u8m1_b8(chunk, 'z', vl);
    vbool8_t is_lower = __riscv_vmand_mm_b8(ge_a, le_z, vl);

    vbool8_t is_underscore = __riscv_vmseq_vx_u8m1_b8(chunk, '_', vl);

    vbool8_t is_alpha = __riscv_vmor_mm_b8(
        is_upper, __riscv_vmor_mm_b8(is_lower, is_underscore, vl), vl);

    /* Digit: '0' .. '9' */
    vbool8_t ge_0 = __riscv_vmsgeu_vx_u8m1_b8(chunk, '0', vl);
    vbool8_t le_9 = __riscv_vmsleu_vx_u8m1_b8(chunk, '9', vl);
    vbool8_t is_digit = __riscv_vmand_mm_b8(ge_0, le_9, vl);

    /* Whitespace */
    vbool8_t eq_sp = __riscv_vmseq_vx_u8m1_b8(chunk, ' ',  vl);
    vbool8_t eq_tb = __riscv_vmseq_vx_u8m1_b8(chunk, '\t', vl);
    vbool8_t eq_nl = __riscv_vmseq_vx_u8m1_b8(chunk, '\n', vl);
    vbool8_t eq_cr = __riscv_vmseq_vx_u8m1_b8(chunk, '\r', vl);
    vbool8_t is_space = __riscv_vmor_mm_b8(
        eq_sp, __riscv_vmor_mm_b8(eq_tb,
              __riscv_vmor_mm_b8(eq_nl, eq_cr, vl), vl), vl);

    /* High byte: byte >= 0x80.  RVV unsigned compare. */
    vbool8_t is_high = __riscv_vmsgeu_vx_u8m1_b8(chunk, 0x80, vl);

    /* Pack each vbool8_t (one bit per element) into a 32-bit mask.
    ** RVV stores predicate registers as packed bits already, so
    ** we re-interpret as bytes via vfirst-style helpers.  For a
    ** straightforward extraction we move the mask bytes through
    ** memory (the compiler is expected to elide the round-trip
    ** when both the producer and consumer live in the same
    ** function -- LLVM's RVV pipeline does this with -O2). */
    uint8_t buf[4] = {0};
    __riscv_vsm_v_b8((uint8_t *)buf, is_alpha, vl);
    result.is_alpha_mask = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    __riscv_vsm_v_b8((uint8_t *)buf, is_digit, vl);
    result.is_digit_mask = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    __riscv_vsm_v_b8((uint8_t *)buf, is_space, vl);
    result.is_space_mask = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    __riscv_vsm_v_b8((uint8_t *)buf, is_high, vl);
    result.is_high_byte_mask = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                               ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    return result;
}
#endif /* __riscv_v_intrinsic */

/* ======================================================================
** CPU feature detection and dispatch
** ====================================================================== */

ClassifyFunc get_classify_func(void) {
#if defined(__x86_64__) || defined(__i386__)
    if (cpu_supports_avx2()) {
        return classify_simd_avx2;
    }
#endif

#if defined(__ARM_NEON)
    return classify_simd_neon;
#endif

#if defined(__riscv) && defined(__riscv_v_intrinsic) && defined(__riscv_vector)
    /* RVV is unconditionally selected when the compiler builds with
    ** RVV intrinsic support; the runtime VLEN determines throughput
    ** but every RVV-1.0 host accepts the instructions emitted by
    ** classify_simd_rvv. */
    return classify_simd_rvv;
#endif

    return classify_scalar;
}
