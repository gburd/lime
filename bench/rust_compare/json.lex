%name_prefix Json.
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
