/*
** test_composition_e2e -- end-to-end demonstration of compose_snapshots()
** against runtime-loaded .so plugins.
**
** The companion fixtures in tests/composition_e2e_fixtures/ build into
** three shared libraries (sql_lite_plugin, json_plugin, xml_plugin),
** each producing an identifiable synthetic ParserSnapshot via the
** LimeParserPlugin interface.  This driver:
**
**   1. dlopens sql_lite_plugin.so and json_plugin.so via ParserManager
**   2. Calls each plugin's create_snapshot() to obtain a snapshot
**   3. Composes the two snapshots via compose_snapshots() with
**      COMPOSE_FLAG_COMPUTE_MERKLE
**   4. Asserts the composed snapshot has the expected summed counts
**      (nrule = 5, action_count = 9, nstate = 4) and a non-zero
**      merkle root
**   5. Repeats the composition with the SAME snapshots and asserts
**      the merkle root is byte-identical -- this is the determinism
**      property the merkle-cache architecture relies on
**   6. Composes sql_lite + xml (different second plugin) and asserts
**      the merkle root differs from step 4 -- the regression guard
**      against silent merkle drift
**
** This test is the load-bearing demonstration that composition works
** end-to-end at the runtime-plugin level.  The unit tests in
** tests/test_parser_composition.c verify compose_snapshots() against
** in-process synthetic snapshots; this test verifies it against
** snapshots produced by independently-loaded .so plugins, which is
** the actual production scenario for PostgreSQL extension distribution
** (DuckDB / EDB Oracle / pg_infer / pg_mentat / ... composed at
** PG startup).
**
** Args: argv[1] = sql_lite_plugin.so path, argv[2] = json_plugin.so,
**       argv[3] = xml_plugin.so.  Wired by tests/meson.build.
*/
#include "parser_manager.h"
#include "parser_composition.h"
#include "snapshot.h"
#include "merkle_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

/*
** Load a plugin by path, activate it (without a grammar file), and
** ask it to produce a snapshot.  Returns the snapshot with refcount
** == 1; the caller releases via snapshot_release().  On any error,
** logs to stderr and returns NULL.
*/
static ParserSnapshot *load_and_snapshot(ParserManager *mgr, const char *path,
                                         LimePluginHandle *handle_out) {
    LimePluginHandle h = LIME_PLUGIN_HANDLE_INVALID;
    ParserManagerStatus st = parser_manager_load(mgr, path, NULL, &h);
    if (st != PM_OK) {
        fprintf(stderr, "    load(%s): %s\n",
                path, parser_manager_status_string(st));
        return NULL;
    }

    /*
    ** Drive the snapshot directly through set_active+get_snapshot so
    ** the snapshot is registered with the manager (matches what a
    ** production host like PostgreSQL would do).  The plugins ignore
    ** their grammar_file argument and produce a synthetic snapshot,
    ** so any non-NULL filename works.
    */
    st = parser_manager_set_active(mgr, h, "fixture-grammar");
    if (st != PM_OK) {
        fprintf(stderr, "    set_active: %s\n",
                parser_manager_status_string(st));
        parser_manager_unload(mgr, h);
        return NULL;
    }

    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        fprintf(stderr, "    get_snapshot returned NULL\n");
        parser_manager_unload(mgr, h);
        return NULL;
    }

    if (handle_out) *handle_out = h;
    return snap;
}

/*
** Run compose_snapshots() over the given snapshot array with merkle
** computation enabled, copy the merkle root into out_hash, and return
** the composed snapshot.  Returns NULL on any error and zeroes out_hash.
*/
static ParserSnapshot *compose_and_hash(ParserSnapshot **snaps, uint32_t n,
                                        uint8_t out_hash[32]) {
    memset(out_hash, 0, 32);

    CompositionOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.flags = COMPOSE_FLAG_COMPUTE_MERKLE;

    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, n, &opts, &out, &diag);
    if (cr != COMPOSE_OK) {
        fprintf(stderr, "    compose_snapshots: %s\n",
                diag.error ? diag.error : "(no error message)");
        composition_diagnostics_destroy(&diag);
        return NULL;
    }

    if (out == NULL) {
        fprintf(stderr, "    compose_snapshots returned NULL output\n");
        composition_diagnostics_destroy(&diag);
        return NULL;
    }

    memcpy(out_hash, out->merkle_root, 32);
    composition_diagnostics_destroy(&diag);
    return out;
}

static bool hash_is_zero(const uint8_t hash[32]) {
    for (int i = 0; i < 32; i++) {
        if (hash[i] != 0) return false;
    }
    return true;
}

static void print_hash(const char *label, const uint8_t hash[32]) {
    char hex[65];
    merkle_hash_to_hex(hash, hex);
    printf("    %s = %s\n", label, hex);
}

/* ------------------------------------------------------------------ */
/*  Test: compose two runtime-loaded plugins                           */
/* ------------------------------------------------------------------ */

