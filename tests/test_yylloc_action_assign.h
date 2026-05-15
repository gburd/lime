/*
** tests/test_yylloc_action_assign.h -- shared types for P0-NEW-7
** test.  Mirrors ecpg's preproc.y location convention: locations
** are heap-allocated C strings that get concatenated across each
** reduce.  An action-body `@$ = cat_str(...)` writes the
** assembled source-text fragment to the LHS slot and the result
** must survive the post-action commit (the bug P0-NEW-7 fixes).
*/
#ifndef TEST_YYLLOC_ACTION_ASSIGN_H
#define TEST_YYLLOC_ACTION_ASSIGN_H

#include <stddef.h>

/* Tracking pool for cat_str-style allocations.  Mirrors ecpg's
** palloc-into-memory-context idiom: every yact_cat* result lands
** in the pool, and the pool is freed all at once at end of test.
** Without this the test trips ASan LeakSanitizer on the
** intermediate concatenations that get superseded by later
** reduces (which is exactly what happens in ecpg's preproc, but
** ecpg's pool is the parser memory context). */
struct yact_pool {
    char  **slots;
    size_t  n;
    size_t  cap;
};

void yact_pool_init(struct yact_pool *p);
void yact_pool_track(struct yact_pool *p, char *ptr);
void yact_pool_free(struct yact_pool *p);

struct yact_capture {
    /* Final LHS location captured at the start symbol's reduce. */
    char *final_loc;
    /* Per-rule debug captures. */
    char *e_loc;
    char *list_loc;
    int   list_seen;
    /* Allocation pool -- freed at end of sub-test. */
    struct yact_pool pool;
};

/* Concatenation helpers.  Both push their result into cap->pool
** so leak-tracking is centralised. */
char *yact_cat3(struct yact_capture *cap,
                const char *a, const char *b, const char *c);
char *yact_cat5(struct yact_capture *cap,
                const char *a, const char *b, const char *c,
                const char *d, const char *e);

#endif /* TEST_YYLLOC_ACTION_ASSIGN_H */
