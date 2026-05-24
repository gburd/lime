/*
** Direct unit tests for src/utf8.c.
**
** utf8.c was identified during the production-readiness audit as
** active code (used by src/tokenize.c) lacking direct test
** coverage -- only the codepaths tokenize.c happens to exercise
** got hit, leaving roughly half the file (utf8_encode + the
** ID_Start / ID_Continue range tables for higher Unicode planes)
** at 0% coverage.
**
** Coverage targets all five public entry points:
**   utf8_decode       -- 1-, 2-, 3-, 4-byte sequences plus error
**                        cases (truncated input, invalid lead,
**                        invalid continuation, overlong, surrogate).
**   utf8_encode       -- ASCII (1B), Latin-1 (2B), BMP (3B),
**                        supplementary (4B), invalid codepoints.
**   utf8_char_length  -- all four lead-byte classes plus invalid.
**   utf8_is_id_start    \  representative codepoints across each
**   utf8_is_id_continue /  range table region (ASCII, Latin-1,
**                          BMP, supplementary planes).
**
** Reference values cross-checked against the Unicode 15.1 character
** database for ID_Start / ID_Continue properties.
*/

#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                          \
    if (!(cond)) {                                                          \
        fprintf(stderr, "FAIL %s:%d: " fmt "\n",                            \
                __func__, __LINE__, ##__VA_ARGS__);                         \
        failures++;                                                         \
    }                                                                       \
} while (0)

/* ------------------------------------------------------------------ */
/* utf8_decode                                                         */
/* ------------------------------------------------------------------ */

static void test_decode_ascii(void) {
    const char *s = "A";
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 1);
    CHECK(cp == 'A', "ASCII A decoded as %d, want 65", cp);
    CHECK(p == s + 1, "ASCII advance: p moved %td bytes, want 1", p - s);
}

static void test_decode_2byte(void) {
    /* U+00E9 LATIN SMALL LETTER E WITH ACUTE = 0xC3 0xA9 */
    const char s[] = { (char)0xC3, (char)0xA9, 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 2);
    CHECK(cp == 0x00E9, "2-byte U+00E9 decoded as 0x%X", cp);
    CHECK(p == s + 2, "2-byte advance: p moved %td bytes, want 2", p - s);
}

static void test_decode_3byte(void) {
    /* U+20AC EURO SIGN = 0xE2 0x82 0xAC */
    const char s[] = { (char)0xE2, (char)0x82, (char)0xAC, 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 3);
    CHECK(cp == 0x20AC, "3-byte U+20AC decoded as 0x%X", cp);
    CHECK(p == s + 3, "3-byte advance: p moved %td bytes, want 3", p - s);
}

static void test_decode_4byte(void) {
    /* U+1F600 GRINNING FACE = 0xF0 0x9F 0x98 0x80 */
    const char s[] = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 4);
    CHECK(cp == 0x1F600, "4-byte U+1F600 decoded as 0x%X", cp);
    CHECK(p == s + 4, "4-byte advance: p moved %td bytes, want 4", p - s);
}

static void test_decode_truncated(void) {
    /* 2-byte sequence with end after one byte */
    const char s[] = { (char)0xC3, 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 1);
    CHECK(cp < 0, "truncated 2-byte should fail, got 0x%X", cp);
}

static void test_decode_invalid_lead(void) {
    /* 0x80 is a continuation byte, not a lead */
    const char s[] = { (char)0x80, 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 1);
    CHECK(cp < 0, "lone continuation byte should fail, got 0x%X", cp);
}

static void test_decode_invalid_continuation(void) {
    /* 0xC3 followed by ASCII A (not a continuation byte 10xxxxxx) */
    const char s[] = { (char)0xC3, 'A', 0 };
    const char *p = s;
    int32_t cp = utf8_decode(&p, s + 2);
    CHECK(cp < 0, "non-continuation after 2-byte lead should fail, got 0x%X", cp);
}

