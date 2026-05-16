/*
** tests/test_lex_include_grammar.lex -- M3.5 buffer-stack test
** grammar.  Three single-byte tokens are enough for verifying
** emit order across nested LexInclude pushes.  No fancy
** patterns -- the work under test is the runtime, not the DFA.
*/
%name_prefix Tlxi.
rule a matches /a/ { /* */ }
rule b matches /b/ { /* */ }
rule c matches /c/ { /* */ }
