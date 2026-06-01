/*
** bench/lex_vectorize/json_vec.lex -- JSON tokeniser, name_prefix
** Jvec, used by the --lex-vectorize (default) bench binary.
**
** The two .lex files (json_vec.lex / json_novec.lex) are byte-
** identical except for the %name_prefix.  Distinct prefixes are
** required because both generated output pairs land in the same
** meson output directory; sharing a prefix would mean both
** generators try to write json_lex.c / json_lex.h.
*/
%name_prefix Jvec.
rule lbrace   matches /\{/        { /* */ }
rule rbrace   matches /\}/        { /* */ }
rule lbracket matches /\[/        { /* */ }
rule rbracket matches /\]/        { /* */ }
rule colon    matches /:/         { /* */ }
rule comma    matches /,/         { /* */ }
rule true     matches /true/      { /* */ }
rule false    matches /false/     { /* */ }
rule null     matches /null/      { /* */ }
rule num      matches /-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?/ { /* */ }
rule str      matches /"([^"\\]|\\.)*"/ { /* */ }
rule ws       matches /[ \t\n\r]+/ { /* */ }
