/*
** test_destructor.h -- shared header for the destructor smoke test.
*/
#ifndef TEST_DESTRUCTOR_H
#define TEST_DESTRUCTOR_H

#include <stddef.h>

/*
** Tracks every (alloc, free) pair so the test can assert that all
** stack-resident values get destructed on error recovery.  All
** allocations and frees go through this struct; the test fails if
** the counts don't match at parse_end time.
*/
typedef struct dtor_tracker {
    int alloc_count;
    int free_count;
} dtor_tracker;

static inline void dtor_track_alloc(dtor_tracker *t, const char *what) {
    (void)what;
    if (t) t->alloc_count++;
}

static inline void dtor_track_free(dtor_tracker *t, const char *what) {
    (void)what;
    if (t) t->free_count++;
}

#endif /* TEST_DESTRUCTOR_H */
