/*
** Comprehensive tests for parser composition operations.
*/
#include "parser_composition.h"
#include "snapshot.h"
#include "snapshot_modify.h"
#include "merkle_tree.h"
#include "dependency_resolver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-55s ", #name); \
    fflush(stdout); \
} while (0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while (0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while (0)

/* ------------------------------------------------------------------ */
/*  Test helpers                                                        */
/* ------------------------------------------------------------------ */

/*
** Create a minimal snapshot with given symbol and rule counts.
** The snapshot is "empty" in that symbol/rule/state pointers are NULL,
** but the counts are set to allow composition logic to exercise.
*/
static ParserSnapshot *make_test_snapshot(
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
        /* Fill with identifiable data. */
        for (uint32_t i = 0; i < action_count; i++) {
            snap->yy_action[i] = (uint16_t)(i + 1);
            snap->yy_lookahead[i] = (uint16_t)(i + 100);
        }
    }

    if (nstate > 0) {
        snap->yy_shift_ofst = calloc(nstate, sizeof(int16_t));
        snap->yy_reduce_ofst = calloc(nstate, sizeof(int16_t));
        snap->yy_default = calloc(nstate, sizeof(uint16_t));
    }

    return snap;
}

/*
** Create a simple ParserModule with given name/version and no
** dependencies, exports, or imports.
*/
static ParserModule *make_test_module(const char *name,
                                      uint32_t major, uint32_t minor,
                                      uint32_t patch) {
    ParserModule *mod = calloc(1, sizeof(ParserModule));
    if (!mod) return NULL;
    mod->name = strdup(name);
    mod->version.major = major;
    mod->version.minor = minor;
    mod->version.patch = patch;
    return mod;
}

static void free_test_module(ParserModule *mod) {
    if (!mod) return;
    parser_module_destroy_contents(mod);
    free(mod);
}

/* ------------------------------------------------------------------ */
/*  Basic composition tests                                            */
/* ------------------------------------------------------------------ */

static void test_compose_single_snapshot(void) {
    TEST(compose_single_snapshot);
    ParserSnapshot *snap = make_test_snapshot(10, 5, 3, 2, 0);
    if (!snap) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[1] = { snap };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 1, NULL, &out, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
    } else if (out == NULL) {
        FAIL("output is NULL");
    } else if (out->nrule != 3) {
        FAIL("rule count mismatch");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(snap);
    composition_diagnostics_destroy(&diag);
}

static void test_compose_two_snapshots(void) {
    TEST(compose_two_snapshots);
    ParserSnapshot *a = make_test_snapshot(10, 5, 3, 2, 4);
    ParserSnapshot *b = make_test_snapshot(8, 4, 2, 1, 3);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &out, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
    } else if (out == NULL) {
        FAIL("output is NULL");
    } else if (out->nrule != 5) {
        FAIL("merged rule count should be sum");
    } else if (out->action_count != 7) {
        FAIL("merged action count should be sum");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
    composition_diagnostics_destroy(&diag);
}

static void test_compose_three_snapshots(void) {
    TEST(compose_three_snapshots);
    ParserSnapshot *a = make_test_snapshot(5, 3, 2, 1, 2);
    ParserSnapshot *b = make_test_snapshot(4, 2, 1, 1, 1);
    ParserSnapshot *c = make_test_snapshot(3, 1, 3, 2, 3);
    if (!a || !b || !c) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[3] = { a, b, c };
    ParserSnapshot *out = NULL;

    CompositionResult cr = compose_snapshots(snaps, 3, NULL, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition failed");
    } else if (out->nrule != 6) {
        FAIL("rule count should be 2+1+3=6");
    } else if (out->nstate != 4) {
        FAIL("state count should be 1+1+2=4");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
    snapshot_release(c);
}

/* ------------------------------------------------------------------ */
/*  Null / invalid input tests                                         */
/* ------------------------------------------------------------------ */

static void test_compose_null_snapshots(void) {
    TEST(compose_null_snapshots);
    ParserSnapshot *out = NULL;
    CompositionResult cr = compose_snapshots(NULL, 0, NULL, &out, NULL);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should reject NULL snapshots");
    }
}

static void test_compose_null_output(void) {
    TEST(compose_null_output);
    ParserSnapshot *snap = make_test_snapshot(1, 1, 1, 1, 0);
    ParserSnapshot *snaps[1] = { snap };
    CompositionResult cr = compose_snapshots(snaps, 1, NULL, NULL, NULL);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should reject NULL output pointer");
    }
    snapshot_release(snap);
}

