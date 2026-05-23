/*
** bench_json.l -- Flex lexer paired with bench_json.y.  Tokenises
** a JSON document into JSON_STRING / JSON_NUMBER / JSON_TRUE /
** JSON_FALSE / JSON_NULL / structural punctuation.  String contents
** and numeric values are not validated beyond what JSON's grammar
** permits at the lexical level -- the benchmark exists to measure
** parser throughput, not to be a faithful JSON validator.
*/

%option noyywrap
%option prefix="json_"

%{
#include "bench_json.tab.h"
%}

%%

[ \t\r\n]+              { /* whitespace */ }

"{"                     { return JSON_LBRACE; }
"}"                     { return JSON_RBRACE; }
"["                     { return JSON_LBRACKET; }
"]"                     { return JSON_RBRACKET; }
","                     { return JSON_COMMA; }
":"                     { return JSON_COLON; }

"true"                  { return JSON_TRUE; }
"false"                 { return JSON_FALSE; }
"null"                  { return JSON_NULL; }

\"([^"\\]|\\.)*\"       { return JSON_STRING; }

-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?  { return JSON_NUMBER; }

.                       { /* skip unknown characters silently */ }

%%
