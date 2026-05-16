/*
** tests/test_lex_runtime_grammar.lex -- M3.3 push-API smoke
** test grammar.  Four simple token kinds for verifying the
** generated push-driven runtime (LexAlloc/Free/FeedBytes/
** FeedEOF) feeds a byte stream and emits per-rule callbacks.
*/
%name_prefix Tlx.
rule plus  matches /\+/      { /* M3.5 will inject this body */ }
rule num   matches /[0-9]+/  { /* */ }
rule ident matches /[a-z]+/  { /* */ }
rule ws    matches /[ \t]+/  { /* */ }
