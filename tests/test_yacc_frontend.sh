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
# Compare against bison 3.x only: Lime implements bison 3.8's interface
# (macOS ships bison 2.3, whose header has no YYEMPTY/YYEOF/YYerror/YYUNDEF).
case "$(bison --version 2>/dev/null | head -1)" in
  *" 3."*) HAVE_BISON=1 ;;
esac

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

# --- 3b. pure parser, %parse-param/%lex-param, %name-prefix, %locations, %expect
cat > "$W/pure.y" <<'EOF'
%{
#include <stdio.h>
typedef struct { const char *s; int k; } scan_t;
%}
%pure-parser
%expect 0
%name-prefix "calc_"
%locations
%parse-param { int *result }
%parse-param { scan_t *sc }
%lex-param { scan_t *sc }
%union { int n; }
%{
int calc_lex(YYSTYPE *lval, YYLTYPE *lloc, scan_t *sc);
void calc_error(YYLTYPE *lloc, int *result, scan_t *sc, const char *m);
%}
%token <n> NUM
%type <n> e
%left '+'
%%
top : e { *result = $1; printf("at col %d-%d\n", @1.first_column, @1.last_column); } ;
e : e '+' e { $$ = $1 + $3; } | NUM ;
%%
int calc_lex(YYSTYPE *lval, YYLTYPE *lloc, scan_t *sc){
  char c = sc->s[sc->k];
  lloc->first_line = lloc->last_line = 1;
  lloc->first_column = lloc->last_column = sc->k;
  if( c==0 ) return 0;
  sc->k++;
  if( c>='0' && c<='9' ){ lval->n = c - '0'; return NUM; }
  return c;
}
void calc_error(YYLTYPE *lloc, int *result, scan_t *sc, const char *m){
  (void)result; printf("%s at %d (k=%d)\n", m, lloc->first_column, sc->k);
}
int main(int argc, char **argv){
  int r = -1; scan_t sc = { argc>1 ? argv[1] : "", 0 };
  int rc = calc_parse(&r, &sc);
  printf("rc=%d r=%d\n", rc, r);
  return 0;
}
EOF
(cd "$W" && ./lime-yacc -o pure.c pure.y && $CC -w -o pure pure.c) || fail "pure: build"
pure_case() {  # name arg expected
  out=$($TO "$W/pure" "$2")
  if [ $? -eq 124 ]; then fail "$1: hang (killed after 10 s)"; return; fi
  [ "$out" = "$(printf '%b' "$3")" ] && echo "ok   $1" || fail "$1: $out"
}
if [ -x "$W/pure" ]; then
  pure_case "pure parse-param"  '1+2+3' 'at col 0-4\nrc=0 r=6'
  pure_case "pure yyerror args" '1++'   'syntax error at 2 (k=3)\nrc=1 r=-1'
fi
if grep -q 'calc_parse' "$W/pure.c" && ! grep -q '^YYSTYPE yylval;' "$W/pure.c"; then
  echo "ok   pure: no global yylval, renamed yyparse"
else
  fail "pure: globals / prefix"
fi
# %expect counts s/r only; r/r need %expect-rr (bison)
printf '%%expect 0\n%%token A\n%%%%\ns : x | y ;\nx : A ;\ny : A ;\n' > "$W/rr.y"
out=$(cd "$W" && ./lime-yacc rr.y 2>&1)
case "$out" in *"reduce/reduce conflicts: 1 found, 0 expected"*) echo "ok   %expect-rr default 0" ;;
  *) fail "%expect-rr: $out" ;; esac
printf '%%expect 0\n%%expect-rr 1\n%%token A\n%%%%\ns : x | y ;\nx : A ;\ny : A ;\n' > "$W/rr.y"
# bison 3.8 LALR: %expect-rr is ignored (GLR only), so this still fails
(cd "$W" && ./lime-yacc rr.y >/dev/null 2>&1) && fail "%expect-rr 1 accepted (bison rejects)" || echo "ok   %expect-rr ignored, as bison"
printf '%%expect 1\n%%token A\n%%%%\ns : A ;\n' > "$W/ex.y"
out=$(cd "$W" && ./lime-yacc ex.y 2>&1); rc=$?
[ $rc -ne 0 ] && echo "ok   %expect mismatch fails" || fail "%expect mismatch accepted: $out"