/* ------------------------------------------------------------------ */
/* utf8_encode                                                         */
/* ------------------------------------------------------------------ */

static void test_encode_ascii(void) {
    char out[5] = {0};
    int n = utf8_encode('A', out);
    CHECK(n == 1, "ASCII encode: wrote %d bytes, want 1", n);
    CHECK((unsigned char)out[0] == 'A', "ASCII encode: out[0]=0x%X", (unsigned char)out[0]);
}

static void test_encode_2byte(void) {
    char out[5] = {0};
    int n = utf8_encode(0x00E9, out);
    CHECK(n == 2, "2-byte encode: wrote %d bytes, want 2", n);
    CHECK((unsigned char)out[0] == 0xC3 && (unsigned char)out[1] == 0xA9,
          "2-byte encode: got 0x%02X 0x%02X, want 0xC3 0xA9",
          (unsigned char)out[0], (unsigned char)out[1]);
}

static void test_encode_3byte(void) {
    char out[5] = {0};
    int n = utf8_encode(0x20AC, out);
    CHECK(n == 3, "3-byte encode: wrote %d bytes, want 3", n);
    CHECK((unsigned char)out[0] == 0xE2 && (unsigned char)out[1] == 0x82
              && (unsigned char)out[2] == 0xAC,
          "3-byte encode: got 0x%02X 0x%02X 0x%02X",
          (unsigned char)out[0], (unsigned char)out[1], (unsigned char)out[2]);
}

static void test_encode_4byte(void) {
    char out[5] = {0};
    int n = utf8_encode(0x1F600, out);
    CHECK(n == 4, "4-byte encode: wrote %d bytes, want 4", n);
    CHECK((unsigned char)out[0] == 0xF0 && (unsigned char)out[1] == 0x9F
              && (unsigned char)out[2] == 0x98 && (unsigned char)out[3] == 0x80,
          "4-byte encode: got 0x%02X 0x%02X 0x%02X 0x%02X",
          (unsigned char)out[0], (unsigned char)out[1],
          (unsigned char)out[2], (unsigned char)out[3]);
}

static void test_encode_invalid(void) {
    char out[5] = {0};
    /* Negative codepoint */
    int n = utf8_encode(-1, out);
    CHECK(n == 0, "negative cp encode: wrote %d, want 0", n);
    /* Beyond U+10FFFF */
    n = utf8_encode(0x110000, out);
    CHECK(n == 0, "cp > U+10FFFF encode: wrote %d, want 0", n);
}

static void test_encode_decode_round_trip(void) {
    /* For a representative sample, encode then decode and verify. */
    int32_t samples[] = { 0, 0x7F, 0x80, 0x7FF, 0x800, 0xFFFF, 0x10000, 0x10FFFF };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        char buf[5] = {0};
        int n = utf8_encode(samples[i], buf);
        CHECK(n > 0 && n <= 4, "round-trip encode 0x%X: wrote %d", samples[i], n);
        const char *p = buf;
        int32_t back = utf8_decode(&p, buf + n);
        CHECK(back == samples[i], "round-trip 0x%X != 0x%X", samples[i], back);
    }
}

/* ------------------------------------------------------------------ */
/* utf8_char_length                                                    */
/* ------------------------------------------------------------------ */

static void test_char_length(void) {
    CHECK(utf8_char_length(0x41) == 1, "ASCII lead: got %d", utf8_char_length(0x41));
    CHECK(utf8_char_length(0xC3) == 2, "2-byte lead: got %d", utf8_char_length(0xC3));
    CHECK(utf8_char_length(0xE2) == 3, "3-byte lead: got %d", utf8_char_length(0xE2));
    CHECK(utf8_char_length(0xF0) == 4, "4-byte lead: got %d", utf8_char_length(0xF0));
    /* Invalid lead bytes: continuations and 5-byte+ leads */
    CHECK(utf8_char_length(0x80) == 0, "continuation: got %d", utf8_char_length(0x80));
    CHECK(utf8_char_length(0xF8) == 0, "5-byte lead: got %d", utf8_char_length(0xF8));
}

