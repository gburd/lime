/*
** tests/test_lex_eof_none_grammar.lex -- M3.6 regression grammar
** with NO <<EOF>> rules.  Verifies that the generated runtime
** still returns LEX_OK on end-of-input when the spec declares
** zero EOF rules (the M3.5 contract preserved).  The codegen
** path under test is the `any_eof == 0` branch in lex_emit.c
** which omits both the per-state lookup table and the dispatch
** code in the auto-pop branch.
*/
%name_prefix Tlen.
rule z matches /z/ { /* auto-emit */ }
