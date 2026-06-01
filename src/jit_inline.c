/*
** src/jit_inline.c -- per-rule action body classifier.
**
** Pure-string analysis of a rule's action body to decide whether
** the body can be safely inlined into the parser hot path.  Used
** by two backends:
**
**   1. The runtime LLVM JIT (src/jit_codegen.c) -- inlines the
**      action body into the trace IR rather than dispatching
**      through yy_rule_reduce_fn[].
**
**   2. The Rust output emitter (src/emit_rust.c) -- emits
**      `#[inline(always)]` on yy_rule_N reducers so rustc inlines
**      them into the parse loop.
**
** The classifier itself has no LLVM or runtime dependencies, so it
** lives outside src/jit_codegen.c (which is conditionally compiled
** under LIME_NO_JIT) and is always available regardless of whether
** LLVM was found at configure time.
**
** Pre-v0.9 history: this code lived inside jit_codegen.c, gated on
** !LIME_NO_JIT.  In LIME_NO_JIT builds it returned false
** unconditionally, which was correct for the C+JIT path but wrong
** for the Rust path (where there is no JIT at all -- inlining is
** decided by ahead-of-time rustc, not by a runtime trace).  The
** classifier was hoisted here in feat/rust-inline-action-bodies so
** the Rust emitter can use it independently of LLVM availability.
*/

#include "jit_inline.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

/*
** Determine whether a rule's action body is small/pure enough to
** inline into the caller.
**
** Inlining simple rules (empty actions, passthrough `$$ = $1`,
** single arithmetic expressions) avoids indirect call overhead and
** lets the host compiler optimise across rule boundaries.  Complex
** actions (function calls, allocations, control flow) are left as
** indirect calls for maintainability and to keep code size bounded.
**
** Algorithm: scan the action code string for blacklist tokens:
**   - identifier(...)      -> function call
**   - goto/setjmp/longjmp/return keywords
**   - malloc/free/realloc/calloc
**   - Parse_*              -> generated parser callbacks
**   - if/while/for/switch  -> control flow
**   - mid-statement ';'    -> multi-statement body
**   - '{' or '}'           -> block statement
**   - body length >= 200   -> too large to inline
**
** Conservative: when in doubt, returns false to preserve
** correctness.  Null or empty code strings classify as inlinable
** (no-op reduces).
**
** The classifier targets C action bodies (Lime's primary output)
** but the syntactic vocabulary it inspects -- function-call
** parens, block braces, the keyword set -- overlaps cleanly with
** Rust, so the Rust emitter reuses it without modification.
*/
bool jit_can_inline_rule_text(const char *code, int no_code) {
    /* Empty actions or explicitly marked noCode are inlinable (no-ops). */
    if (no_code || code == NULL || code[0] == '\0') {
        return true;
    }
    size_t len = strlen(code);

    /* Simple passthrough: A = B; (single short identifier on each
    ** side, optional whitespace).  Catches the lemon idiom
    ** `$$ = $1;` after $-substitution renames the LHS/RHS to
    ** alias-named variables. */
    bool looks_like_passthrough = false;
    {
        const char *p = code;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p && (isalpha((unsigned char)*p) || *p == '_')) {
            p++;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '=') {
                p++;
                while (*p && isspace((unsigned char)*p)) p++;
                if (*p && (isalpha((unsigned char)*p) || *p == '_')) {
                    const char *id_start = p;
                    p++;
                    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
                    size_t id_len = (size_t)(p - id_start);
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (*p == ';') {
                        p++;
                        while (*p && isspace((unsigned char)*p)) p++;
                        if (*p == '\0' && id_len <= 3) {
                            looks_like_passthrough = true;
                        }
                    }
                }
            }
        }
    }
    if (looks_like_passthrough) return true;

    /* Single-line arithmetic expressions: no mid-statement
    ** semicolons, no curly braces, no function calls. */
    bool has_semicolon_mid = false;
    bool has_braces = false;
    bool has_function_call = false;

    for (size_t i = 0; i < len; i++) {
        char c = code[i];

        if (c == ';') {
            /* Trailing semicolon is OK; mid-statement is not. */
            size_t j = i + 1;
            while (j < len && isspace((unsigned char)code[j])) j++;
            if (j < len) {
                has_semicolon_mid = true;
            }
        }

        if (c == '{' || c == '}') {
            has_braces = true;
        }

        if (c == '(') {
            /* identifier(...) is a function call. */
            if (i > 0) {
                int j = (int)i - 1;
                while (j >= 0 && isspace((unsigned char)code[j])) j--;
                if (j >= 0 && (isalnum((unsigned char)code[j]) || code[j] == '_')) {
                    has_function_call = true;
                }
            }
        }
    }

    /* Blacklist keywords -- match only as standalone words, not
    ** as substrings of larger identifiers. */
    static const char *blacklist[] = {
        "goto", "setjmp", "longjmp", "return",
        "malloc", "free", "realloc", "calloc",
        "Parse_", "if", "while", "for", "switch",
        NULL
    };

    for (const char **kw = blacklist; *kw != NULL; kw++) {
        const char *found = strstr(code, *kw);
        if (found != NULL) {
            size_t kwlen = strlen(*kw);
            bool is_keyword = true;

            if (found > code) {
                char prev = *(found - 1);
                if (isalnum((unsigned char)prev) || prev == '_') {
                    is_keyword = false;
                }
            }

            if (is_keyword && found[kwlen] != '\0') {
                char next = found[kwlen];
                if (isalnum((unsigned char)next) || next == '_') {
                    is_keyword = false;
                }
            }

            if (is_keyword) {
                return false;
            }
        }
    }

    if (has_semicolon_mid || has_braces || has_function_call) {
        return false;
    }

    /* Cap at 200 chars to keep inlined code size reasonable. */
    return len < 200;
}
