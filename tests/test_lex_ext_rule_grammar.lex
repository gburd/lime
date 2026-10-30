/*
** tests/test_lex_ext_rule_grammar.lex -- B5 fixture.  Compiled by
** lime -X at build time.  Base syntax only: identifiers (through the
** runtime keyword table), numbers, whitespace, operator runs (through
** the runtime operator trie), single punctuation.  %runtime_rules lets
** an extension add new lexical syntax at runtime (MOD_ADD_LEX_RULE).
*/
%name_prefix Xr.

%keyword_class ident.
%operator_class op.
%runtime_rules.

rule ws     matches /[ \t\r\n]+/              { LEX_SKIP(); }
rule ident  matches /[A-Za-z_][A-Za-z_0-9]*/  { }
rule number matches /[0-9]+/                 { }
rule op     matches /[\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]+/ { }
rule punct  matches /[,;(){}]/               { }
