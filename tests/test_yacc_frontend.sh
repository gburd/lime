#!/bin/sh
# tests/test_yacc_frontend.sh -- the yacc/bison front end (Track D).
#
# Each grammar below is plain yacc.  lime (as `lime-yacc`, i.e. the yacc
# command line) generates y.tab.c/y.tab.h; the program runs on a set of
# inputs; stdout, stderr and the exit status must equal the expected
# output written here, which is bison 3.8.2's output for the same grammar
# and inputs.  When bison is on PATH, it is run as well and must agree.
# Plus: char literals and bison token numbering in y.tab.h, and the named
# diagnostics for the features the front end does not support.
#
# usage: test_yacc_frontend.sh <lime-binary> <limpar.c>
set -u
LIME=$1
TPL=$2
W=$(mktemp -d)
trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
fails=0
CC=${CC:-cc}
ln -s "$LIME" "$W/lime-yacc"
cp "$TPL" "$W/limpar.c"   # found next to the binary, as when installed
HAVE_BISON=0
command -v bison >/dev/null 2>&1 && HAVE_BISON=1

fail() { echo "FAIL $*"; fails=$((fails + 1)); }
# Every generated program runs under a timeout: broken error recovery
# can loop forever, and that must FAIL the test, not hang it.
TO=""
command -v timeout >/dev/null 2>&1 && TO="timeout 10"

# --- 1. calculator with char literals, precedence, %union, error recovery
cat > "$W/calc.y" <<'EOF'
%{
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
int yylex(void);
void yyerror(const char *s) { printf("error: %s\n", s); }
%}
%union { int n; }
%token <n> NUM
%type <n> e
%left '+' '-'
%left '*' '/'
%right UMINUS
%%
input : /* empty */
      | input line
      ;
line  : '\n'
      | e '\n'          { printf("%d\n", $1); }
      | error '\n'      { yyerrok; printf("recovered\n"); }
      ;
e : e '+' e   { $$ = $1 + $3; }
  | e '-' e   { $$ = $1 - $3; }
  | e '*' e   { $$ = $1 * $3; }
  | e '/' e   { $$ = $3 ? $1 / $3 : 0; }
  | '-' e %prec UMINUS { $$ = -$2; }
  | '(' e ')' { $$ = $2; }
  | NUM
  | '[' { printf("mid\n"); } e ']' { $$ = 1000 + $3; }
  ;
%%
int yylex(void){
  int c = getchar();
  while( c==' ' ) c = getchar();
  if( c==EOF ) return 0;
  if( isdigit(c) ){ int v = 0; while( isdigit(c) ){ v = v*10 + c-'0'; c = getchar(); }
    ungetc(c, stdin); yylval.n = v; return NUM; }
  return c;
}
int main(void){ int r = yyparse(); printf("rc=%d\n", r); return r; }
EOF

# input|expected stdout (\n-separated), one case per line pair
run_case() {  # name prog input expected
  out=$(printf '%b' "$3" | $TO "$2" 2>&1)
  if [ $? -eq 124 ]; then fail "$1: hang (killed after 10 s)"; return; fi
  exp=$(printf '%b' "$4")
  if [ "$out" = "$exp" ]; then echo "ok   $1"; else
    fail "$1: got"; echo "$out" | sed 's/^/     /'; echo "     want"; echo "$exp" | sed 's/^/     /'; fi
}

(cd "$W" && ./lime-yacc -d calc.y) || fail "calc: lime-yacc failed"
[ -f "$W/y.tab.c" ] && [ -f "$W/y.tab.h" ] || fail "calc: y.tab.c / y.tab.h not written"
$CC -w -o "$W/calc" "$W/y.tab.c" || fail "calc: cc failed"
if [ $HAVE_BISON = 1 ]; then
  (cd "$W" && bison -y -o b.tab.c calc.y 2>/dev/null && $CC -w -o calcb b.tab.c) || fail "calc: bison build"
fi
for prog in calc calcb; do
  [ -x "$W/$prog" ] || continue
  run_case "$prog precedence"    "$W/$prog" '1+2*3\n2*3+1\n-2*3\n' '7\n7\n-6\nrc=0'
  run_case "$prog left assoc"    "$W/$prog" '10-4-3\n100/10/5\n' '3\n2\nrc=0'
  run_case "$prog parens"        "$W/$prog" '(1+2)*3\n' '9\nrc=0'
  run_case "$prog mid-rule"      "$W/$prog" '[2+3]\n' 'mid\n1005\nrc=0'
  run_case "$prog error recover" "$W/$prog" '1+\n2\n' 'error: syntax error\nrecovered\n2\nrc=0'
  run_case "$prog two errors"    "$W/$prog" '1 1\n+\n3\n' 'error: syntax error\nrecovered\nerror: syntax error\nrecovered\n3\nrc=0'
  run_case "$prog error at eof"  "$W/$prog" '1+' 'error: syntax error\nrc=1'
  run_case "$prog empty input"   "$W/$prog" '' 'rc=0'
