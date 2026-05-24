/*
** Concurrent stress tests for thread safety validation.
**
** Tests:
**   1. Concurrent snapshot acquire/release from many threads
**   2. Concurrent token lookups while writer adds/removes tokens
**   3. Concurrent parse_begin/parse_end with shared snapshots
**   4. Extension registry concurrent register/load/unload
**   5. Snapshot refcount stress under high contention
**
** Designed to expose data races when run under Thread Sanitizer (TSan).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lime_threads.h"
#include <stdatomic.h>
#include <assert.h>

#include "snapshot.h"
#include "token_table.h"
#include "parse_context.h"
#include "extension.h"

/* -------------------------------------------------------------------
** Test framework
** ------------------------------------------------------------------- */

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  ASSERTION FAILED: %s (line %d)\n", msg, __LINE__); \
        return 1; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("  %-55s", #fn); \
    fflush(stdout); \
    int _rc = fn(); \
    if (_rc == 0) { \
        printf("PASS\n"); \
        g_tests_passed++; \
    } else { \
        printf("FAIL\n"); \
    } \
} while (0)

/* -------------------------------------------------------------------
** Helper: create a test snapshot with action tables allocated
** ------------------------------------------------------------------- */

static ParserSnapshot *create_test_snapshot(uint32_t nstate, uint32_t naction) {
    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (!snap) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->nstate = nstate;
    snap->action_count = naction;
    snap->lookahead_count = naction;

    snap->yy_action = calloc(naction, sizeof(uint16_t));
    snap->yy_lookahead = calloc(naction, sizeof(uint16_t));
    snap->yy_shift_ofst = calloc(nstate, sizeof(int32_t));
    snap->yy_reduce_ofst = calloc(nstate, sizeof(int32_t));
    snap->yy_default = calloc(nstate, sizeof(uint16_t));

    /* Fill with recognizable patterns */
    for (uint32_t i = 0; i < naction; i++) {
        snap->yy_action[i] = (uint16_t)(i % 256);
        snap->yy_lookahead[i] = (uint16_t)(i % 128);
    }
    for (uint32_t i = 0; i < nstate; i++) {
        snap->yy_shift_ofst[i] = (int32_t)(i % 64);
        snap->yy_reduce_ofst[i] = (int32_t)(i % 32);
        snap->yy_default[i] = (uint16_t)(i % 16);
    }

    return snap;
}

/* -------------------------------------------------------------------
** Test 1: Concurrent snapshot acquire/release
**
** Multiple threads simultaneously acquire and release references to
** a shared snapshot.  The snapshot must not be freed while any thread
** still holds a reference, and must eventually be freed when all
** references are released.
** ------------------------------------------------------------------- */

#define SNAPSHOT_THREADS 16
#define SNAPSHOT_ITERATIONS 10000

typedef struct {
    ParserSnapshot *snap;
    atomic_int errors;
} SnapshotTestCtx;

static void *snapshot_stress_thread(void *arg) {
    SnapshotTestCtx *ctx = (SnapshotTestCtx *)arg;

    for (int i = 0; i < SNAPSHOT_ITERATIONS; i++) {
        ParserSnapshot *ref = snapshot_acquire(ctx->snap);
        if (ref == NULL) {
            atomic_fetch_add(&ctx->errors, 1);
            continue;
        }

        /* Read some data to ensure the snapshot is still valid */
        volatile uint64_t v = ref->version;
        volatile uint32_t ns = ref->nstate;
        (void)v;
        (void)ns;

        /* Read action table data */
        if (ref->yy_action && ref->action_count > 0) {
            volatile uint16_t a = ref->yy_action[0];
            (void)a;
        }

        snapshot_release(ref);
    }

    return NULL;
}

static int test_concurrent_snapshot_acquire_release(void) {
    ParserSnapshot *snap = create_test_snapshot(64, 256);
    ASSERT_TRUE(snap != NULL, "create test snapshot");

    SnapshotTestCtx ctx = { .snap = snap };
    atomic_init(&ctx.errors, 0);

    pthread_t threads[SNAPSHOT_THREADS];
    for (int i = 0; i < SNAPSHOT_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, snapshot_stress_thread, &ctx);
        ASSERT_TRUE(rc == 0, "thread create");
    }

    for (int i = 0; i < SNAPSHOT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no errors during concurrent acquire/release");

    /* Original refcount should be back to 1 (our reference) */
    uint_fast32_t rc = atomic_load(&snap->refcount);
    ASSERT_TRUE(rc == 1, "refcount back to 1 after all threads done");

    snapshot_release(snap);
    return 0;
}

