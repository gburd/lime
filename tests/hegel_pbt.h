/*
** hegel_pbt.h -- thin run-loop harness over libhegel's C ABI.
**
** libhegel (hegeldev/hegel-rust, crate `hegeltest-c`) exposes the raw
** property-based-testing engine: the caller owns the run loop, asks
** for one test case at a time, draws values, and reports the outcome.
** It replaced the old socket-protocol client (gburd/hegel-c), whose
** hegel_session_new / hegel_run_test / hegel_draw_int / hegel_assume
** API no longer exists -- the wire protocol it spoke was removed
** upstream, which is why every PBT in this directory skipped at
** runtime for its entire existence.
**
** This header is the whole shim.  Four test files need the same run
** loop, so it lives here once instead of four times.
**
** NOTE FOR ANYONE CONSULTING EXTERNAL HEGEL DOCS: some locally
** installed hegel skill/reference material still documents only the
** removed socket API (hegel_session_new / hegel_run_test /
** hegel_draw_int, plus a HEGEL_SERVER_COMMAND env var for spawning a
** `hegel` server subprocess).  None of that applies here -- libhegel
** is in-process and there is no server.  Treat the shipped `hegel.h`
** from the linked libhegel as the only authority, and this header as
** the worked example.
**
** Mapping from the old API:
**
**   hegel_session_new()                  -> (nothing; no server)
**   hegel_run_test(s, fn, ud, &settings) -> hpbt_run(name, fn, ud, n)
**   hegel_draw_int(tc, hegel_integers(lo, hi))
**                                        -> hpbt_int(c, lo, hi)
**   hegel_assume(cond)                   -> hpbt_assume(c, cond)
**   hegel_note(msg)                      -> hpbt_note(c, msg)
**   hegel_note(...) + assert(0)          -> HPBT_FAIL(c, fmt, ...)
**   hegel_results.passed                 -> hpbt_run() == 0
**
** Two behavioural notes:
**
**   * The old tests reported a violation with assert(0), which aborts
**     the process, so the engine never got to shrink the
**     counterexample.  HPBT_FAIL instead reports the case as
**     HEGEL_STATUS_INTERESTING, which is what lets libhegel shrink to
**     a minimal counterexample.  A violated property still fails the
**     test; it just prints the shrunk input now.
**
**   * A draw can return HEGEL_E_STOP_TEST when the engine's choice
**     budget runs out mid-case.  C has no exceptions, so hpbt_int
**     records the stop and returns the low bound; property bodies
**     check hpbt_done(c) in their loop conditions and unwind
**     normally, so their cleanup still runs (no longjmp past live
**     allocations).
*/

#ifndef LIME_TESTS_HEGEL_PBT_H
#define LIME_TESTS_HEGEL_PBT_H

#include <hegel.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Per-test-case state handed to a property body                      */
/* ------------------------------------------------------------------ */

typedef struct hpbt_case {
    hegel_context_t *ctx;
    hegel_test_case_t *tc;
    int stopped;        /* a draw hit HEGEL_E_STOP_TEST (budget gone) */
    int invalid;        /* an assumption was violated (hpbt_assume)   */
    int failed;         /* the property was violated (HPBT_FAIL)      */
    const char *origin; /* stable per-assertion-site failure grouping */
} hpbt_case;

typedef void (*hpbt_body)(hpbt_case *c, void *user_data);

/*
** Most recent note text recorded during the current run.  libhegel
** shrinks after the first failure, so the last note written belongs
** to the minimal counterexample: that is what hpbt_run prints.
** Single-threaded by construction -- one property at a time.
*/
static char hpbt_last_note[512];

/* ------------------------------------------------------------------ */
/*  Drawing                                                            */
/* ------------------------------------------------------------------ */

/*
** Draw an integer in [lo, hi] inclusive.  Direct replacement for
** hegel_draw_int(tc, hegel_integers(lo, hi)).  On budget exhaustion
** the case is marked stopped and `lo` is returned, so the caller's
** arithmetic stays in range while it unwinds.
*/
static inline int64_t hpbt_int(hpbt_case *c, int64_t lo, int64_t hi) {
    int64_t v = lo;
    if (c->stopped) return lo;
    if (hegel_generate_integer(c->ctx, c->tc, lo, hi, &v) != HEGEL_OK) {
        c->stopped = 1;
        return lo;
    }
    return v;
}

/*
** Replacement for hegel_assume(cond): a false condition makes the
** case invalid, so libhegel discards it without counting it as
** either a pass or a failure, and without charging it against the
** max-examples budget.
*/
static inline void hpbt_assume(hpbt_case *c, int cond) {
    if (!cond) c->invalid = 1;
}

/* Replacement for hegel_note(msg). */
static inline void hpbt_note(hpbt_case *c, const char *msg) {
    snprintf(hpbt_last_note, sizeof hpbt_last_note, "%s", msg);
    (void)hegel_note(c->ctx, c->tc, (const uint8_t *)msg, strlen(msg));
}

/*
** True once the body should stop drawing and unwind: budget gone,
** assumption violated, or the property already failed.
*/
static inline int hpbt_done(const hpbt_case *c) {
    return c->stopped || c->invalid || c->failed;
}

static inline void hpbt_fail_at(hpbt_case *c, const char *origin, const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    hpbt_note(c, msg);
    c->failed = 1;
    c->origin = origin;
}

