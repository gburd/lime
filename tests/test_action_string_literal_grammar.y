/*
** P0-NEW-11 smoke grammar: action-body letter-label substitution
** must respect string literals, char literals, and comments.
**
** The cube-shaped reproducer that motivated the fix:
**
**     box(A) ::= LP NUM RP. {
**         errdetail("A cube cannot have more than %d ...");
**     }
**
** The buggy walk rewrote the bare `A` inside the string.  This
** grammar exercises every substitution-vulnerable position the
** state machine must protect:
**
**   - bare A in code position (must be substituted to a stack
**     slot reference)
**   - A inside a "..." string literal
**   - "..." string with `//` and `/ * * /` runs that must NOT
**     trigger comment state
**   - A inside a '...' char literal
**   - A inside a `// ...` line comment
**   - A inside a `/ * ... * /` block comment
**   - escape sequences (\\\" inside a string, \\' inside a char
**     literal) that must not prematurely close the literal
**
** The `(A)` LHS alias matches the cube reproducer.  The action
** body assigns to A in code position and stashes the verbatim
** strings/chars/comment-bracketed text into a capture struct
** the driver verifies.
*/
%name_prefix Pasl

%token_prefix PASL_

%token_type      { int }
%type box        { int }
%type expr       { int }
%extra_argument  { struct pasl_capture *cap }
%start_symbol    box

%token NUM LP RP.

%include {
#include "test_action_string_literal.h"
}

box(A) ::= LP expr(N) RP. {
    /* Code-position assignment: bare `A` must be rewritten to
    ** the LHS stack-slot reference.  Bare `N` must be rewritten
    ** to the RHS stack-slot reference. */
    A = N;

    /* String literals: every `A` and `N` must stay literal.  The
    ** "//" run inside a string must NOT enter line-comment
    ** state, and the "/ *" run must NOT enter block-comment
    ** state. */
    cap->s_plain = "A cube cannot have more than N dimensions.";
    cap->s_with_double_slash = "A//still-in-string-N";
    cap->s_with_block_open  = "A/*still-in-string-N*/";

    /* Escaped quote inside a string: the \" must not terminate
    ** the literal.  The body of the string still references A
    ** and must be preserved verbatim. */
    cap->s_with_escaped_quote = "an \"A\" inside an N";

    /* Char literals: 'A' must stay literal, 'N' too.  Lime's
    ** state machine just looks for the closing apostrophe. */
    cap->c_a = 'A';
    cap->c_n = 'N';

    /* Escaped apostrophe inside a char literal must not close
    ** the literal early; the byte after `\\` is consumed
    ** verbatim. */
    cap->c_apos = '\'';

    /* Block comment must protect bare A and N.  When the
    ** end-of-comment marker is reached, the state machine
    ** flips back to code state, and substitution resumes for
    ** subsequent code-position references. */
    cap->c_after_block = /* A and N inside block */ 1;

    /* Adjacent string literals: exit-string then enter-string
    ** with bare code (just whitespace) in between.  Both
    ** strings' interiors are protected. */
    cap->s_adjacent = "A first" " then N";

    /* Line comment must protect bare A and N until newline. */
    // line comment: A and N stay literal here
    cap->c_after_line = 1;
}

expr(E) ::= NUM(M). {
    E = M;
}