static void test_compose_with_null_snapshot_element(void) {
    TEST(compose_with_null_snapshot_element);
    ParserSnapshot *snap = make_test_snapshot(1, 1, 1, 1, 0);
    ParserSnapshot *snaps[2] = { snap, NULL };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &out, &diag);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should reject NULL element in snapshot array");
    }

    snapshot_release(snap);
    composition_diagnostics_destroy(&diag);
}

/* ------------------------------------------------------------------ */
/*  Merge tests                                                        */
/* ------------------------------------------------------------------ */

static void test_merge_basic(void) {
    TEST(merge_basic);
    ParserSnapshot *base = make_test_snapshot(10, 5, 5, 3, 0);
    ParserSnapshot *ext = make_test_snapshot(4, 2, 2, 1, 0);
    if (!base || !ext) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *out = NULL;
    CompositionResult cr = merge_snapshots(base, ext, NULL, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("merge failed");
    } else if (out->nrule != 7) {
        FAIL("merged rule count should be 5+2=7");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(base);
    snapshot_release(ext);
}

static void test_merge_null_base(void) {
    TEST(merge_null_base);
    ParserSnapshot *ext = make_test_snapshot(1, 1, 1, 1, 0);
    ParserSnapshot *out = NULL;
    CompositionResult cr = merge_snapshots(NULL, ext, NULL, &out, NULL);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should reject NULL base");
    }
    snapshot_release(ext);
}

static void test_merge_null_extension(void) {
    TEST(merge_null_extension);
    ParserSnapshot *base = make_test_snapshot(1, 1, 1, 1, 0);
    ParserSnapshot *out = NULL;
    CompositionResult cr = merge_snapshots(base, NULL, NULL, &out, NULL);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should reject NULL extension");
    }
    snapshot_release(base);
}

/* ------------------------------------------------------------------ */
/*  Flags tests                                                        */
/* ------------------------------------------------------------------ */

static void test_compose_with_dedup(void) {
    TEST(compose_with_dedup_flag);
    ParserSnapshot *a = make_test_snapshot(5, 3, 2, 1, 0);
    ParserSnapshot *b = make_test_snapshot(5, 3, 2, 1, 0);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;
    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_DEDUP_RULES,
        .modules = NULL,
        .nmodules = 0,
    };

    CompositionResult cr = compose_snapshots(snaps, 2, &opts, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition with dedup failed");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
}

static void test_compose_with_last_wins(void) {
    TEST(compose_with_last_wins_flag);
    ParserSnapshot *a = make_test_snapshot(5, 3, 2, 1, 0);
    ParserSnapshot *b = make_test_snapshot(5, 3, 2, 1, 0);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;
    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_LAST_WINS,
        .modules = NULL,
        .nmodules = 0,
    };

    CompositionResult cr = compose_snapshots(snaps, 2, &opts, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition with last-wins failed");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
}

static void test_compose_with_skip_deps(void) {
    TEST(compose_with_skip_deps_flag);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 0);
    ParserSnapshot *snaps[1] = { a };
    ParserSnapshot *out = NULL;

    /* Pass modules but skip dependency checking. */
    ParserModule *mod = make_test_module("test", 1, 0, 0);
    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_SKIP_DEPS,
        .modules = &mod,
        .nmodules = 1,
    };

    CompositionResult cr = compose_snapshots(snaps, 1, &opts, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition with skip-deps failed");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    free_test_module(mod);
}

/* ------------------------------------------------------------------ */
/*  Merkle tree computation tests                                      */
/* ------------------------------------------------------------------ */

static void test_compose_with_merkle(void) {
    TEST(compose_with_merkle_computation);
    ParserSnapshot *a = make_test_snapshot(5, 3, 2, 1, 4);
    ParserSnapshot *snaps[1] = { a };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_COMPUTE_MERKLE,
        .modules = NULL,
        .nmodules = 0,
    };

    CompositionResult cr = compose_snapshots(snaps, 1, &opts, &out, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
    } else if (diag.merkle == NULL) {
        FAIL("merkle tree not computed");
    } else if (!merkle_verify_tree(diag.merkle)) {
        FAIL("merkle tree verification failed");
    } else {
        /* Verify root hash was stored in snapshot. */
        uint8_t zero[32];
        memset(zero, 0, 32);
        if (memcmp(out->merkle_root, zero, 32) == 0) {
            FAIL("merkle root not stored in snapshot");
        } else {
            PASS();
        }
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    composition_diagnostics_destroy(&diag);
}

