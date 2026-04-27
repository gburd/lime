/*
** SIMD Character Classification
**
** Provides parallel character classification using SIMD instructions
** (AVX2 on x86_64, NEON on ARM) with a scalar fallback.
**
** Each classify function examines up to 32 characters (AVX2/scalar)
** or 16 characters (NEON) starting at input+offset and returns
** bitmasks indicating which characters match each class.
*/
#ifndef TOKENIZE_SIMD_H
#define TOKENIZE_SIMD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct CharClassVector {
    uint32_t is_alpha_mask;   /* Bitmask: bit i set if character i is alphabetic */
    uint32_t is_digit_mask;   /* Bitmask: bit i set if character i is a digit */
    uint32_t is_space_mask;   /* Bitmask: bit i set if character i is whitespace */
    uint32_t is_high_byte_mask; /* Bitmask: bit i set if byte i >= 0x80 (UTF-8 lead/continuation) */
} CharClassVector;

/* Function pointer type for classification.
** Classifies characters starting at input+offset.
** The caller must ensure at least 32 bytes (AVX2/scalar) or 16 bytes (NEON)
** are readable from input+offset.
*/
typedef CharClassVector (*ClassifyFunc)(const char *input, size_t offset);

/*
** Return the best available classification function for the current CPU.
** On x86_64 with AVX2 support, returns the AVX2 implementation.
** On ARM with NEON, returns the NEON implementation.
** Otherwise returns the scalar fallback.
*/
ClassifyFunc get_classify_func(void);

/*
** Scalar fallback -- always available on every platform.
** Classifies 32 characters starting at input+offset.
*/
CharClassVector classify_scalar(const char *input, size_t offset);

#if defined(__x86_64__) || defined(__i386__)
/*
** AVX2 implementation -- classifies 32 characters in parallel.
** Uses target("avx2") attribute; safe to call only after checking
** CPU support via get_classify_func() or cpu_supports_avx2().
*/
CharClassVector classify_simd_avx2(const char *input, size_t offset);
#endif

#ifdef __ARM_NEON
/*
** NEON implementation -- classifies 16 characters in parallel.
** Only the lower 16 bits of each mask field are meaningful.
*/
CharClassVector classify_simd_neon(const char *input, size_t offset);
#endif

#endif /* TOKENIZE_SIMD_H */
