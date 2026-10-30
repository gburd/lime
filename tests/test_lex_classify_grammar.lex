/*
** tests/test_lex_classify_grammar.lex -- B3 fixture: a SQL-ish scanner
** whose identifier and operator rules go through the runtime lexicon.
** The scanner knows NO keywords and NO multi-char operators; it emits
** IDENT / OP / punctuation / numbers.  At runtime the host installs
** lime_lexicon_classify over a snapshot lexicon, and the same compiled
** scanner then returns SELECT, CONNECT (added by an extension after
** this file was compiled), <=, ->> ...
*/
%name_prefix Cls.

%pattern op_chars /[\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]/.

%keyword_class ident.
%operator_class op.

rule ws     matches /[ \t\r\n]+/            { LEX_SKIP(); }
rule ident  matches /[A-Za-z_][A-Za-z_0-9]*/ { }
rule number matches /[0-9]+/               { }
rule op     matches /{op_chars}+/          { }
rule punct  matches /[,;()]/               { }
