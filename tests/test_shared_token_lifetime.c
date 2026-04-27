/*
** Unit tests for shared token lifetime guarantees.
**
** Verifies that tokens, snapshots, and token tables remain valid
** across reference-counted ownership boundaries: plugin unloads,
** composition source releases, concurrent access, and extension
** removal.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>

#include "parser.h"
#include "snapshot.h"
#include "snapshot_modify.h"
#include "parser_composition.h"
#include "token_table.h"
#include "jit_context.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
    fflush(stdout); \
} while (0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

/* ------------------------------------------------------------------ */
/*  Test helpers                                                        */
/* ------------------------------------------------------------------ */

/*
** Create a minimal snapshot with identifiable action table data.
** Simulates a "plugin" snapshot that can be acquired and released.
*/
static ParserSnapshot *make_plugin_snapshot(
    uint32_t nsymbol, uint32_t nterminal,
    uint32_t nrule, uint32_t nstate,
    uint32_t action_count)
{
    ParserSnapshot *snap = clone_snapshot(NULL);
    if (!snap) return NULL;

    snap->nsymbol = nsymbol;
    snap->nterminal = nterminal;
    snap->nrule = nrule;
    snap->nstate = nstate;
    snap->action_count = action_count;
    snap->lookahead_count = action_count;

    if (action_count > 0) {
        snap->yy_action = calloc(action_count, sizeof(uint16_t));
        snap->yy_lookahead = calloc(action_count, sizeof(uint16_t));
        if (!snap->yy_action || !snap->yy_lookahead) {
            snapshot_release(snap);
            return NULL;
        }
        for (uint32_t i = 0; i < action_count; i++) {
            snap->yy_action[i] = (uint16_t)(i + 1);
            snap->yy_lookahead[i] = (uint16_t)(i + 100);
        }
    }

    if (nstate > 0) {
        snap->yy_shift_ofst = calloc(nstate, sizeof(int16_t));
        snap->yy_reduce_ofst = calloc(nstate, sizeof(int16_t));
        snap->yy_default = calloc(nstate, sizeof(uint16_t));
        if (!snap->yy_shift_ofst || !snap->yy_reduce_ofst || !snap->yy_default) {
            snapshot_release(snap);
            return NULL;
        }
    }

    return snap;
}

/* ------------------------------------------------------------------ */
/*  Test 1: Token data survives plugin unload                          */
/* ------------------------------------------------------------------ */