/* -------------------------------------------------------------------
** Test 2: Concurrent token lookup while writer modifies table
**
** Reader threads continuously look up tokens while a writer thread
** adds and removes tokens.  Readers must always get consistent results
** (a valid token code or -1, never garbage).
** ------------------------------------------------------------------- */

#define TOKEN_READER_THREADS 8
#define TOKEN_WRITER_ITERATIONS 500
#define TOKEN_READER_ITERATIONS 50000

typedef struct {
    TokenTable *table;
    atomic_int reader_errors;
    atomic_int writer_done;
} TokenTestCtx;

static void *token_reader_thread(void *arg) {
    TokenTestCtx *ctx = (TokenTestCtx *)arg;
    const char *keywords[] = {
        "SELECT", "FROM", "WHERE", "INSERT", "UPDATE",
        "DELETE", "CREATE", "DROP", "ALTER", "INDEX"
    };
    int nkeywords = sizeof(keywords) / sizeof(keywords[0]);

    for (int i = 0; i < TOKEN_READER_ITERATIONS; i++) {
        const char *kw = keywords[i % nkeywords];
        int code = lookup_token(ctx->table, kw, strlen(kw));

        /* Code must be either -1 (not found) or a valid positive number */
        if (code != -1 && code < 0) {
            atomic_fetch_add(&ctx->reader_errors, 1);
        }

        /* Also test case-insensitive lookup */
        code = lookup_token(ctx->table, "select", 6);
        if (code != -1 && code < 0) {
            atomic_fetch_add(&ctx->reader_errors, 1);
        }
    }

    return NULL;
}

static void *token_writer_thread(void *arg) {
    TokenTestCtx *ctx = (TokenTestCtx *)arg;

    for (int iter = 0; iter < TOKEN_WRITER_ITERATIONS; iter++) {
        /* Add tokens from "extension" */
        ExtensionID ext_id = (ExtensionID)(100 + (iter % 10));

        char name[32];
        snprintf(name, sizeof(name), "EXT_TOKEN_%d", iter);
        add_token(ctx->table, name, 5000 + iter, ext_id);

        /* Periodically remove extension tokens */
        if (iter % 50 == 49) {
            remove_tokens_by_extension(ctx->table, ext_id);
        }
    }

    atomic_store(&ctx->writer_done, 1);
    return NULL;
}

