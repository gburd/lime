/*
** rule_inline.h -- per-rule action-body inlining classifier
**
** Decides whether a reduction rule's action body is small and pure
** enough for the host compiler to inline.  The Rust emitter
** (src/emit_rust.c) uses it to put #[inline(always)] on trivial
** yy_rule_N reducers.  Complex actions (function calls, allocations,
** control flow) are left for the compiler to decide.
*/
#ifndef RULE_INLINE_H
#define RULE_INLINE_H

#include <stdbool.h>

/*
** Classify a rule action body for inlining.
**
** Returns true if the rule body:
**   - Is empty (no_code != 0 or code == NULL or code[0] == '\0')
**   - Is a simple passthrough: $$ = $1 (single RHS reference)
**   - Is a single arithmetic/assignment expression with only $N references
**
** Returns false if the action:
**   - Contains function calls (e.g., malloc, Parse_*, user functions)
**   - Contains control flow (goto, setjmp, longjmp, return)
**   - Contains complex statements (loops, conditionals)
**   - References context beyond simple RHS symbol values
**
** Conservative: when in doubt, returns false to maintain correctness.
*/
bool rule_can_inline_text(const char *code, int no_code);

#endif /* RULE_INLINE_H */
