/*
** tests/test_lex_terminate_frame_grammar.lex -- P0-NEW-13
** regression grammar.
**
** Single rule that matches `[a-z]+` and immediately fires
** LEX_TERMINATE().  Driver loops LexFeedBytes() 200 times on
** the same input; without the P0-NEW-13 fix the bottom frame
** leaks per call and iteration 65 fails with the
** "include depth exceeded" check (FOO_LEX_MAX_INCLUDE_DEPTH=64).
*/
%name_prefix Tlt.

rule word matches /[a-z]+/ {
    LEX_EMIT(TLT_RULE_WORD);
    LEX_TERMINATE();
}
