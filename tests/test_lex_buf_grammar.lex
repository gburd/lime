/*
** tests/test_lex_buf_grammar.lex -- M3.7 (%literal_buffer
** runtime emission) and P0-NEW-9 (%include block emission)
** smoke grammar.
**
** Two literal buffers exercise both the multiplicative ("*2")
** and additive ("+4") growth policies plus an aggressively
** small initial capacity so even short inputs trip the grow
** path.  alloc/realloc/free are bound to the libc functions
** so the test has no PostgreSQL dependency.
**
** A %include block injects a static helper plus the libc
** headers the action bodies need (memcpy, strlen).  The
** "checksum" helper is called from str_close to prove the
** include block's symbols are visible to action bodies.
**
** Two exclusive states (STR for double-quoted runs, IDA for
** angle-bracket runs) accumulate into separate buffers so
** sub-test 6 (multi-buffer independence) can verify the two
** buffers don't interfere.
*/
%name_prefix Buf.

%include {
    /* P0-NEW-9: emitted verbatim immediately after #include
    ** <stddef.h> / <string.h> in the generated .c.  The static
    ** helper below is private to this translation unit and is
    ** referenced from the str_close action body. */
    #include <stdlib.h>
    #include <string.h>
    static int buf_checksum(const char *s, size_t n) {
        int sum = 0;
        for (size_t i = 0; i < n; i++) sum += (unsigned char)s[i];
        return sum;
    }
}

%literal_buffer scanstr {
    type      char
    initial   4         /* small -> growth fires on most inputs */
    grow      "*2"
    alloc     malloc
    realloc   realloc
    free      free
}.

%literal_buffer scanid {
    type      char
    initial   8
    grow      "+4"      /* additive policy, exercised independently */
    alloc     malloc
    realloc   realloc
    free      free
}.

%exclusive_state STR.
%exclusive_state IDA.

/* INITIAL: open a string, open an ident, eat whitespace, or
** auto-emit single lower-case letters as a passthrough probe. */
rule open_str matches /"/        {
    LEX_BUF_START(scanstr);
    LEX_TRANSITION(BUF_STATE_STR);
    LEX_SKIP();
}
rule open_id  matches /</        {
    LEX_BUF_START(scanid);
    LEX_TRANSITION(BUF_STATE_IDA);
    LEX_SKIP();
}
rule space    matches /[ \t]+/   { LEX_SKIP(); }
rule passthru matches /[a-z]/    { /* auto-emit */ }

/* STR: append every non-quote byte one char at a time.  On the
** closing quote, peek + len + take, run the checksum helper from
** the %include block over the buffer contents, emit the buffer
** as the str_close token text, then free. */
<STR> rule str_char  matches /[^"]/ {
    LEX_BUF_APPEND_CH(scanstr, matched[0]);
    LEX_SKIP();
}
<STR> rule str_close matches /"/ {
    size_t n = LEX_BUF_LEN(scanstr);
    int sum = buf_checksum(LEX_BUF_PEEK(scanstr), n);
    char *s = LEX_BUF_TAKE(scanstr);
    if (s) {
        if (emit) emit(user, BUF_RULE_STR_CLOSE, s, n);
        free(s);
    }
    /* sum is exposed to the test through err_msg's pointer field
    ** by no other channel; just discard.  The point is the call
    ** linked. */
    (void)sum;
    LEX_TRANSITION(BUF_STATE_INITIAL);
    LEX_SKIP();
}

/* IDA: append every non-`>` byte as a multi-byte run.  On `>`,
** take + emit + free.  Uses LEX_BUF_APPEND (not _CH) to test the
** memcpy path. */
<IDA> rule id_char   matches /[^>]+/ {
    LEX_BUF_APPEND(scanid, matched, matched_len);
    LEX_SKIP();
}
<IDA> rule id_close  matches />/ {
    size_t n = LEX_BUF_LEN(scanid);
    char *s = LEX_BUF_TAKE(scanid);
    if (s) {
        if (emit) emit(user, BUF_RULE_ID_CLOSE, s, n);
        free(s);
    }
    LEX_TRANSITION(BUF_STATE_INITIAL);
    LEX_SKIP();
}