/*
** Create a snapshot (simulating a plugin), populate it with action
** table data, acquire a reference, destroy the original, and verify
** the data is still accessible through the acquired reference.
*/
static void test_token_survives_plugin_unload(void) {
    TEST("token_survives_plugin_unload");

    /* Simulate a plugin creating a snapshot with token/action data. */
    ParserSnapshot *plugin_snap = make_plugin_snapshot(8, 4, 3, 2, 5);
    if (!plugin_snap) { FAIL("snapshot creation failed"); return; }

    /* A consumer acquires a reference before the plugin is unloaded. */
    ParserSnapshot *consumer_ref = snapshot_acquire(plugin_snap);
    assert(consumer_ref == plugin_snap);

    /* Verify refcount is 2. */
    uint_fast32_t rc = atomic_load(&plugin_snap->refcount);
    if (rc != 2) {
        FAIL("expected refcount 2 after acquire");
        snapshot_release(consumer_ref);
        snapshot_release(plugin_snap);
        return;
    }

    /* "Unload" the plugin by releasing its reference. */
    snapshot_release(plugin_snap);

    /* The consumer's reference should keep the snapshot alive. */
    rc = atomic_load(&consumer_ref->refcount);
    if (rc != 1) {
        FAIL("expected refcount 1 after plugin unload");
        snapshot_release(consumer_ref);
        return;
    }

    /* Verify action table data is still intact. */
    if (consumer_ref->action_count != 5) {
        FAIL("action_count corrupted after unload");
        snapshot_release(consumer_ref);
        return;
    }
    for (uint32_t i = 0; i < 5; i++) {
        if (consumer_ref->yy_action[i] != (uint16_t)(i + 1)) {
            FAIL("yy_action data corrupted after unload");
            snapshot_release(consumer_ref);
            return;
        }
    }
    if (consumer_ref->nsymbol != 8 || consumer_ref->nterminal != 4) {
        FAIL("symbol counts corrupted after unload");
        snapshot_release(consumer_ref);
        return;
    }

    snapshot_release(consumer_ref);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 2: Imported tokens survive source unload                      */
/* ------------------------------------------------------------------ */

/*
** Create two snapshots (A exports tokens, B imports from A).
** Compose them. Release snapshot A. Verify the composed snapshot
** still has all the merged token/action data.
*/
static void test_imported_tokens_survive_source_unload(void) {
    TEST("imported_tokens_survive_source_unload");

    ParserSnapshot *snap_a = make_plugin_snapshot(10, 5, 4, 3, 6);
    ParserSnapshot *snap_b = make_plugin_snapshot(6, 3, 2, 2, 4);
    if (!snap_a || !snap_b) {
        FAIL("snapshot creation failed");
        if (snap_a) snapshot_release(snap_a);
        if (snap_b) snapshot_release(snap_b);
        return;
    }

    /* Compose A and B. */
    ParserSnapshot *snaps[2] = { snap_a, snap_b };
    ParserSnapshot *composed = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &composed, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
        snapshot_release(snap_a);
        snapshot_release(snap_b);
        composition_diagnostics_destroy(&diag);
        return;
    }

    /* Record expected merged values before releasing sources. */
    uint32_t expected_rules = snap_a->nrule + snap_b->nrule;
    uint32_t expected_actions = snap_a->action_count + snap_b->action_count;

    /* "Unload" both source snapshots. */
    snapshot_release(snap_a);
    snapshot_release(snap_b);

    /* Composed snapshot should still be fully valid. */
    if (composed->nrule != expected_rules) {
        FAIL("rule count changed after source unload");
        snapshot_release(composed);
        composition_diagnostics_destroy(&diag);
        return;
    }
    if (composed->action_count != expected_actions) {
        FAIL("action count changed after source unload");
        snapshot_release(composed);
        composition_diagnostics_destroy(&diag);
        return;
    }
    if (composed->yy_action == NULL) {
        FAIL("action table pointer NULL after source unload");
        snapshot_release(composed);
        composition_diagnostics_destroy(&diag);
        return;
    }

    /* Verify action table data integrity. */
    bool data_ok = true;
    for (uint32_t i = 0; i < 6 && data_ok; i++) {
        if (composed->yy_action[i] != (uint16_t)(i + 1)) data_ok = false;
    }
    for (uint32_t i = 0; i < 4 && data_ok; i++) {
        if (composed->yy_action[6 + i] != (uint16_t)(i + 1)) data_ok = false;
    }
    if (!data_ok) {
        FAIL("action table data corrupted after source unload");
        snapshot_release(composed);
        composition_diagnostics_destroy(&diag);
        return;
    }

    snapshot_release(composed);
    composition_diagnostics_destroy(&diag);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 3: In-flight parse survives plugin unload                     */
/* ------------------------------------------------------------------ */

/*
** Acquire a snapshot, simulate an in-flight parse by reading action
** tables (including jit_find_shift_action fallback path), release
** the original, and continue reading from the acquired reference.
*/
static void test_inflight_parse_survives_plugin_unload(void) {
    TEST("inflight_parse_survives_plugin_unload");

    ParserSnapshot *plugin_snap = make_plugin_snapshot(6, 3, 2, 4, 8);
    if (!plugin_snap) { FAIL("snapshot creation failed"); return; }

    /* Set up shift offsets so jit_find_shift_action has something to read. */
    for (uint32_t i = 0; i < 4; i++) {
        plugin_snap->yy_shift_ofst[i] = 0;
        plugin_snap->yy_default[i] = (uint16_t)(i + 50);
    }

    /* "Begin parse" by acquiring a reference. */
    ParserSnapshot *parse_ref = snapshot_acquire(plugin_snap);

    /* Simulate some action table lookups mid-parse. */
    uint16_t action0 = parse_ref->yy_action[0];
    uint16_t action3 = parse_ref->yy_action[3];
    uint16_t default1 = parse_ref->yy_default[1];

    /* Use jit_find_shift_action (will use table-driven fallback since
    ** no JIT is compiled). State 0, lookahead 0. */
    uint16_t jit_result = jit_find_shift_action(parse_ref, 0, 0);

    /* "Unload plugin" -- release the original reference. */
    snapshot_release(plugin_snap);

    /* Continue the parse on the acquired reference. */
    if (parse_ref->action_count != 8) {
        FAIL("action_count corrupted during in-flight parse");
        snapshot_release(parse_ref);
        return;
    }

    /* Verify the earlier reads are consistent with the current state. */
    if (action0 != parse_ref->yy_action[0] ||
        action3 != parse_ref->yy_action[3] ||
        default1 != parse_ref->yy_default[1]) {
        FAIL("action table data changed during in-flight parse");
        snapshot_release(parse_ref);
        return;
    }

    /* Perform more lookups after the "unload". */
    uint16_t action7 = parse_ref->yy_action[7];
    if (action7 != 8) {
        FAIL("post-unload action table read returned wrong value");
        snapshot_release(parse_ref);
        return;
    }

    /* Another jit_find_shift_action call after unload. */
    uint16_t jit_result2 = jit_find_shift_action(parse_ref, 0, 0);
    if (jit_result != jit_result2) {
        FAIL("jit_find_shift_action returned different result after unload");
        snapshot_release(parse_ref);
        return;
    }

    /* "End parse" -- release our reference. */
    snapshot_release(parse_ref);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 4: Composed tokens survive module unload                      */
/* ------------------------------------------------------------------ */

/*
** Compose 3 snapshots with different token sets. Release the middle
** snapshot. Verify the composed result still works correctly.
*/
static void test_composed_tokens_survive_module_unload(void) {
    TEST("composed_tokens_survive_module_unload");

    ParserSnapshot *snap_a = make_plugin_snapshot(4, 2, 1, 1, 3);
    ParserSnapshot *snap_b = make_plugin_snapshot(6, 3, 2, 2, 4);
    ParserSnapshot *snap_c = make_plugin_snapshot(5, 2, 3, 1, 2);
    if (!snap_a || !snap_b || !snap_c) {
        FAIL("snapshot creation failed");
        if (snap_a) snapshot_release(snap_a);
        if (snap_b) snapshot_release(snap_b);
        if (snap_c) snapshot_release(snap_c);
        return;
    }

    ParserSnapshot *snaps[3] = { snap_a, snap_b, snap_c };
    ParserSnapshot *composed = NULL;

    CompositionResult cr = compose_snapshots(snaps, 3, NULL, &composed, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("3-way composition failed");
        snapshot_release(snap_a);
        snapshot_release(snap_b);
        snapshot_release(snap_c);
        return;
    }

    /* Record expected values. */
    uint32_t expected_rules = 1 + 2 + 3;
    uint32_t expected_actions = 3 + 4 + 2;

    /* Release the "middle" module. */
    snapshot_release(snap_b);

    /* Composed snapshot should be unaffected. */
    if (composed->nrule != expected_rules) {
        FAIL("rule count wrong after middle module unload");
        snapshot_release(snap_a);
        snapshot_release(snap_c);
        snapshot_release(composed);
        return;
    }
    if (composed->action_count != expected_actions) {
        FAIL("action count wrong after middle module unload");
        snapshot_release(snap_a);
        snapshot_release(snap_c);
        snapshot_release(composed);
        return;
    }

    /* Verify action data integrity: A's actions [1,2,3], B's [1,2,3,4], C's [1,2]. */
    bool ok = true;
    for (uint32_t i = 0; i < 3 && ok; i++) {
        if (composed->yy_action[i] != (uint16_t)(i + 1)) ok = false;
    }
    for (uint32_t i = 0; i < 4 && ok; i++) {
        if (composed->yy_action[3 + i] != (uint16_t)(i + 1)) ok = false;
    }
    for (uint32_t i = 0; i < 2 && ok; i++) {
        if (composed->yy_action[7 + i] != (uint16_t)(i + 1)) ok = false;
    }
    if (!ok) {
        FAIL("action data corrupted after middle module unload");
        snapshot_release(snap_a);
        snapshot_release(snap_c);
        snapshot_release(composed);
        return;
    }

    /* Release remaining sources; composed should still be fine. */
    snapshot_release(snap_a);
    snapshot_release(snap_c);

    /* Final verification after all sources released. */
    if (composed->nrule != expected_rules ||
        composed->action_count != expected_actions) {
        FAIL("composed data corrupted after all source unloads");
        snapshot_release(composed);
        return;
    }

    snapshot_release(composed);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 5: Token table shared across parsers                          */
/* ------------------------------------------------------------------ */

/*
** Create a TokenTable, use it from two snapshot references (simulating
** two parsers), destroy one reference's snapshot, verify the TokenTable
** is still valid for the other.
*/
static void test_token_table_shared_across_parsers(void) {
    TEST("token_table_shared_across_parsers");

    TokenTable *tt = create_token_table(32);
    if (!tt) { FAIL("token table creation failed"); return; }

    /* Populate with some tokens. */
    add_token(tt, "SELECT", 1, 0);
    add_token(tt, "FROM", 2, 0);
    add_token(tt, "WHERE", 3, 0);
    add_token(tt, "INSERT", 4, 0);

    /* Create two snapshots (simulating two parser instances sharing the
    ** same token table). The token table is external to the snapshots. */
    ParserSnapshot *snap1 = make_plugin_snapshot(4, 2, 2, 1, 0);
    ParserSnapshot *snap2 = snapshot_acquire(snap1);
    if (!snap1) {
        FAIL("snapshot creation failed");
        destroy_token_table(tt);
        return;
    }

    /* Both "parsers" can look up tokens. */
    int code1 = lookup_token(tt, "SELECT", 6);
    int code2 = lookup_token(tt, "WHERE", 5);
    if (code1 != 1 || code2 != 3) {
        FAIL("initial token lookup failed");
        snapshot_release(snap1);
        snapshot_release(snap2);
        destroy_token_table(tt);
        return;
    }

    /* "Destroy" one parser's snapshot reference. */
    snapshot_release(snap1);

    /* Token table should still work for the other parser. */
    int code3 = lookup_token(tt, "FROM", 4);
    int code4 = lookup_token(tt, "INSERT", 6);
    if (code3 != 2 || code4 != 4) {
        FAIL("token lookup failed after one snapshot released");
        snapshot_release(snap2);
        destroy_token_table(tt);
        return;
    }

    /* Verify case-insensitive lookup still works. */
    int code5 = lookup_token(tt, "select", 6);
    if (code5 != 1) {
        FAIL("case-insensitive lookup failed");
        snapshot_release(snap2);
        destroy_token_table(tt);
        return;
    }

    snapshot_release(snap2);
    destroy_token_table(tt);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 6: Concurrent unload during parse                             */
/* ------------------------------------------------------------------ */

/*
** Thread 1 reads from a snapshot continuously. Thread 2 releases the
** "original" reference. The snapshot survives because thread 1 acquired
** its own reference.
*/

typedef struct {
    ParserSnapshot *snap;
    atomic_int ready;
    atomic_int done;
    int result;  /* 0 = pass, non-zero = fail */
} ConcurrentTestData;

static void *reader_thread(void *arg) {
    ConcurrentTestData *data = (ConcurrentTestData *)arg;

    /* Signal that we are ready. */
    atomic_store_explicit(&data->ready, 1, memory_order_release);

    /* Read action table data in a loop. */
    data->result = 0;
    for (int iter = 0; iter < 10000; iter++) {
        if (data->snap->action_count != 6) {
            data->result = 1;
            break;
        }
        for (uint32_t i = 0; i < data->snap->action_count; i++) {
            if (data->snap->yy_action[i] != (uint16_t)(i + 1)) {
                data->result = 2;
                goto reader_done;
            }
        }
        if (data->snap->nsymbol != 8 || data->snap->nstate != 3) {
            data->result = 3;
            break;
        }
    }

reader_done:
    atomic_store_explicit(&data->done, 1, memory_order_release);
    return NULL;
}

static void test_concurrent_unload_during_parse(void) {
    TEST("concurrent_unload_during_parse");

    ParserSnapshot *original = make_plugin_snapshot(8, 4, 2, 3, 6);
    if (!original) { FAIL("snapshot creation failed"); return; }

    /* The reader acquires its own reference (simulating parse_begin). */
    ParserSnapshot *reader_ref = snapshot_acquire(original);

    ConcurrentTestData data;
    data.snap = reader_ref;
    atomic_init(&data.ready, 0);
    atomic_init(&data.done, 0);
    data.result = 0;

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, reader_thread, &data);
    if (rc != 0) {
        FAIL("pthread_create failed");
        snapshot_release(reader_ref);
        snapshot_release(original);
        return;
    }

    /* Wait for reader to be ready. */
    while (!atomic_load_explicit(&data.ready, memory_order_acquire)) {
        /* spin */
    }

    /* "Unload" the plugin by releasing the original reference.
    ** The reader's acquired reference keeps the snapshot alive. */
    snapshot_release(original);

    /* Wait for reader to finish. */
    pthread_join(tid, NULL);

    if (data.result != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "reader detected corruption (code %d)",
                 data.result);
        FAIL(buf);
        snapshot_release(reader_ref);
        return;
    }

    /* Verify data one more time on the main thread. */
    if (reader_ref->action_count != 6 ||
        reader_ref->yy_action[0] != 1 ||
        reader_ref->yy_action[5] != 6) {
        FAIL("data corrupted on main thread after concurrent test");
        snapshot_release(reader_ref);
        return;
    }

    snapshot_release(reader_ref);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Test 7: Extension tokens survive extension unload                  */
/* ------------------------------------------------------------------ */

/*
** Create a token table, add extension tokens under a specific
** ExtensionID, create a snapshot referencing the table, remove
** the extension tokens from the table, and verify the snapshot's
** independent action data is unaffected.
*/
static void test_extension_tokens_survive_extension_unload(void) {
    TEST("extension_tokens_survive_extension_unload");

    TokenTable *tt = create_token_table(32);
    if (!tt) { FAIL("token table creation failed"); return; }

    /* Add base tokens (ext_id = 0). */
    add_token(tt, "SELECT", 1, 0);
    add_token(tt, "FROM", 2, 0);

    /* Add extension tokens (ext_id = 42). */
    ExtensionID ext_id = 42;
    add_token(tt, "MATCH", 100, ext_id);
    add_token(tt, "AGAINST", 101, ext_id);
    add_token(tt, "FULLTEXT", 102, ext_id);

    /* Verify extension tokens are present. */
    if (lookup_token(tt, "MATCH", 5) != 100 ||
        lookup_token(tt, "AGAINST", 7) != 101) {
        FAIL("extension tokens not found before unload");
        destroy_token_table(tt);
        return;
    }

    /* Create a snapshot with its own independent action data.
    ** This simulates a snapshot taken while the extension was active. */
    ParserSnapshot *snap = make_plugin_snapshot(7, 4, 3, 2, 5);
    if (!snap) {
        FAIL("snapshot creation failed");
        destroy_token_table(tt);
        return;
    }

    /* Record the snapshot's state before extension removal. */
    uint32_t snap_actions = snap->action_count;
    uint32_t snap_symbols = snap->nsymbol;
    uint16_t snap_action_0 = snap->yy_action[0];
    uint16_t snap_action_4 = snap->yy_action[4];

    /* "Unload" the extension by removing its tokens. */
    remove_tokens_by_extension(tt, ext_id);

    /* Extension tokens should be gone from the table. */
    if (lookup_token(tt, "MATCH", 5) != -1 ||
        lookup_token(tt, "AGAINST", 7) != -1 ||
        lookup_token(tt, "FULLTEXT", 8) != -1) {
        FAIL("extension tokens still present after removal");
        snapshot_release(snap);
        destroy_token_table(tt);
        return;
    }

    /* Base tokens should still be there. */
    if (lookup_token(tt, "SELECT", 6) != 1 ||
        lookup_token(tt, "FROM", 4) != 2) {
        FAIL("base tokens lost after extension removal");
        snapshot_release(snap);
        destroy_token_table(tt);
        return;
    }

    /* The snapshot's data should be completely unaffected by the
    ** token table modification (snapshots own independent copies). */
    if (snap->action_count != snap_actions) {
        FAIL("snapshot action_count changed after extension removal");
        snapshot_release(snap);
        destroy_token_table(tt);
        return;
    }
    if (snap->nsymbol != snap_symbols) {
        FAIL("snapshot nsymbol changed after extension removal");
        snapshot_release(snap);
        destroy_token_table(tt);
        return;
    }
    if (snap->yy_action[0] != snap_action_0 ||
        snap->yy_action[4] != snap_action_4) {
        FAIL("snapshot action data changed after extension removal");
        snapshot_release(snap);
        destroy_token_table(tt);
        return;
    }

    /* Verify all action table entries are still intact. */
    for (uint32_t i = 0; i < snap->action_count; i++) {
        if (snap->yy_action[i] != (uint16_t)(i + 1)) {
            FAIL("snapshot action data corrupted after extension removal");
            snapshot_release(snap);
            destroy_token_table(tt);
            return;
        }
    }

    snapshot_release(snap);
    destroy_token_table(tt);
    PASS();
}

/* ================================================================== */
/*  Main                                                                */
/* ================================================================== */

int main(void) {
    printf("Shared token lifetime tests\n");
    printf("===========================\n\n");

    test_token_survives_plugin_unload();
    test_imported_tokens_survive_source_unload();
    test_inflight_parse_survives_plugin_unload();
    test_composed_tokens_survive_module_unload();
    test_token_table_shared_across_parsers();
    test_concurrent_unload_during_parse();
    test_extension_tokens_survive_extension_unload();

    printf("\n===========================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("===========================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
