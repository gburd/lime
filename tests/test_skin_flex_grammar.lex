/*
** tests/test_skin_flex_grammar.lex -- v0.9.3 flex-skin smoke
** grammar.  Five token kinds drive an end-to-end pipeline:
**
**   lime -X --target=c:flex test_skin_flex_grammar.lex
**       -> standard test_skin_flex_grammar_lex.{c,h}
**       -> flex skin test_skin_flex_grammar_flex.{c,h}
**
** The driver compiles all four files together and asserts the
** flex skin's yylex() emits the same rule sequence as Lime's
** native push-driven LexFeedBytes() runtime over the same input.
**
** Each rule's action body is intentionally empty -- the flex
** skin does not honour LEX_SKIP / LEX_EMIT / LEX_TRANSITION
** (documented limitation; see docs/SKINS.md), so the auto-emit
** fallback is what both APIs see.  This keeps the round-trip
** comparison meaningful.
*/
%name_prefix Sf.
rule plus  matches /\+/      { /* */ }
rule num   matches /[0-9]+/  { /* */ }
rule ident matches /[a-z]+/  { /* */ }
rule ws    matches /[ \t]+/  { /* */ }
rule nl    matches /\n/      { /* */ }