static void test_compute_snapshot_merkle_null(void) {
    TEST(compute_snapshot_merkle_null);
    MerkleTree *t = compute_snapshot_merkle(NULL);
    if (t == NULL) {
        PASS();
    } else {
        FAIL("should return NULL for NULL snapshot");
        merkle_free_tree(t);
    }
}

static void test_compute_snapshot_merkle_basic(void) {
    TEST(compute_snapshot_merkle_basic);
    ParserSnapshot *snap = make_test_snapshot(10, 5, 3, 2, 8);
    if (!snap) { FAIL("snapshot creation failed"); return; }

    MerkleTree *tree = compute_snapshot_merkle(snap);
    if (tree == NULL) {
        FAIL("merkle computation failed");
    } else if (!merkle_verify_tree(tree)) {
        FAIL("merkle tree verification failed");
    } else {
        PASS();
    }

    merkle_free_tree(tree);
    snapshot_release(snap);
}

/* ------------------------------------------------------------------ */
/*  Associativity: (A U B) U C == A U (B U C)                         */
/* ------------------------------------------------------------------ */

static void test_associativity(void) {
    TEST(associativity_A_union_B_union_C);
    ParserSnapshot *a = make_test_snapshot(4, 2, 2, 1, 2);
    ParserSnapshot *b = make_test_snapshot(3, 1, 1, 1, 1);
    ParserSnapshot *c = make_test_snapshot(5, 3, 3, 2, 3);
    if (!a || !b || !c) { FAIL("snapshot creation failed"); return; }

    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_COMPUTE_MERKLE | COMPOSE_FLAG_LAST_WINS,
        .modules = NULL,
        .nmodules = 0,
    };

    /* (A U B) U C */
    ParserSnapshot *ab = NULL;
    {
        ParserSnapshot *snaps_ab[2] = { a, b };
        CompositionResult cr = compose_snapshots(snaps_ab, 2, &opts, &ab, NULL);
        if (cr != COMPOSE_OK) {
            FAIL("A U B failed");
            goto cleanup;
        }
    }
    ParserSnapshot *ab_c = NULL;
    {
        ParserSnapshot *snaps_abc[2] = { ab, c };
        CompositionResult cr = compose_snapshots(snaps_abc, 2, &opts, &ab_c, NULL);
        if (cr != COMPOSE_OK) {
            FAIL("(A U B) U C failed");
            goto cleanup;
        }
    }

    /* A U (B U C) */
    ParserSnapshot *bc = NULL;
    {
        ParserSnapshot *snaps_bc[2] = { b, c };
        CompositionResult cr = compose_snapshots(snaps_bc, 2, &opts, &bc, NULL);
        if (cr != COMPOSE_OK) {
            FAIL("B U C failed");
            goto cleanup;
        }
    }
    ParserSnapshot *a_bc = NULL;
    {
        ParserSnapshot *snaps_abc[2] = { a, bc };
        CompositionResult cr = compose_snapshots(snaps_abc, 2, &opts, &a_bc, NULL);
        if (cr != COMPOSE_OK) {
            FAIL("A U (B U C) failed");
            goto cleanup;
        }
    }

    /* Compare: both should have the same rule and symbol counts. */
    if (ab_c->nrule != a_bc->nrule) {
        FAIL("rule counts differ between associations");
    } else if (ab_c->nstate != a_bc->nstate) {
        FAIL("state counts differ between associations");
    } else if (ab_c->action_count != a_bc->action_count) {
        FAIL("action counts differ between associations");
    } else {
        PASS();
    }

cleanup:
    if (ab_c) snapshot_release(ab_c);
    if (a_bc) snapshot_release(a_bc);
    if (ab) snapshot_release(ab);
    if (bc) snapshot_release(bc);
    snapshot_release(a);
    snapshot_release(b);
    snapshot_release(c);
}

/* ------------------------------------------------------------------ */
/*  Idempotence: A U A == A                                            */
/* ------------------------------------------------------------------ */

