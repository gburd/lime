#!/bin/sh
# tests/test_skin_bison_charcodes.sh -- --target=c:bison passes char codes
# through: a Lime grammar whose tokens are named after characters
# (CHAR_<code>, the spelling tests/compat/y2lime_min.py uses) gets a
# yyparse() that accepts yylex() returning '+' (code 43).  Before, the
# skin's yy_xlat_token rejected every code < 258 as "invalid token".
#
# usage: test_skin_bison_charcodes.sh <lime-binary> <limpar.c>
set -u
LIME=$1; TPL=$2
W=$(mktemp -d)
trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
CC=${CC:-cc}
TO=""; command -v timeout >/dev/null 2>&1 && TO="timeout 10"
fails=0
cat > "$W/g.lime" <<'EOF'
%name G
%token_type {int}
%type e {int}
%extra_argument {int *res}
%left CHAR_43.
%syntax_error { yyerror("syntax error"); }
%include { void yyerror(const char *); }
top ::= e(A). { *res = A; }
e(A) ::= e(B) CHAR_43 e(C). { A = B + C; }
e(A) ::= NUM(B). { A = B; }
EOF
cat > "$W/main.c" <<'EOF'
#include <stdio.h>
#include "g_bison.h"
static const char *in; static int k;
int yylex(void){ char c = in[k++]; if( c>='0' && c<='9' ){ yylval = c - '0'; return NUM; } return c; }
void yyerror(const char *s){ printf("%s\n", s); }
int main(int argc, char **argv){ int r = -1; in = argc>1 ? argv[1] : ""; int rc = yyparse_extra(&r); printf("rc=%d r=%d\n", rc, r); return 0; }
EOF
(cd "$W" && "$LIME" -q -T"$TPL" --target=c:bison g.lime) || { echo "FAIL lime"; exit 1; }
$CC -w -o "$W/t" "$W/main.c" "$W/g.c" "$W/g_bison.c" || { echo "FAIL cc"; exit 1; }
out=$($TO "$W/t" '1+2+3')
[ "$out" = "$(printf 'rc=0 r=6')" ] && echo "ok   char code '+' accepted" || { echo "FAIL accept: $out"; fails=$((fails+1)); }
out=$($TO "$W/t" '1*2')
case "$out" in *"rc=1"*) echo "ok   undeclared char rejected" ;; *) echo "FAIL undeclared: $out"; fails=$((fails+1)) ;; esac
echo "fails=$fails"
[ $fails -eq 0 ]
