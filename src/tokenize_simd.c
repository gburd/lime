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

__attribute__((target("avx2")))
CharClassVector classify_simd_avx2(const char *input, size_t offset) {
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
    __m256i is_alpha = _mm256_or_si256(is_upper,
                         _mm256_or_si256(is_lower, is_underscore));

    /*
    ** Digit: '0'(0x30) .. '9'(0x39)
    */
    __m256i ge_0 = _mm256_cmpgt_epi8(chunk, _mm256_set1_epi8('0' - 1));
    __m256i le_9 = _mm256_cmpgt_epi8(_mm256_set1_epi8('9' + 1), chunk);
    __m256i is_digit = _mm256_and_si256(ge_0, le_9);

    /*
    ** Whitespace: space(0x20), tab(0x09), newline(0x0A), carriage-return(0x0D)
    */
    __m256i eq_space   = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8(' '));
    __m256i eq_tab     = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\t'));
    __m256i eq_newline = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\n'));
    __m256i eq_cr      = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\r'));
    __m256i is_space_v = _mm256_or_si256(eq_space,
                           _mm256_or_si256(eq_tab,
                             _mm256_or_si256(eq_newline, eq_cr)));

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
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
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

    /* Load 16 bytes */
    uint8x16_t chunk = vld1q_u8(p);

    /*
    ** Alphabetic: uppercase || lowercase || underscore
    */
    uint8x16_t ge_A = vcgeq_u8(chunk, vdupq_n_u8('A'));
    uint8x16_t le_Z = vcleq_u8(chunk, vdupq_n_u8('Z'));
    uint8x16_t is_upper = vandq_u8(ge_A, le_Z);

    uint8x16_t ge_a = vcgeq_u8(chunk, vdupq_n_u8('a'));
    uint8x16_t le_z = vcleq_u8(chunk, vdupq_n_u8('z'));
    uint8x16_t is_lower = vandq_u8(ge_a, le_z);

    uint8x16_t is_underscore = vceqq_u8(chunk, vdupq_n_u8('_'));

    uint8x16_t is_alpha = vorrq_u8(is_upper, vorrq_u8(is_lower, is_underscore));

    /*
    ** Digit: '0' .. '9'
    */
    uint8x16_t ge_0 = vcgeq_u8(chunk, vdupq_n_u8('0'));
    uint8x16_t le_9 = vcleq_u8(chunk, vdupq_n_u8('9'));
    uint8x16_t is_digit = vandq_u8(ge_0, le_9);

    /*
    ** Whitespace: space, tab, newline, carriage-return
    */
    uint8x16_t eq_space   = vceqq_u8(chunk, vdupq_n_u8(' '));
    uint8x16_t eq_tab     = vceqq_u8(chunk, vdupq_n_u8('\t'));
    uint8x16_t eq_newline = vceqq_u8(chunk, vdupq_n_u8('\n'));
    uint8x16_t eq_cr      = vceqq_u8(chunk, vdupq_n_u8('\r'));
    uint8x16_t is_space   = vorrq_u8(eq_space,
                              vorrq_u8(eq_tab,
                                vorrq_u8(eq_newline, eq_cr)));

    /* High bytes: c >= 0x80 */
    uint8x16_t is_high = vcgeq_u8(chunk, vdupq_n_u8(0x80));

    /* Convert to bitmasks -- only lower 16 bits are meaningful */
    result.is_alpha_mask = (uint32_t)neon_movemask(is_alpha);
    result.is_digit_mask = (uint32_t)neon_movemask(is_digit);
    result.is_space_mask = (uint32_t)neon_movemask(is_space);
    result.is_high_byte_mask = (uint32_t)neon_movemask(is_high);

    return result;
}
#endif /* __ARM_NEON */

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

    return classify_scalar;
}