/*
** Report a property violation.  libhegel groups failures by origin
** and shrinks each group separately, so the origin must be stable per
** assertion site: file:line of the call.
*/
#define HPBT_STR2(x) #x
#define HPBT_STR(x) HPBT_STR2(x)
#define HPBT_FAIL(c, ...) hpbt_fail_at((c), __FILE__ ":" HPBT_STR(__LINE__), __VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  Run loop                                                           */
/* ------------------------------------------------------------------ */

/*
** Run one property for up to `max_examples` valid test cases.  Prints
** a one-line summary in the shape the old harness used, plus the
** shrunk counterexample when the property fails.
**
** Returns 0 when the property held, 1 otherwise.
*/
static inline int hpbt_run(const char *name, hpbt_body body, void *user_data,
                           uint64_t max_examples) {
    hegel_context_t *ctx = hegel_context_new();
    hegel_settings_t *settings = NULL;
    hegel_run_t *run = NULL;
    hegel_run_result_t *result = NULL;
    unsigned valid = 0, invalid = 0, overrun = 0, interesting = 0;
    int rc = 1;

    if (ctx == NULL) {
        fprintf(stderr, "%s: hegel_context_new failed\n", name);
        return 1;
    }
    if (hegel_settings_new(ctx, &settings) != HEGEL_OK) {
        fprintf(stderr, "%s: hegel_settings_new: %s\n", name, hegel_context_last_error(ctx));
        hegel_context_free(ctx);
        return 1;
    }
    hegel_settings_set_test_cases(ctx, settings, max_examples);
    /* "" disables the on-disk example database.  These run from the
    ** meson build tree; a persistent .hegel/ directory there would
    ** make one run's outcome depend on the previous run's. */
    hegel_settings_set_database(ctx, settings, "");

    if (hegel_run_start(ctx, settings, NULL, NULL, &run) != HEGEL_OK) {
        fprintf(stderr, "%s: hegel_run_start: %s\n", name, hegel_context_last_error(ctx));
        hegel_settings_free(ctx, settings);
        hegel_context_free(ctx);
        return 1;
    }

    for (;;) {
        hpbt_case c;
        hegel_test_case_t *tc = NULL;
        uint32_t status;

        if (hegel_next_test_case(ctx, run, &tc) != HEGEL_OK) {
            fprintf(stderr, "%s: hegel_next_test_case: %s\n", name, hegel_context_last_error(ctx));
            goto out;
        }
        if (tc == NULL) break; /* run finished */

        c.ctx = ctx;
        c.tc = tc;
        c.stopped = 0;
        c.invalid = 0;
        c.failed = 0;
        c.origin = NULL;

        body(&c, user_data);

        if (c.failed) {
            status = HEGEL_STATUS_INTERESTING;
            interesting++;
        } else if (c.invalid) {
            status = HEGEL_STATUS_INVALID;
            invalid++;
        } else if (c.stopped) {
            status = HEGEL_STATUS_OVERRUN;
            overrun++;
        } else {
            status = HEGEL_STATUS_VALID;
            valid++;
        }
        hegel_mark_complete(ctx, tc, status, c.failed ? c.origin : NULL);
        hegel_test_case_free(ctx, tc);
    }

    if (hegel_run_result(ctx, run, &result) != HEGEL_OK) {
        fprintf(stderr, "%s: hegel_run_result: %s\n", name, hegel_context_last_error(ctx));
        goto out;
    }

    {
        hegel_run_status_t st = HEGEL_RUN_STATUS_ERROR;
        const char *err = NULL;
        size_t nfail = 0, i;

        hegel_run_result_status(ctx, result, &st);
        hegel_run_result_error(ctx, result, &err);
        hegel_run_result_failure_count(ctx, result, &nfail);

        printf("%s: %s (%u valid / %u interesting / %u invalid / %u overrun)\n", name,
               st == HEGEL_RUN_STATUS_PASSED ? "PASS" : "FAIL", valid, interesting, invalid,
               overrun);

        if (err != NULL && err[0] != '\0') printf("%s: run error: %s\n", name, err);

        for (i = 0; i < nfail; i++) {
            hegel_failure_t *f = NULL;
            const char *origin = NULL, *blob = NULL;
            if (hegel_run_result_failure(ctx, result, i, &f) != HEGEL_OK) continue;
            hegel_failure_origin(ctx, f, &origin);
            hegel_failure_reproduction_blob(ctx, f, &blob);
            printf("%s: FAILURE at %s\n", name, origin ? origin : "(unknown)");
            printf("%s:   minimal counterexample: %s\n", name,
                   hpbt_last_note[0] ? hpbt_last_note : "(no note recorded)");
            if (blob != NULL) printf("%s:   reproduce blob: %s\n", name, blob);
            hegel_failure_free(ctx, f);
        }

        rc = (st == HEGEL_RUN_STATUS_PASSED) ? 0 : 1;
    }

out:
    hegel_run_result_free(ctx, result);
    hegel_run_free(ctx, run);
    hegel_settings_free(ctx, settings);
    hegel_context_free(ctx);
    fflush(stdout);
    return rc;
}

/*
** Report the linked libhegel version, or NULL if the library failed
** to initialise.  With the in-process engine there is no server to be
** unavailable, so this is effectively always non-NULL; it exists so
** the four tests degrade rather than crash in the pathological case.
*/
static inline const char *hpbt_version(void) {
    hegel_context_t *ctx = hegel_context_new();
    const char *v = NULL;

    if (ctx == NULL) return NULL;
    if (hegel_version(ctx, &v) != HEGEL_OK) v = NULL;
    hegel_context_free(ctx);
    return v;
}

#endif /* LIME_TESTS_HEGEL_PBT_H */