done

# --- 2. no error rule: the first syntax error aborts, yyparse returns 1
cat > "$W/abort.y" <<'EOF'
%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *s) { printf("%s\n", s); }
%}
%token A B
%%
s : s A { printf("A\n"); } | B { printf("B\n"); } ;
%%
static const char *in; static int k;
int yylex(void){ char c = in[k++]; return c=='a' ? A : c=='b' ? B : c ? c : 0; }
int main(int argc, char **argv){ in = argc>1 ? argv[1] : ""; printf("rc=%d\n", yyparse()); return 0; }
EOF
(cd "$W" && ./lime-yacc -o abort.c abort.y && $CC -w -o abort abort.c) || fail "abort: build"
abort_case() {  # name arg expected
  out=$($TO "$W/abort" "$2")
  if [ $? -eq 124 ]; then fail "$1: hang (killed after 10 s)"; return; fi
  [ "$out" = "$(printf '%b' "$3")" ] && echo "ok   $1" || fail "$1: $out"
}
if [ -x "$W/abort" ]; then
  abort_case "abort accept"    baa   'B\nA\nA\nrc=0'
  abort_case "abort on error"  bab   'B\nA\nsyntax error\nrc=1'
  abort_case "undeclared char" 'b+a' 'B\nsyntax error\nrc=1'
  [ -f "$W/abort.h" ] && fail "abort: header written without -d" || echo "ok   no header without -d"
fi

# --- 3. bison token numbering in the header
cat > "$W/num.y" <<'EOF'
%token FIRST
%token SECOND 300
%token THIRD
%left '+'
%%
s : FIRST | SECOND '+' THIRD ;
EOF
(cd "$W" && ./lime-yacc -d -b num num.y) || fail "num: lime-yacc"
# bison: explicit codes stand; the rest (YYUNDEF included) take max+1 on.
for want in 'YYUNDEF = 301' 'FIRST = 302' 'SECOND = 300' 'THIRD = 303' '#define FIRST 302' '#define THIRD 303'; do
  grep -q "$want" "$W/num.tab.h" && echo "ok   numbering: $want" || fail "numbering: no '$want' in num.tab.h"
done
grep -q "'+'" "$W/num.tab.h" && fail "numbering: char literal leaked into the header" || echo "ok   char literal not in header"
if [ $HAVE_BISON = 1 ]; then
  (cd "$W" && bison -y -d -b numb num.y 2>/dev/null)
  a=$(grep '^#define [A-Z]' "$W/num.tab.h" | grep -v YYSTYPE | sort)
  b=$(grep '^#define [A-Z]' "$W/numb.tab.h" | grep -v 'YYSTYPE\|YYDEBUG\|YY_YY' | sort)
  [ "$a" = "$b" ] && echo "ok   token #defines equal bison's" || { fail "token #defines differ from bison"; echo "$a"; echo "$b"; }
fi

# --- 4. named diagnostics for unsupported constructs
expect_diag() {  # name grammar needle
  printf '%b' "$2" > "$W/d.y"
  out=$(cd "$W" && ./lime-yacc d.y 2>&1); rc=$?
  if [ $rc -eq 0 ]; then fail "$1: accepted"; return; fi
  case "$out" in *"$3"*) echo "ok   diag $1" ;; *) fail "$1: diagnostic lacks '$3': $out" ;; esac
}
expect_diag "%glr-parser" '%glr-parser\n%token A\n%%\ns : A ;\n' 'glr-parser'
expect_diag "YYERROR"     '%token A\n%%\ns : A { YYERROR; } ;\n'   'YYERROR'
expect_diag "\$name"      '%token A\n%%\ns : A { $$ = $A; } ;\n'   'named reference'
expect_diag "\$3 > m"     '%token A\n%%\ns : A { $$ = $3; } ;\n'   'exceeds'
out=$(cd "$W" && ./lime-yacc -p xx d.y 2>&1); case "$out" in *name-prefix*) echo "ok   diag -p" ;; *) fail "-p: $out" ;; esac

# --- 5. a Lemon grammar is still read as Lemon (no %% line), and --lemon
printf '%%token_type {int}\n%%token A.\ns ::= A.\n' > "$W/lemon.y"
(cd "$W" && "$LIME" -q -T"$TPL" lemon.y) && grep -q '#define A ' "$W/lemon.h" && echo "ok   lemon .y still Lemon" || fail "lemon .y"

echo "fails=$fails"
[ $fails -eq 0 ]
