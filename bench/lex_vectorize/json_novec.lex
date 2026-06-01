/*
** bench/lex_vectorize/json_novec.lex -- JSON tokeniser, name_prefix
** Jnovec, used by the --lex-no-vectorize bench binary.
**
** Identical to json_vec.lex except for the %name_prefix.  See
** json_vec.lex for the rationale.
*/
%name_prefix Jnovec.
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