static int test_concurrent_token_read_write(void) {
    TokenTable *table = create_token_table(128);
    ASSERT_TRUE(table != NULL, "create token table");

    /* Pre-populate with base keywords */
    add_token(table, "SELECT", 100, 0);
    add_token(table, "FROM", 101, 0);
    add_token(table, "WHERE", 102, 0);
    add_token(table, "INSERT", 103, 0);
    add_token(table, "UPDATE", 104, 0);
    add_token(table, "DELETE", 105, 0);
    add_token(table, "CREATE", 106, 0);
    add_token(table, "DROP", 107, 0);
    add_token(table, "ALTER", 108, 0);
    add_token(table, "INDEX", 109, 0);

    TokenTestCtx ctx = { .table = table };
    atomic_init(&ctx.reader_errors, 0);
    atomic_init(&ctx.writer_done, 0);

    /* Start readers + writer */
    pthread_t readers[TOKEN_READER_THREADS];
    pthread_t writer;

    for (int i = 0; i < TOKEN_READER_THREADS; i++) {
        int rc = pthread_create(&readers[i], NULL, token_reader_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create reader thread");
    }
    int rc = pthread_create(&writer, NULL, token_writer_thread, &ctx);
    ASSERT_TRUE(rc == 0, "create writer thread");

    /* Wait for all to finish */
    for (int i = 0; i < TOKEN_READER_THREADS; i++) {
        pthread_join(readers[i], NULL);
    }
    pthread_join(writer, NULL);

    int reader_errors = atomic_load(&ctx.reader_errors);
    ASSERT_TRUE(reader_errors == 0, "no invalid token codes from concurrent reads");

    /* Verify base tokens still accessible */
    int code = lookup_token(table, "SELECT", 6);
    ASSERT_TRUE(code == 100, "base token SELECT still present");

    code = lookup_token(table, "FROM", 4);
    ASSERT_TRUE(code == 101, "base token FROM still present");

    destroy_token_table(table);
    return 0;
}

/* -------------------------------------------------------------------
** Test 3: Concurrent parse_begin/parse_end
**
** Multiple threads create and destroy ParseContexts from the same
** snapshot.  The snapshot's refcount must remain consistent.
** ------------------------------------------------------------------- */

#define PARSE_CTX_THREADS 12
#define PARSE_CTX_ITERATIONS 5000

typedef struct {
    ParserSnapshot *snap;
    atomic_int errors;
} ParseCtxTestCtx;

static void *parse_ctx_stress_thread(void *arg) {
    ParseCtxTestCtx *ctx = (ParseCtxTestCtx *)arg;

    for (int i = 0; i < PARSE_CTX_ITERATIONS; i++) {
        ParseContext *pctx = parse_begin(ctx->snap);
        if (pctx == NULL) {
            atomic_fetch_add(&ctx->errors, 1);
            continue;
        }

        /* Verify snapshot is accessible */
        ParserSnapshot *s = parse_get_snapshot(pctx);
        if (s == NULL || s != ctx->snap) {
            atomic_fetch_add(&ctx->errors, 1);
        }

        parse_end(pctx);
    }

    return NULL;
}

static int test_concurrent_parse_context(void) {
    ParserSnapshot *snap = create_test_snapshot(32, 128);
    ASSERT_TRUE(snap != NULL, "create test snapshot");

    ParseCtxTestCtx ctx = { .snap = snap };
    atomic_init(&ctx.errors, 0);

    pthread_t threads[PARSE_CTX_THREADS];
    for (int i = 0; i < PARSE_CTX_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, parse_ctx_stress_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create thread");
    }

    for (int i = 0; i < PARSE_CTX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no errors in concurrent parse context create/destroy");

    uint_fast32_t rc = atomic_load(&snap->refcount);
    ASSERT_TRUE(rc == 1, "snapshot refcount back to 1");

    snapshot_release(snap);
    return 0;
}

/* -------------------------------------------------------------------
** Test 4: Extension registry concurrent operations
**
** Multiple threads register extensions concurrently.  Each should get
** a unique ExtensionID and no registrations should be lost.
** ------------------------------------------------------------------- */

#define EXT_REG_THREADS 8
#define EXT_REG_PER_THREAD 100

/* Simple get_modifications callback that does nothing */
static bool noop_get_mods(void *ud, const struct ParserSnapshot *base,
                          GrammarModification **mods_out, uint32_t *nmods_out) {
    (void)ud; (void)base;
    *mods_out = NULL;
    *nmods_out = 0;
    return true;
}

typedef struct {
    ExtensionRegistry *reg;
    atomic_int errors;
    ExtensionID ids[EXT_REG_THREADS][EXT_REG_PER_THREAD];
    atomic_int thread_idx;
} ExtRegTestCtx;

static void *ext_register_thread(void *arg) {
    ExtRegTestCtx *ctx = (ExtRegTestCtx *)arg;
    int tidx = atomic_fetch_add(&ctx->thread_idx, 1);

    for (int i = 0; i < EXT_REG_PER_THREAD; i++) {
        char name[64];
        snprintf(name, sizeof(name), "ext_t%d_i%d", tidx, i);

        ExtensionInfo info = {
            .name = name,
            .version = "1.0.0",
            .get_modifications = noop_get_mods,
            .on_conflict = NULL,
            .on_unload = NULL,
            .user_data = NULL,
        };

        ExtensionID id = 0;
        bool ok = register_extension(ctx->reg, &info, &id);
        if (!ok || id == 0) {
            atomic_fetch_add(&ctx->errors, 1);
        }
        ctx->ids[tidx][i] = id;
    }

    return NULL;
}

static int test_concurrent_extension_registration(void) {
    ExtensionRegistry *reg = create_extension_registry();
    ASSERT_TRUE(reg != NULL, "create extension registry");

    ExtRegTestCtx ctx = { .reg = reg };
    atomic_init(&ctx.errors, 0);
    atomic_init(&ctx.thread_idx, 0);
    memset(ctx.ids, 0, sizeof(ctx.ids));

    pthread_t threads[EXT_REG_THREADS];
    for (int i = 0; i < EXT_REG_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, ext_register_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create thread");
    }

    for (int i = 0; i < EXT_REG_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no registration errors");

    /* Verify all IDs are unique */
    int total = EXT_REG_THREADS * EXT_REG_PER_THREAD;
    ExtensionID *all_ids = malloc(total * sizeof(ExtensionID));
    int idx = 0;
    for (int t = 0; t < EXT_REG_THREADS; t++) {
        for (int i = 0; i < EXT_REG_PER_THREAD; i++) {
            all_ids[idx++] = ctx.ids[t][i];
        }
    }

    /* Simple uniqueness check: sort and scan for duplicates */
    for (int i = 0; i < total - 1; i++) {
        for (int j = i + 1; j < total; j++) {
            if (all_ids[i] != 0 && all_ids[i] == all_ids[j]) {
                fprintf(stderr, "  Duplicate ID: %u at indices %d and %d\n",
                        all_ids[i], i, j);
                free(all_ids);
                ASSERT_TRUE(0, "all extension IDs are unique");
            }
        }
    }
    free(all_ids);

    /* Verify total count */
    /* Extensions are registered but not loaded, so loaded count should be 0 */
    /* We just verify the registry didn't lose any registrations */
    ASSERT_TRUE(reg->count == (uint32_t)total,
                "registry has correct number of extensions");

    destroy_extension_registry(reg);
    return 0;
}

/* -------------------------------------------------------------------
** Test 5: Snapshot refcount stress - high contention
**
** All threads hammer acquire/release as fast as possible with a very
** short hold time to maximize contention on the atomic refcount.
** ------------------------------------------------------------------- */

#define REFCOUNT_THREADS 32
#define REFCOUNT_ITERATIONS 100000

typedef struct {
    ParserSnapshot *snap;
    atomic_int errors;
} RefcountStressCtx;

static void *refcount_stress_thread(void *arg) {
    RefcountStressCtx *ctx = (RefcountStressCtx *)arg;

    for (int i = 0; i < REFCOUNT_ITERATIONS; i++) {
        ParserSnapshot *ref = snapshot_acquire(ctx->snap);
        if (ref == NULL) {
            atomic_fetch_add(&ctx->errors, 1);
            continue;
        }
        /* Minimal hold time -- immediately release */
        snapshot_release(ref);
    }

    return NULL;
}

static int test_snapshot_refcount_stress(void) {
    ParserSnapshot *snap = create_test_snapshot(8, 32);
    ASSERT_TRUE(snap != NULL, "create test snapshot");

    RefcountStressCtx ctx = { .snap = snap };
    atomic_init(&ctx.errors, 0);

    pthread_t threads[REFCOUNT_THREADS];
    for (int i = 0; i < REFCOUNT_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, refcount_stress_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create thread");
    }

    for (int i = 0; i < REFCOUNT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no errors in refcount stress");

    uint_fast32_t rc = atomic_load(&snap->refcount);
    ASSERT_TRUE(rc == 1, "refcount exactly 1 after stress");

    snapshot_release(snap);
    return 0;
}

/* -------------------------------------------------------------------
** Test 6: Mixed readers and snapshot swapper (rwlock-protected)
**
** Simulates hot-swapping a snapshot while readers are actively using
** the old one.  Uses a rwlock to protect the load-then-acquire window:
** readers hold the read lock while they load the pointer and call
** snapshot_acquire; the writer holds the write lock while it swaps the
** pointer and releases the old snapshot.  This eliminates the TOCTOU
** race between loading the atomic pointer and incrementing the refcount.
**
** NOTE: A lock-free approach (hazard pointers or epoch-based
** reclamation) would eliminate the rwlock overhead but is deferred
** to a future iteration.
** ------------------------------------------------------------------- */

#define SWAP_READER_THREADS 8
#define SWAP_COUNT 50

typedef struct {
    ParserSnapshot *current;
    pthread_rwlock_t swap_lock;
    atomic_int errors;
    atomic_int swapper_done;
} SwapTestCtx;

static void *swap_reader_thread(void *arg) {
    SwapTestCtx *ctx = (SwapTestCtx *)arg;

    while (!atomic_load(&ctx->swapper_done)) {
        /* Hold read lock while we load the pointer and acquire a ref.
        ** This guarantees the snapshot isn't freed between load and acquire. */
        pthread_rwlock_rdlock(&ctx->swap_lock);
        ParserSnapshot *ref = snapshot_acquire(ctx->current);
        pthread_rwlock_unlock(&ctx->swap_lock);

        if (ref == NULL) {
            atomic_fetch_add(&ctx->errors, 1);
            continue;
        }

        /* Simulate some work with the snapshot */
        volatile uint64_t v = ref->version;
        (void)v;

        if (ref->yy_default && ref->nstate > 0) {
            volatile uint16_t d = ref->yy_default[0];
            (void)d;
        }

        snapshot_release(ref);
    }

    return NULL;
}

static void *swap_writer_thread(void *arg) {
    SwapTestCtx *ctx = (SwapTestCtx *)arg;

    for (int i = 0; i < SWAP_COUNT; i++) {
        /* Create a new snapshot */
        ParserSnapshot *new_snap = create_test_snapshot(16, 64);
        if (!new_snap) {
            atomic_fetch_add(&ctx->errors, 1);
            continue;
        }
        new_snap->version = (uint64_t)(i + 2);

        /* Hold write lock while swapping.  Release the old snapshot
        ** inside the lock to ensure no reader is between load-and-acquire
        ** when we drop the global reference.  Readers who already called
        ** snapshot_acquire() before this lock hold their own reference and
        ** will keep the snapshot alive until they call snapshot_release(). */
        pthread_rwlock_wrlock(&ctx->swap_lock);
        ParserSnapshot *old = ctx->current;
        ctx->current = new_snap;
        snapshot_release(old);
        pthread_rwlock_unlock(&ctx->swap_lock);

        /* Yield to let readers interleave */
        for (volatile int j = 0; j < 2000; j++) {}
    }

    atomic_store(&ctx->swapper_done, 1);
    return NULL;
}

static int test_concurrent_snapshot_swap(void) {
    ParserSnapshot *initial = create_test_snapshot(16, 64);
    ASSERT_TRUE(initial != NULL, "create initial snapshot");

    SwapTestCtx ctx;
    ctx.current = initial;
    atomic_init(&ctx.errors, 0);
    atomic_init(&ctx.swapper_done, 0);
    pthread_rwlock_init(&ctx.swap_lock, NULL);

    pthread_t readers[SWAP_READER_THREADS];
    pthread_t writer;

    for (int i = 0; i < SWAP_READER_THREADS; i++) {
        int rc = pthread_create(&readers[i], NULL, swap_reader_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create reader thread");
    }
    int rc = pthread_create(&writer, NULL, swap_writer_thread, &ctx);
    ASSERT_TRUE(rc == 0, "create writer thread");

    pthread_join(writer, NULL);
    for (int i = 0; i < SWAP_READER_THREADS; i++) {
        pthread_join(readers[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no errors during snapshot swap");

    /* Release the final snapshot */
    snapshot_release(ctx.current);

    pthread_rwlock_destroy(&ctx.swap_lock);
    return 0;
}

/* -------------------------------------------------------------------
** Test 7: Extension load/unload while registry is being read
**
** Reader threads look up extensions while a writer loads and unloads
** them.  The registry lock must prevent corruption.
** ------------------------------------------------------------------- */

#define EXT_LU_READER_THREADS 6
#define EXT_LU_WRITER_ITERATIONS 200

typedef struct {
    ExtensionRegistry *reg;
    atomic_int errors;
    atomic_int writer_done;
    ExtensionID base_ids[10];
    int nbase;
} ExtLUTestCtx;

static void *ext_lu_reader_thread(void *arg) {
    ExtLUTestCtx *ctx = (ExtLUTestCtx *)arg;

    while (!atomic_load(&ctx->writer_done)) {
        for (int i = 0; i < ctx->nbase; i++) {
            const Extension *ext = find_extension(ctx->reg, ctx->base_ids[i]);
            if (ext == NULL) {
                /* Extension should always be findable since we don't
                ** remove registrations, only load/unload */
                atomic_fetch_add(&ctx->errors, 1);
            }
        }
    }

    return NULL;
}

static void *ext_lu_writer_thread(void *arg) {
    ExtLUTestCtx *ctx = (ExtLUTestCtx *)arg;

    /* Create a test snapshot for loading */
    ParserSnapshot *snap = create_test_snapshot(8, 32);

    for (int iter = 0; iter < EXT_LU_WRITER_ITERATIONS; iter++) {
        int idx = iter % ctx->nbase;
        ExtensionID id = ctx->base_ids[idx];

        const Extension *ext = find_extension(ctx->reg, id);
        if (!ext) continue;

        char *error = NULL;
        if (ext->state == EXT_REGISTERED || ext->state == EXT_UNLOADED) {
            load_extension(ctx->reg, id, snap, &error);
            free(error);
        } else if (ext->state == EXT_LOADED) {
            unload_extension(ctx->reg, id);
        }
    }

    if (snap) snapshot_release(snap);
    atomic_store(&ctx->writer_done, 1);
    return NULL;
}

static int test_concurrent_extension_load_unload(void) {
    ExtensionRegistry *reg = create_extension_registry();
    ASSERT_TRUE(reg != NULL, "create extension registry");

    ExtLUTestCtx ctx = { .reg = reg, .nbase = 10 };
    atomic_init(&ctx.errors, 0);
    atomic_init(&ctx.writer_done, 0);

    /* Register 10 extensions */
    for (int i = 0; i < ctx.nbase; i++) {
        char name[32];
        snprintf(name, sizeof(name), "ext_lu_%d", i);

        ExtensionInfo info = {
            .name = name,
            .version = "1.0.0",
            .get_modifications = noop_get_mods,
            .on_conflict = NULL,
            .on_unload = NULL,
            .user_data = NULL,
        };

        bool ok = register_extension(reg, &info, &ctx.base_ids[i]);
        ASSERT_TRUE(ok, "register extension for LU test");
    }

    pthread_t readers[EXT_LU_READER_THREADS];
    pthread_t writer;

    for (int i = 0; i < EXT_LU_READER_THREADS; i++) {
        int rc = pthread_create(&readers[i], NULL, ext_lu_reader_thread, &ctx);
        ASSERT_TRUE(rc == 0, "create reader thread");
    }
    int rc = pthread_create(&writer, NULL, ext_lu_writer_thread, &ctx);
    ASSERT_TRUE(rc == 0, "create writer thread");

    pthread_join(writer, NULL);
    for (int i = 0; i < EXT_LU_READER_THREADS; i++) {
        pthread_join(readers[i], NULL);
    }

    int errors = atomic_load(&ctx.errors);
    ASSERT_TRUE(errors == 0, "no errors during concurrent load/unload");

    destroy_extension_registry(reg);
    return 0;
}

/* -------------------------------------------------------------------
** Main
** ------------------------------------------------------------------- */

int main(void) {
    printf("Concurrent Stress Tests (Thread Safety)\n");
    printf("========================================\n\n");

    printf("Snapshot system:\n");
    RUN_TEST(test_concurrent_snapshot_acquire_release);
    RUN_TEST(test_snapshot_refcount_stress);
    RUN_TEST(test_concurrent_snapshot_swap);

    printf("\nToken table:\n");
    RUN_TEST(test_concurrent_token_read_write);

    printf("\nParse context:\n");
    RUN_TEST(test_concurrent_parse_context);

    printf("\nExtension registry:\n");
    RUN_TEST(test_concurrent_extension_registration);
    RUN_TEST(test_concurrent_extension_load_unload);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
