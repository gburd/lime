/*
** tests/test_lex_extra_arg_grammar.lex -- P0-NEW-12 regression
** grammar.
**
** Exercises %lexer_extra_argument: the directive-declared parameter
** is threaded through LexFeedBytes() and is in scope inside every
** action body via standard C parameter binding.  No file-scope
** globals required, so the lexer remains thread-safe and per-instance
** isolated.
*/
%name_prefix Exa.

%lexer_extra_argument { struct test_extra *ext }

%include {
    /* Forward-declared in the generated header via the
    ** %lexer_extra_argument prototype; the .lex side defines
    ** the struct so action bodies can dereference it. */
    struct test_extra { int counter; };
}

rule word    matches /[a-z]+/  {
    ext->counter++;
    LEX_EMIT(EXA_RULE_WORD);
}
rule space   matches /[ \t]+/  { LEX_SKIP(); }