static int test_compose_two_plugins(const char *sql_path, const char *json_path,
                                    uint8_t out_sql_json_hash[32]) {
    TEST("compose_two_runtime_plugins");

    ParserManagerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    ParserManager *mgr = parser_manager_create(&cfg);
    if (!mgr) { FAIL("parser_manager_create"); return 1; }

    LimePluginHandle h_sql, h_json;
    ParserSnapshot *sql_snap = load_and_snapshot(mgr, sql_path, &h_sql);
    ParserSnapshot *json_snap = load_and_snapshot(mgr, json_path, &h_json);
    if (!sql_snap || !json_snap) {
        FAIL("plugin load + snapshot");
        if (sql_snap) snapshot_release(sql_snap);
        if (json_snap) snapshot_release(json_snap);
        parser_manager_destroy(mgr);
        return 1;
    }

    ParserSnapshot *snaps[2] = { sql_snap, json_snap };
    uint8_t hash[32];
    ParserSnapshot *composed = compose_and_hash(snaps, 2, hash);

    int rc = 1;
    if (!composed) {
        FAIL("compose_snapshots failed");
    } else if (composed->nrule != 5) {
        FAIL("composed nrule != 3+2=5");
    } else if (composed->action_count != 9) {
        FAIL("composed action_count != 5+4=9");
    } else if (composed->nstate != 4) {
        FAIL("composed nstate != 2+2=4");
    } else if (hash_is_zero(hash)) {
        FAIL("merkle root is all zero (COMPUTE_MERKLE not honored?)");
    } else {
        memcpy(out_sql_json_hash, hash, 32);
        PASS();
        print_hash("sql+json merkle", hash);
        rc = 0;
    }

    if (composed) snapshot_release(composed);
    snapshot_release(sql_snap);
    snapshot_release(json_snap);
    parser_manager_destroy(mgr);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test: re-composing identical snapshots yields identical merkle    */
/* ------------------------------------------------------------------ */

static int test_merkle_determinism(const char *sql_path, const char *json_path,
                                   const uint8_t reference_hash[32]) {
    TEST("merkle_determinism_across_recompose");

    ParserManagerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    ParserManager *mgr = parser_manager_create(&cfg);
    if (!mgr) { FAIL("parser_manager_create"); return 1; }

    LimePluginHandle h_sql, h_json;
    ParserSnapshot *sql_snap = load_and_snapshot(mgr, sql_path, &h_sql);
    ParserSnapshot *json_snap = load_and_snapshot(mgr, json_path, &h_json);
    if (!sql_snap || !json_snap) {
        FAIL("plugin load + snapshot");
        if (sql_snap) snapshot_release(sql_snap);
        if (json_snap) snapshot_release(json_snap);
        parser_manager_destroy(mgr);
        return 1;
    }

    ParserSnapshot *snaps[2] = { sql_snap, json_snap };
    uint8_t hash[32];
    ParserSnapshot *composed = compose_and_hash(snaps, 2, hash);

    int rc = 1;
    if (!composed) {
        FAIL("recompose failed");
    } else if (memcmp(hash, reference_hash, 32) != 0) {
        FAIL("merkle root differs across two runs of the same composition");
        print_hash("expected", reference_hash);
        print_hash("got     ", hash);
    } else {
        PASS();
        rc = 0;
    }

    if (composed) snapshot_release(composed);
    snapshot_release(sql_snap);
    snapshot_release(json_snap);
    parser_manager_destroy(mgr);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Test: swapping a plugin produces a different merkle root           */
/* ------------------------------------------------------------------ */

static int test_merkle_changes_on_input_swap(const char *sql_path,
                                             const char *xml_path,
                                             const uint8_t sql_json_hash[32]) {
    TEST("merkle_root_changes_when_input_set_changes");

    ParserManagerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    ParserManager *mgr = parser_manager_create(&cfg);
    if (!mgr) { FAIL("parser_manager_create"); return 1; }

    LimePluginHandle h_sql, h_xml;
    ParserSnapshot *sql_snap = load_and_snapshot(mgr, sql_path, &h_sql);
    ParserSnapshot *xml_snap = load_and_snapshot(mgr, xml_path, &h_xml);
    if (!sql_snap || !xml_snap) {
        FAIL("plugin load + snapshot");
        if (sql_snap) snapshot_release(sql_snap);
        if (xml_snap) snapshot_release(xml_snap);
        parser_manager_destroy(mgr);
        return 1;
    }

    ParserSnapshot *snaps[2] = { sql_snap, xml_snap };
    uint8_t hash[32];
    ParserSnapshot *composed = compose_and_hash(snaps, 2, hash);

    int rc = 1;
    if (!composed) {
        FAIL("compose sql+xml failed");
    } else if (memcmp(hash, sql_json_hash, 32) == 0) {
        FAIL("merkle root identical despite different inputs");
    } else if (hash_is_zero(hash)) {
        FAIL("sql+xml merkle root is zero");
    } else {
        /* Counts also differ: sql(5) + xml(6) = 11 actions. */
        if (composed->action_count != 11) {
            FAIL("composed action_count != 5+6=11");
        } else if (composed->nrule != 7) {
            FAIL("composed nrule != 3+4=7");
        } else {
            PASS();
            print_hash("sql+xml merkle", hash);
            rc = 0;
        }
    }

    if (composed) snapshot_release(composed);
    snapshot_release(sql_snap);
    snapshot_release(xml_snap);
    parser_manager_destroy(mgr);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <sql_lite_plugin.so> <json_plugin.so> <xml_plugin.so>\n",
                argv[0]);
        return 2;
    }

    const char *sql_path  = argv[1];
    const char *json_path = argv[2];
    const char *xml_path  = argv[3];

    printf("Composition e2e:\n");
    printf("  sql_lite plugin: %s\n", sql_path);
    printf("  json     plugin: %s\n", json_path);
    printf("  xml      plugin: %s\n", xml_path);

    uint8_t sql_json_hash[32];
    int rc = 0;

    rc |= test_compose_two_plugins(sql_path, json_path, sql_json_hash);
    if (rc == 0) {
        rc |= test_merkle_determinism(sql_path, json_path, sql_json_hash);
        rc |= test_merkle_changes_on_input_swap(sql_path, xml_path, sql_json_hash);
    }

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
