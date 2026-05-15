/*
** tests/test_drain.h -- shared types for P0-NEW-8 test.
**
** Mirrors ecpg's preproc.y + pgc.c interaction shape: two
** "channels" both write to the same byte buffer.
**   Channel 1 (lex-time): driver appends a fixed marker between
**                         lex calls, simulating pgc.c's
**                         echo_text(yytext, yyleng) for whitespace.
**   Channel 2 (reduce-time): grammar action body appends the
**                            token text via cap->buf, simulating
**                            preproc.y's fprintf(base_yyout, ...).
** Source order semantics: token, then space, then token, ...
** With Lime's lazy reduce, channel 1 fires BEFORE channel 2 for
** the previous token, producing wrong order.  Parse_drain between
** Parse() calls fixes the timing.
*/
#ifndef TEST_DRAIN_H
#define TEST_DRAIN_H

#include <stddef.h>

struct drain_capture {
    char   buf[256];
    size_t buf_len;
};

void drain_emit(struct drain_capture *cap, const char *s, size_t n);

#endif