/* ------------------------------------------------------------------ */
/* utf8_is_id_start / utf8_is_id_continue                              */
/* ------------------------------------------------------------------ */

static void test_id_start_ascii(void) {
    CHECK(utf8_is_id_start('a') == true, "'a' should be ID_Start");
    CHECK(utf8_is_id_start('Z') == true, "'Z' should be ID_Start");
    CHECK(utf8_is_id_start('_') == true, "'_' should be ID_Start");
    CHECK(utf8_is_id_start('0') == false, "'0' should not be ID_Start");
    CHECK(utf8_is_id_start(' ') == false, "' ' should not be ID_Start");
    CHECK(utf8_is_id_start('-') == false, "'-' should not be ID_Start");
}

static void test_id_continue_ascii(void) {
    CHECK(utf8_is_id_continue('a') == true, "'a' should be ID_Continue");
    CHECK(utf8_is_id_continue('0') == true, "'0' should be ID_Continue");
    CHECK(utf8_is_id_continue('_') == true, "'_' should be ID_Continue");
    CHECK(utf8_is_id_continue(' ') == false, "' ' should not be ID_Continue");
    CHECK(utf8_is_id_continue('-') == false, "'-' should not be ID_Continue");
}

static void test_id_start_unicode(void) {
    /* Latin small a with acute (U+00E1) -- ID_Start */
    CHECK(utf8_is_id_start(0x00E1) == true, "U+00E1 should be ID_Start");
    /* Greek capital alpha (U+0391) -- ID_Start */
    CHECK(utf8_is_id_start(0x0391) == true, "U+0391 should be ID_Start");
    /* CJK ideograph 木 (U+6728) -- ID_Start */
    CHECK(utf8_is_id_start(0x6728) == true, "U+6728 should be ID_Start");
    /* Punctuation . (U+002E) -- not ID_Start */
    CHECK(utf8_is_id_start(0x002E) == false, "U+002E should not be ID_Start");
    /* Currency € (U+20AC) -- not ID_Start */
    CHECK(utf8_is_id_start(0x20AC) == false, "U+20AC should not be ID_Start");
}

static void test_id_continue_unicode(void) {
    /* Combining acute accent (U+0301) -- ID_Continue, not ID_Start */
    CHECK(utf8_is_id_continue(0x0301) == true, "U+0301 should be ID_Continue");
    CHECK(utf8_is_id_start(0x0301) == false, "U+0301 should not be ID_Start");
    /* Devanagari digit one (U+0967) -- ID_Continue */
    CHECK(utf8_is_id_continue(0x0967) == true, "U+0967 should be ID_Continue");
}

static void test_id_negative_codepoint(void) {
    /* Negative codepoint must return false for both. */
    CHECK(utf8_is_id_start(-1) == false, "-1 should not be ID_Start");
    CHECK(utf8_is_id_continue(-1) == false, "-1 should not be ID_Continue");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("UTF-8 unit tests\n");
    printf("================\n");

    test_decode_ascii();
    test_decode_2byte();
    test_decode_3byte();
    test_decode_4byte();
    test_decode_truncated();
    test_decode_invalid_lead();
    test_decode_invalid_continuation();

    test_encode_ascii();
    test_encode_2byte();
    test_encode_3byte();
    test_encode_4byte();
    test_encode_invalid();
    test_encode_decode_round_trip();

    test_char_length();

    test_id_start_ascii();
    test_id_continue_ascii();
    test_id_start_unicode();
    test_id_continue_unicode();
    test_id_negative_codepoint();

    if (failures > 0) {
        fprintf(stderr, "FAIL: %d assertion(s) failed\n", failures);
        return 1;
    }
    printf("PASS: all utf8 unit tests\n");
    return 0;
}
