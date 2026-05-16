/*
** tests/test_lex_actions_grammar.lex -- M3.4 action-body
** inlining smoke grammar.
**
** Exercises every M3.4 action primitive:
**   - empty body  -> auto-emit (M3.3 contract preserved)
**   - LEX_SKIP()  -> suppress emit (whitespace)
**   - LEX_EMIT()  -> emit a different rule code (keyword vs ident)
**   - LEX_TRANSITION() -> switch state
**   - LEX_TERMINATE() -> stop early, return LEX_OK
**   - LEX_ERROR_AT() -> stop with custom error, return LEX_ERROR
**
** SAW_BANG is exclusive so that, after `!`, the only rule that
** matches is the explicit <SAW_BANG>-qualified `reset` rule.
** This lets the test confirm LEX_TRANSITION actually changed the
** lexer state without other rules masking the difference.
*/
%name_prefix Tla.

%exclusive_state SAW_BANG.

rule plus    matches /\+/        { /* empty -> auto-emit PLUS */ }
rule ws      matches /[ \t]+/    { LEX_SKIP(); }
rule kw      matches /if/        { LEX_EMIT(TLA_RULE_IDENT); }
rule ident   matches /[a-z]+/    { /* empty -> auto-emit IDENT */ }
rule bang    matches /!/         { LEX_TRANSITION(TLA_STATE_SAW_BANG); }
<SAW_BANG> rule reset matches /\?/ { LEX_TRANSITION(TLA_STATE_INITIAL); }
rule stop    matches /;/         { LEX_TERMINATE(); }
rule err     matches /@/         { LEX_ERROR_AT("custom @ error"); }
