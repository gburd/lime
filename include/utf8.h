#ifndef LIME_UTF8_H
#define LIME_UTF8_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode one UTF-8 codepoint from *p. Advances *p past the decoded character.
** Returns the codepoint, or -1 on invalid sequence. Stops at end. */
int32_t utf8_decode(const char **p, const char *end);

/* Encode a codepoint to UTF-8. Writes 1-4 bytes to out.
** Returns the number of bytes written, or 0 if cp is invalid. */
int utf8_encode(int32_t cp, char *out);

/* Return the expected byte length of a UTF-8 character given its first byte.
** Returns 0 for invalid lead bytes. */
int utf8_char_length(unsigned char first_byte);

/* Unicode character properties per UAX#31 (Unicode Identifier Syntax).
** ID_Start: Letters, Nl, Other_ID_Start, minus Pattern_Syntax/White_Space
** ID_Continue: ID_Start + Mn, Mc, Nd, Pc, Other_ID_Continue */
bool utf8_is_id_start(int32_t cp);
bool utf8_is_id_continue(int32_t cp);

#ifdef __cplusplus
}
#endif

#endif /* LIME_UTF8_H */
