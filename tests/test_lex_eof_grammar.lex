/*
** tests/test_lex_eof_grammar.lex -- M3.6 <<EOF>> rule dispatch
** smoke grammar.
**
** Two states, four regular rules, two EOF rules:
**   - INITIAL  : 'a' / 'x' / '!' (-> XQ)
**   - XQ       : 'y' / '?' (-> INITIAL)
**   - <<EOF>>           in INITIAL : LEX_EMIT(EOF_INIT)
**   - <XQ><<EOF>>                 : LEX_ERROR_AT(...)
**
** XQ is exclusive so the unqualified <<EOF>> rule does NOT
** apply there -- the <XQ>-qualified one wins.  The split lets
** the runtime test verify per-state dispatch.
**
** 'x' is an INITIAL-state rule (not in XQ) so test_eof_with_include
** can match an "x" included from inside the emit of 'a' without
** needing to flip state first.
*/
%name_prefix Tle.

%exclusive_state XQ.

rule a   matches /a/   { /* auto-emit A */ }
rule x   matches /x/   { /* auto-emit X */ }
rule b   matches /!/   { LEX_TRANSITION(TLE_STATE_XQ); }
<XQ> rule y   matches /y/   { /* auto-emit Y */ }
<XQ> rule e   matches /\?/  { LEX_TRANSITION(TLE_STATE_INITIAL); }

rule eof_init    matches <<EOF>> { LEX_EMIT(TLE_RULE_EOF_INIT); }
<XQ> rule eof_xq matches <<EOF>> { LEX_ERROR_AT("unterminated string"); }