# --- 3c. YYERROR in an action: recover with no message, as bison
cat > "$W/yyerr.y" <<'EOF'
%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *s) { printf("yyerror: %s\n", s); }
%}
%token NUM
%%
list : /* empty */ | list line ;
line : e '\n' { printf("= %d\n", $1); }
     | error '\n' { yyerrok; printf("recovered\n"); }
     ;
e : NUM { if ($1 == 0) { printf("zero rejected\n"); YYERROR; } $$ = $1; }
  | e '+' NUM { $$ = $1 + $3; }
  ;
%%
int yylex(void){ int c = getchar(); while (c==' ') c = getchar();
  if (c==EOF) return 0; if (c>='0'&&c<='9'){ yylval = c-'0'; return NUM; } return c; }
int main(void){ int r = yyparse(); printf("rc=%d\n", r); return 0; }
EOF
(cd "$W" && ./lime-yacc -o yyerr.c yyerr.y && $CC -w -o yyerr yyerr.c) || fail "yyerr: build"
if [ -x "$W/yyerr" ]; then
  run_case "YYERROR recover"  "$W/yyerr" '1+2\n0+1\n3\n' '= 3\nzero rejected\nrecovered\n= 3\nrc=0'
  run_case "YYERROR at eof"   "$W/yyerr" '0' 'zero rejected\nrc=1'
fi

# --- 3d. push parser (%define api.push-pull push), pure: yypush_parse()
cat > "$W/push.y" <<'EOF'
%{
#include <stdio.h>
#include <stdlib.h>
void yyerror(const char *s) { printf("%s\n", s); }
%}
%define api.pure full
%define api.push-pull push
%define api.value.type {int}
%token NUM
%left '+'
%%
top : e { printf("%d\n", $1); } ;
e : e '+' e { $$ = $1 + $3; } | NUM ;
%%
int main(int argc, char **argv){
  const char *in = argc>1 ? argv[1] : "";
  yypstate *ps = yypstate_new();
  int st, k = 0;
  do {
    YYSTYPE v = 0; int c = in[k] ? in[k++] : 0;
    if( c>='0' && c<='9' ){ v = c - '0'; c = NUM; }
    st = yypush_parse(ps, c, &v);
  } while( st==YYPUSH_MORE );
  yypstate_delete(ps);
  printf("rc=%d\n", st);
  return 0;
}
EOF
(cd "$W" && ./lime-yacc -o push.c push.y && $CC -w -o push push.c) || fail "push: build"
if [ -x "$W/push" ]; then
  out=$($TO "$W/push" '1+2+3'); [ "$out" = "$(printf '6\nrc=0')" ] && echo "ok   push accept" || fail "push accept: $out"
  out=$($TO "$W/push" '1++');   [ "$out" = "$(printf 'syntax error\nrc=1')" ] && echo "ok   push error" || fail "push error: $out"
fi

# --- 3e. `return v;` in an action ends yyparse() with v (bison)
printf '%%{\n#include <stdio.h>\nint yylex(void){ static const char *s = "aab"; return *s ? *s++ : 0; }\nvoid yyerror(const char *m){ printf("%%s\\n", m); }\n%%}\n%%%%\ns : s %sa%s { printf("a\\n"); } | s %sb%s { printf("b\\n"); return 7; } | ;\n%%%%\nint main(void){ printf("rc=%%d\\n", yyparse()); return 0; }\n' "'" "'" "'" "'" > "$W/ret.y"
(cd "$W" && ./lime-yacc -o ret.c ret.y && $CC -w -o ret ret.c) || fail "ret: build"
[ -x "$W/ret" ] && run_case "action return" "$W/ret" '' 'a\na\nb\nrc=7'
# two yacc parsers with different prefixes link into one program
printf '%%{\nint one_lex(void); void one_error(const char *);\n%%}\n%%name-prefix "one_"\n%%token A\n%%%%\ns : A ;\n%%%%\nint one_lex(void){ return 0; }\nvoid one_error(const char *m){ (void)m; }\n' > "$W/one.y"
printf '%%{\nint two_lex(void); void two_error(const char *);\n%%}\n%%name-prefix "two_"\n%%token B\n%%%%\ns : B ;\n%%%%\nint two_lex(void){ return 0; }\nvoid two_error(const char *m){ (void)m; }\nint main(void){ return 0; }\n' > "$W/two.y"
(cd "$W" && ./lime-yacc -o one.c one.y && ./lime-yacc -o two.c two.y && $CC -w -o onetwo one.c two.c) \
  && echo "ok   two prefixed parsers link" || fail "two prefixed parsers: link"

