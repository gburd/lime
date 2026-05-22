/*
** Internal include shim.  The public header lives at include/tokenize_simd.h
** so generated lexers and downstream consumers can pull in the SIMD
** character-classification API by writing #include <lime/tokenize_simd.h>
** (after `make install`) without depending on any private library
** symbols.
*/
#ifndef TOKENIZE_SIMD_INTERNAL_H
#define TOKENIZE_SIMD_INTERNAL_H
#include "../include/tokenize_simd.h"
#endif
