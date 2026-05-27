/*
** jit_inline.h -- per-rule reducer inlining policy for JIT traces
**
** Helper functions to determine whether a reduction rule's action body
** can be safely inlined into a JIT-compiled trace versus dispatching
** through yy_rule_reduce_fn[].
**
** Inlining simple rules (empty actions, passthrough `$$ = $1`, single
** arithmetic expressions) avoids indirect call overhead and enables
** LLVM to optimize across rule boundaries. Complex actions (function
** calls, allocations, control flow) are left as indirect calls for
** maintainability and to keep trace code size bounded.
*/
#ifndef JIT_INLINE_H
#define JIT_INLINE_H

#include <stdbool.h>
#include <stdint.h>

/*
** Classify a rule action body for inlining.
**
** Returns true if the rule body:
**   - Is empty (no_code != 0 or code == NULL or code[0] == '\0')
**   - Is a simple passthrough: $$ = $1 (single RHS reference)
**   - Is a single arithmetic/assignment expression with only $N references
**
** Returns false (dispatch through yy_rule_reduce_fn[]) if the action:
**   - Contains function calls (e.g., malloc, Parse_*, user functions)
**   - Contains control flow (goto, setjmp, longjmp, return)
**   - Contains complex statements (loops, conditionals)
**   - References context beyond simple RHS symbol values
**
** Conservative: when in doubt, returns false to maintain correctness.
**
** Plain-data signature so callers without a `struct rule` declaration
** in scope (jit_codegen.c, the standalone test harness, future out-
** of-tree consumers) can use it directly.  The lime generator (which
** has `struct rule` available) calls this with rp->code and rp->noCode.
*/
bool jit_can_inline_rule_text(const char *code, int no_code);

/*
** Instrumentation counters for JIT reduce inlining statistics.
** Updated during trace compilation (jit_codegen_generate_into).
** Exposed via jit_get_inline_stats for testing and diagnostics.
*/
typedef struct {
    uint32_t inline_count;    /* Rules inlined into traces */
    uint32_t dispatch_count;  /* Rules dispatched via function pointer */
    uint32_t total_rules;     /* Total number of rules processed */
} JITInlineStats;

/*
** Retrieve current inline/dispatch counters.
** Returns zeroed stats if ctx is NULL or JIT unavailable.
*/
void jit_get_inline_stats(uint32_t *inline_count, uint32_t *dispatch_count);

#endif /* JIT_INLINE_H */