# --- 3f. yychar + yyclearin in an empty rule's action (plpgsql's
# read_datatype): the action sees the lookahead and consumes it.
cat > "$W/clr.y" <<'EOF'
%{
#include <stdio.h>
static const char *in; static int k;
int yylex(void){ while (in[k]==' ') k++; return in[k] ? in[k++] : 0; }
void yyerror(const char *s){ printf("yyerror: %s\n", s); }
static int read_type(int tok){ int n = 1; printf("read_type got %d\n", tok); while (in[k] && in[k] != ';') { k++; n++; } return n; }
%}
%%
decls : | decls decl ;
decl : 'v' decl_const decl_type ';' { printf("decl const=%d type=%d\n", $2, $3); } ;
decl_const : { $$ = 0; } | 'c' { $$ = 1; } ;
decl_type : { $$ = read_type(yychar); yyclearin; } ;
%%
int main(int argc, char **argv){ in = argc>1 ? argv[1] : ""; int r = yyparse(); printf("rc=%d\n", r); return 0; }
EOF
(cd "$W" && ./lime-yacc -o clr.c clr.y && $CC -w -o clr clr.c) || fail "clr: build"
if [ -x "$W/clr" ]; then
  out=$($TO "$W/clr" 'v int; v c x y;')
  # bison: after 'c' is shifted no lookahead is read, so yychar is YYEMPTY (-2)
  want=$(printf "read_type got 105\ndecl const=0 type=3\nread_type got -2\ndecl const=1 type=5\nrc=0")
  [ "$out" = "$want" ] && echo "ok   yychar + yyclearin in an action" || fail "yyclearin: $out"
fi

# --- 4. named diagnostics for unsupported constructs
expect_diag() {  # name grammar needle
  printf '%b' "$2" > "$W/d.y"
  out=$(cd "$W" && ./lime-yacc d.y 2>&1); rc=$?
  if [ $rc -eq 0 ]; then fail "$1: accepted"; return; fi
  case "$out" in *"$3"*) echo "ok   diag $1" ;; *) fail "$1: diagnostic lacks '$3': $out" ;; esac
}
expect_diag "%glr-parser" '%glr-parser\n%token A\n%%\ns : A ;\n' 'glr-parser'
expect_diag "\$nosuch"    '%token A\n%%\ns : A { $$ = $nosuch; } ;\n'   'named reference'
expect_diag "\$ambiguous" '%token A\n%%\ns : A A { $$ = $A; } ;\n'   'named reference'
expect_diag "\$3 > m"     '%token A\n%%\ns : A { $$ = $3; } ;\n'   'exceeds'


# --- 5. a Lemon grammar is still read as Lemon (no %% line), and --lemon
printf '%%token_type {int}\n%%token A.\ns ::= A.\n' > "$W/lemon.y"
(cd "$W" && "$LIME" -q -T"$TPL" lemon.y) && grep -q '#define A ' "$W/lemon.h" && echo "ok   lemon .y still Lemon" || fail "lemon .y"

# --- 6. invoked by bare name through $PATH, as `./configure YACC=lime-yacc`
# does.  The rewritten argument vector is longer than the original; writing
# it over argv[] used to clobber environ[0] ($PATH) and lose limpar.c.
mkdir -p "$W/pathbin" "$W/pathrun"
ln -s "$LIME" "$W/pathbin/lime-yacc"
cp "$TPL" "$W/pathbin/limpar.c"
printf '%%token N\n%%%%\ne: N ;\n%%%%\n' > "$W/pathrun/p.y"
(cd "$W/pathrun" && PATH="$W/pathbin:$PATH" lime-yacc -d p.y >/dev/null 2>&1) \
  && test -s "$W/pathrun/y.tab.c" && echo "ok   bare lime-yacc via PATH" \
  || fail "bare lime-yacc via PATH did not write y.tab.c"

echo "fails=$fails"
[ $fails -eq 0 ]
