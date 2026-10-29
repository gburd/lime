#!/bin/sh
# tests/test_bison_diagnostics.sh -- every row of docs/BISON_COMPAT.md
# marked **Rejected** must be refused by lime with a diagnostic that names
# the construct (not a generic "illegal character" / "unknown keyword"),
# and the alias form Lime does accept must still generate.
#
# usage: test_bison_diagnostics.sh <lime-binary> <limpar.c>
set -u
LIME=$1
TPL=$2
W=$(mktemp -d)
trap 'find "$W" -mindepth 1 -delete; rmdir "$W"' EXIT
fails=0

# expect_reject NAME GRAMMAR NEEDLE
expect_reject() {
    printf '%b' "$2" > "$W/g.y"
    out=$(cd "$W" && "$LIME" -q -T"$TPL" g.y 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        echo "FAIL $1: accepted (expected rejection)"; fails=$((fails + 1)); return
    fi
    case "$out" in
        *"$3"*) echo "ok   $1" ;;
        *) echo "FAIL $1: diagnostic lacks '$3':"; echo "     $out"; fails=$((fails + 1)) ;;
    esac
}

# expect_reject_rc NAME GRAMMAR NEEDLE -- as expect_reject, and the exit
# status must be exactly 1 (not a signal / abort, not the error count)
expect_reject_rc() {
    printf '%b' "$2" > "$W/g.y"
    out=$(cd "$W" && "$LIME" -q -T"$TPL" g.y 2>&1)
    rc=$?
    if [ "$rc" -ne 1 ]; then
        echo "FAIL $1: exit status $rc (want 1)"; echo "$out" | head -3 | sed "s/^/     /"; fails=$((fails + 1)); return
    fi
    case "$out" in
        *"$3"*) echo "ok   $1" ;;
        *) echo "FAIL $1: diagnostic lacks '$3':"; echo "     $out"; fails=$((fails + 1)) ;;
    esac
}

# expect_accept NAME GRAMMAR
expect_accept() {
    printf '%b' "$2" > "$W/g.y"
    out=$(cd "$W" && "$LIME" -q -T"$TPL" g.y 2>&1)
    if [ $? -eq 0 ]; then echo "ok   $1"; else
        echo "FAIL $1: rejected:"; echo "     $out"; fails=$((fails + 1)); fi
}

expect_reject "mid-rule action"  '%name T\n%token A B.\ns ::= A { x(); } B.\n'            "mid-rule action"
expect_reject "%precedence"      '%name T\n%token A P.\n%precedence P.\ns ::= A.\n'       "%nonassoc"
expect_reject "%define"          '%name T\n%define api.pure full\n%token A.\ns ::= A.\n'  "bison directive"
expect_reject "%parse-param"     '%name T\n%parse-param { int *x }\n%token A.\ns ::= A.\n' "%extra_argument"
expect_reject "%printer"         '%name T\n%printer { } A\n%token A.\ns ::= A.\n'         "%destructor"
expect_reject "%glr-parser"      '%name T\n%glr-parser\n%token A.\ns ::= A.\n'            "glr_parse"
expect_reject "\$\$ = \$1"       '%name T\n%token_type {int}\n%type s {int}\n%token A.\ns ::= A. { $$ = $1; }\n' "names values by alias"
expect_reject "\$<type>N"        '%name T\n%token A B.\ns ::= A B. { u($<int>2); }\n'     "names values by alias"
expect_reject "%expect mismatch" '%name T\n%expect 0\n%token A.\ns ::= e.\ne ::= e A e.\ne ::= A.\n' "Expected 0 conflict"

# yacc syntax pasted into lime: one named diagnostic, not a cascade.
expect_reject "%% section"       '%token NUM\n%%\nexp: NUM ;\n'                         "looks like a yacc/bison"
expect_reject "%{ prologue"      '%{\n#include <stdio.h>\n%}\n%token A.\ns ::= A.\n'   "looks like a yacc/bison"
expect_reject "yacc colon rule"  '%name T\n%token A.\ns : A ;\n'                         "lhs ::= a | b."
expect_reject "char literal"     "%name T\ns ::= A '+' A.\n"                             "no character or string literal tokens"
expect_reject "string alias"     '%name T\ns ::= A "+" A.\n'                             "no character or string literal tokens"
expect_reject "%empty"           '%name T\n%token A.\ns ::= %empty.\n'                   "write nothing"
expect_reject "%expect-rr"       '%name T\n%expect-rr 0\n%token A.\ns ::= A.\n'         "%expect-rr is not supported"
# Lemon aborted (SIGABRT in resolve_conflict) on a start symbol used on an
# RHS, the yacc idiom `input: | input line ;`.  Must be a clean error.
expect_reject_rc "start symbol on RHS" '%name T\n%token A.\ns ::= s A.\ns ::= A.\n'     "start ::= s."
# exit(errorcnt) wrapped to 0 at 256 errors.
many=$(i=0; while [ $i -lt 256 ]; do printf '%%token lc%d.\\n' $i; i=$((i + 1)); done)
expect_reject_rc "256 errors exit 1" "%name T\n${many}%token A.\ns ::= A.\n"              "should be a token"

# The diagnostics must not fire on legitimate code.
expect_accept "alias form"       '%name T\n%token_type {int}\n%type s {int}\n%token A.\ns(L) ::= A(X). { L = X; }\n'
expect_accept "\$1 in a string"  '%name T\n%token A.\ns ::= A. { puts("cost $1"); }\n'
expect_accept "\$\$ in a comment" '%name T\n%token A.\ns ::= A. { /* was $$ = $1 */ }\n'
expect_accept "wrapper start rule" '%name T\n%token A.\nstart ::= s.\ns ::= s A.\ns ::= A.\n'
expect_accept "% in an action"   '%name T\n%token A.\ns ::= A. {\n%%x;\n}\n'
expect_accept "%expect match"    '%name T\n%expect 1\n%token A.\ns ::= e.\ne ::= e A e.\ne ::= A.\n'

[ "$fails" -eq 0 ] && echo "all bison diagnostics OK" || echo "$fails failure(s)"
exit "$fails"
