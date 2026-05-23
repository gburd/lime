/*
** bench_json.y -- Bison grammar for a JSON parser, paired one-to-one
** with bench_json_grammar.y (Lime).  Used by the bench_flex_bison_compare
** harness to measure parse throughput on a real-world workload that
** exercises non-trivial state transitions: object/array nesting,
** multiple value types, key/value pairs.
**
** Token IDs map by name to the Lime grammar; the comparison harness
** does not require numeric equality.
*/

%{
#include <stdio.h>
#include <stdlib.h>
int json_lex(void);
void json_error(const char *s);
extern int g_json_count;
%}

%define api.prefix {json_}

%token JSON_LBRACE JSON_RBRACE JSON_LBRACKET JSON_RBRACKET
%token JSON_COMMA JSON_COLON
%token JSON_STRING JSON_NUMBER
%token JSON_TRUE JSON_FALSE JSON_NULL

%%

json
    : value                             { g_json_count++; }
    ;

value
    : JSON_STRING
    | JSON_NUMBER
    | JSON_TRUE
    | JSON_FALSE
    | JSON_NULL
    | object
    | array
    ;

object
    : JSON_LBRACE JSON_RBRACE
    | JSON_LBRACE members JSON_RBRACE
    ;

members
    : pair
    | members JSON_COMMA pair
    ;

pair
    : JSON_STRING JSON_COLON value
    ;

array
    : JSON_LBRACKET JSON_RBRACKET
    | JSON_LBRACKET elements JSON_RBRACKET
    ;

elements
    : value
    | elements JSON_COMMA value
    ;

%%

int g_json_count;

void json_error(const char *s) {
    (void)s;
}