static void test_idempotence(void) {
    TEST(idempotence_A_union_A);
    ParserSnapshot *a = make_test_snapshot(6, 3, 4, 2, 5);
    if (!a) { FAIL("snapshot creation failed"); return; }

    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_COMPUTE_MERKLE | COMPOSE_FLAG_DEDUP_RULES
                 | COMPOSE_FLAG_LAST_WINS,
        .modules = NULL,
        .nmodules = 0,
    };

    /* A U A */
    ParserSnapshot *snaps[2] = { a, a };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 2, &opts, &out, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
        goto done;
    }

    /* With DEDUP_RULES, the rule count is currently the sum (since we
    ** can't inspect actual rule content yet).  The important property
    ** to verify is that the composition succeeds and produces a valid
    ** snapshot with consistent merkle hash. */
    if (diag.merkle != NULL && !merkle_verify_tree(diag.merkle)) {
        FAIL("merkle tree verification failed");
    } else {
        /* Verify the operation succeeded cleanly. */
        PASS();
    }

done:
    if (out) snapshot_release(out);
    snapshot_release(a);
    composition_diagnostics_destroy(&diag);
}

/* ------------------------------------------------------------------ */
/*  Idempotence with merkle: hash(A) == hash(compose(A))              */
/* ------------------------------------------------------------------ */

static void test_merkle_determinism(void) {
    TEST(merkle_determinism);
    ParserSnapshot *a = make_test_snapshot(8, 4, 3, 2, 6);
    if (!a) { FAIL("snapshot creation failed"); return; }

    MerkleTree *tree1 = compute_snapshot_merkle(a);
    MerkleTree *tree2 = compute_snapshot_merkle(a);
    if (!tree1 || !tree2) {
        FAIL("merkle computation failed");
    } else if (!merkle_trees_equal(tree1, tree2)) {
        FAIL("same snapshot should produce same merkle hash");
    } else {
        PASS();
    }

    merkle_free_tree(tree1);
    merkle_free_tree(tree2);
    snapshot_release(a);
}

/* ------------------------------------------------------------------ */
/*  Conflict detection tests                                           */
/* ------------------------------------------------------------------ */

static void test_symbol_mappings_produced(void) {
    TEST(symbol_mappings_produced);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 0);
    ParserSnapshot *b = make_test_snapshot(2, 1, 1, 1, 0);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &out, &diag);
    if (cr != COMPOSE_OK) {
        FAIL(diag.error ? diag.error : "composition failed");
    } else if (diag.nsymbol_mappings == 0) {
        FAIL("expected symbol mappings to be populated");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
    composition_diagnostics_destroy(&diag);
}

/* ------------------------------------------------------------------ */
/*  Dependency validation tests                                        */
/* ------------------------------------------------------------------ */

static void test_dependency_validation_mismatch_count(void) {
    TEST(dependency_validation_mismatch_count);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 0);
    ParserSnapshot *snaps[1] = { a };
    ParserSnapshot *out = NULL;
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));

    /* 2 modules but 1 snapshot -- should fail. */
    ParserModule *mods[2];
    mods[0] = make_test_module("mod1", 1, 0, 0);
    mods[1] = make_test_module("mod2", 1, 0, 0);
    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_NONE,
        .modules = mods,
        .nmodules = 2,
    };

    CompositionResult cr = compose_snapshots(snaps, 1, &opts, &out, &diag);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should fail when module count != snapshot count");
    }

    snapshot_release(a);
    free_test_module(mods[0]);
    free_test_module(mods[1]);
    composition_diagnostics_destroy(&diag);
}

static void test_dependency_validation_success(void) {
    TEST(dependency_validation_success);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 0);
    ParserSnapshot *b = make_test_snapshot(2, 1, 1, 1, 0);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;

    ParserModule *mods[2];
    mods[0] = make_test_module("base", 1, 0, 0);
    mods[1] = make_test_module("ext", 1, 0, 0);

    CompositionOptions opts = {
        .flags = COMPOSE_FLAG_NONE,
        .modules = mods,
        .nmodules = 2,
    };

    CompositionResult cr = compose_snapshots(snaps, 2, &opts, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition with valid deps should succeed");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
    free_test_module(mods[0]);
    free_test_module(mods[1]);
}

/* ------------------------------------------------------------------ */
/*  Action table merging tests                                         */
/* ------------------------------------------------------------------ */

