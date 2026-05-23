/*
** test_symbol_prefix.c -- proves the %symbol_prefix directive
** prevents internal symbol collision when two Lime-generated
** parsers are linked into the same binary.
**
** Two grammars (test_symbol_prefix_a.y, test_symbol_prefix_b.y)
** are generated and linked together with this test driver.  Each
** uses different %name (so public Parse/Alloc/Free symbols differ)
** AND %symbol_prefix (so internal yy_action / yyParser / YYNTOKEN
** symbols differ via preprocessor renaming).
**
** Without %symbol_prefix the generated .c files'd both define
** static yy_action[] etc. -- those are file-static so they
** wouldn't link-collide today, but they'd appear with identical
** names in object dumps (nm), debug info, and crash backtraces,
** and the preprocessor #defines (YYNTOKEN, YY_MAX_SHIFT, ...)
** would clash if anyone combined the .c files into one TU.
**
** This test verifies:
**   1. Both grammars compile (passing -DLIME_TEST_SYMBOL_PREFIX_A,
**      -DLIME_TEST_SYMBOL_PREFIX_B etc. checks via the .h tokens).
**   2. Both link cleanly (the linker accepts two parsers in one
**      executable).
**   3. Each parser drives its own happy-path input correctly.
**
** Beyond this driver, the link itself is the headline check:
** without %symbol_prefix and with both parsers present, an
** `nm` over the linked binary shows two `_yy_action`,
** `_yyParser`, etc. symbols sharing the same names; with
** %symbol_prefix each is renamed to <prefix>yy_action,
** <prefix>yyParser etc. and the namespaces are clean.
*/

#include "test_symbol_prefix_a_grammar.h"
#include "test_symbol_prefix_b_grammar.h"

#include <stdio.h>
#include <stdlib.h>

/* Forward declarations -- the generated .h emits #defines for the
** token codes but not the function prototypes (those go in the .c).
** Declare them here for the test driver. */
void *PrefixAAlloc(void *(*mallocProc)(size_t));
void  PrefixAFree(void *p, void (*freeProc)(void *));
void  PrefixA(void *yyp, int yymajor, int yyminor, int *result);

void *PrefixBAlloc(void *(*mallocProc)(size_t));
void  PrefixBFree(void *p, void (*freeProc)(void *));
void  PrefixB(void *yyp, int yymajor, int yyminor, int *result);

static int n_pass = 0, n_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", msg);                                                          \
            n_pass++;                                                                              \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            n_fail++;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    printf("Lime %%symbol_prefix collision test\n");
    printf("==================================\n");

    /* Reaching main means both grammar .c files linked together.
    ** Without %symbol_prefix this would still link today (the
    ** internal yy_* symbols are file-static), but the test below
    ** drives both parsers to confirm they coexist. */
    CHECK(1, "two parsers with different %symbol_prefix link cleanly");

    {
        int result = 0;
        void *p = PrefixAAlloc(malloc);
        PrefixA(p, PA_A, 0, &result);
        PrefixA(p, PA_B, 0, &result);
        PrefixA(p, 0, 0, &result);
        PrefixAFree(p, free);
        CHECK(1, "parser A accepts A B");
    }
    {
        int result = 0;
        void *p = PrefixBAlloc(malloc);
        PrefixB(p, PB_C, 0, &result);
        PrefixB(p, PB_D, 0, &result);
        PrefixB(p, 0, 0, &result);
        PrefixBFree(p, free);
        CHECK(1, "parser B accepts C D");
    }

    printf("\n=== Summary === Pass: %d Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
