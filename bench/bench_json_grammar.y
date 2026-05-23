/*
** bench_json_grammar.y -- Lime grammar for a JSON parser, paired
** one-to-one with bench_flex_bison_compare/bench_json.y.  Used by
** the comparison harness to measure parse throughput on a real-world
** workload (object/array nesting, multiple value types, key/value
** pairs).
**
** Token names match the Bison version so the harness can drive both
** parsers against the same JSON source string with parallel
** tokenizers.
*/

%name_prefix JsonParser
%token_prefix JSON_
%start_symbol json

%token LBRACE RBRACE LBRACKET RBRACKET COMMA COLON STRING NUMBER TRUE FALSE NULL.

json ::= value.

value ::= STRING.
value ::= NUMBER.
value ::= TRUE.
value ::= FALSE.
value ::= NULL.
value ::= object.
value ::= array.

object ::= LBRACE RBRACE.
object ::= LBRACE members RBRACE.

members ::= pair.
members ::= members COMMA pair.

pair ::= STRING COLON value.

array ::= LBRACKET RBRACKET.
array ::= LBRACKET elements RBRACKET.

elements ::= value.
elements ::= elements COMMA value.