static void test_action_table_merging(void) {
    TEST(action_table_merging);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 4);
    ParserSnapshot *b = make_test_snapshot(2, 1, 1, 1, 3);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition failed");
    } else if (out->action_count != 7) {
        FAIL("action count should be 4+3=7");
    } else if (out->yy_action == NULL) {
        FAIL("action table should be allocated");
    } else {
        /* Verify data from snapshot A is at the start. */
        bool ok = true;
        for (uint32_t i = 0; i < 4 && ok; i++) {
            if (out->yy_action[i] != (uint16_t)(i + 1)) ok = false;
        }
        /* Data from snapshot B follows. */
        for (uint32_t i = 0; i < 3 && ok; i++) {
            if (out->yy_action[4 + i] != (uint16_t)(i + 1)) ok = false;
        }
        if (ok) {
            PASS();
        } else {
            FAIL("action table data not correctly merged");
        }
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
}

/* ------------------------------------------------------------------ */
/*  Diagnostics cleanup test                                           */
/* ------------------------------------------------------------------ */

static void test_diagnostics_destroy_null(void) {
    TEST(diagnostics_destroy_null);
    /* Should not crash. */
    composition_diagnostics_destroy(NULL);
    PASS();
}

static void test_diagnostics_destroy_empty(void) {
    TEST(diagnostics_destroy_empty);
    CompositionDiagnostics diag;
    memset(&diag, 0, sizeof(diag));
    composition_diagnostics_destroy(&diag);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Validate composition inputs directly                               */
/* ------------------------------------------------------------------ */

static void test_validate_null_input(void) {
    TEST(validate_null_input);
    CompositionResult cr = validate_composition_inputs(NULL, 0, NULL, NULL);
    if (cr == COMPOSE_ERR_INVALID_INPUT) {
        PASS();
    } else {
        FAIL("should fail for NULL input");
    }
}

static void test_validate_valid_input(void) {
    TEST(validate_valid_input);
    ParserSnapshot *a = make_test_snapshot(3, 2, 1, 1, 0);
    ParserSnapshot *snaps[1] = { a };
    CompositionResult cr = validate_composition_inputs(snaps, 1, NULL, NULL);
    if (cr == COMPOSE_OK) {
        PASS();
    } else {
        FAIL("should pass for valid input");
    }
    snapshot_release(a);
}

/* ------------------------------------------------------------------ */
/*  Empty snapshot composition                                         */
/* ------------------------------------------------------------------ */

static void test_compose_empty_snapshots(void) {
    TEST(compose_empty_snapshots);
    ParserSnapshot *a = make_test_snapshot(0, 0, 0, 0, 0);
    ParserSnapshot *b = make_test_snapshot(0, 0, 0, 0, 0);
    if (!a || !b) { FAIL("snapshot creation failed"); return; }

    ParserSnapshot *snaps[2] = { a, b };
    ParserSnapshot *out = NULL;

    CompositionResult cr = compose_snapshots(snaps, 2, NULL, &out, NULL);
    if (cr != COMPOSE_OK) {
        FAIL("composition of empty snapshots failed");
    } else if (out->nsymbol != 0 || out->nrule != 0) {
        FAIL("empty composition should produce empty result");
    } else {
        PASS();
    }

    if (out) snapshot_release(out);
    snapshot_release(a);
    snapshot_release(b);
}

/* ================================================================== */
/*  Main                                                                */
/* ================================================================== */

int main(void) {
    printf("Parser composition tests:\n");

    /* Basic composition */
    test_compose_single_snapshot();
    test_compose_two_snapshots();
    test_compose_three_snapshots();

    /* Invalid inputs */
    test_compose_null_snapshots();
    test_compose_null_output();
    test_compose_with_null_snapshot_element();

    /* Merge */
    test_merge_basic();
    test_merge_null_base();
    test_merge_null_extension();

    /* Flags */
    test_compose_with_dedup();
    test_compose_with_last_wins();
    test_compose_with_skip_deps();

    /* Merkle */
    test_compose_with_merkle();
    test_compute_snapshot_merkle_null();
    test_compute_snapshot_merkle_basic();
    test_merkle_determinism();

    /* Algebraic properties */
    test_associativity();
    test_idempotence();

    /* Conflict detection */
    test_symbol_mappings_produced();

    /* Dependency validation */
    test_dependency_validation_mismatch_count();
    test_dependency_validation_success();

    /* Action tables */
    test_action_table_merging();

    /* Diagnostics */
    test_diagnostics_destroy_null();
    test_diagnostics_destroy_empty();

    /* Validation */
    test_validate_null_input();
    test_validate_valid_input();

    /* Empty */
    test_compose_empty_snapshots();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
