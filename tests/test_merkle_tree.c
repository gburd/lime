/*
** Comprehensive tests for the merkle tree infrastructure.
*/
#include "merkle_tree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Counters for pass/fail reporting. */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
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
/*  SHA-256 known-answer tests                                          */
/* ------------------------------------------------------------------ */

/*
** NIST test vectors for SHA-256.
*/
static void test_sha256_empty(void) {
    TEST(sha256_empty);
    uint8_t hash[MERKLE_HASH_SIZE];
    merkle_sha256("", 0, hash);

    /* SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    const uint8_t expected[MERKLE_HASH_SIZE] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    if (memcmp(hash, expected, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch for empty string");
    }
}

static void test_sha256_abc(void) {
    TEST(sha256_abc);
    uint8_t hash[MERKLE_HASH_SIZE];
    merkle_sha256("abc", 3, hash);

    /* SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    const uint8_t expected[MERKLE_HASH_SIZE] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };

    if (memcmp(hash, expected, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch for 'abc'");
    }
}

static void test_sha256_448bit(void) {
    TEST(sha256_448bit);
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t hash[MERKLE_HASH_SIZE];
    merkle_sha256(msg, strlen(msg), hash);

    /* SHA-256 of the 448-bit NIST test message. */
    const uint8_t expected[MERKLE_HASH_SIZE] = {
        0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
        0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
        0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
        0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1
    };

    if (memcmp(hash, expected, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch for 448-bit test vector");
    }
}

/* ------------------------------------------------------------------ */
/*  Leaf node tests                                                     */
/* ------------------------------------------------------------------ */

static void test_create_leaf_basic(void) {
    TEST(create_leaf_basic);
    const char *data = "hello";
    MerkleNode *leaf = merkle_create_leaf(data, strlen(data), "test-leaf");
    if (!leaf) { FAIL("allocation failed"); return; }

    if (leaf->type != MERKLE_NODE_LEAF) {
        FAIL("wrong type");
    } else if (leaf->nchildren != 0) {
        FAIL("leaf should have no children");
    } else if (!leaf->label || strcmp(leaf->label, "test-leaf") != 0) {
        FAIL("label mismatch");
    } else {
        /* Verify the hash matches a direct SHA-256("hello"). */
        uint8_t expected[MERKLE_HASH_SIZE];
        merkle_sha256(data, strlen(data), expected);
        if (memcmp(leaf->hash, expected, MERKLE_HASH_SIZE) == 0) {
            PASS();
        } else {
            FAIL("hash mismatch");
        }
    }
    merkle_free_node(leaf);
}

static void test_create_leaf_no_label(void) {
    TEST(create_leaf_no_label);
    MerkleNode *leaf = merkle_create_leaf("x", 1, NULL);
    if (!leaf) { FAIL("allocation failed"); return; }
    if (leaf->label != NULL) {
        FAIL("label should be NULL");
    } else {
        PASS();
    }
    merkle_free_node(leaf);
}

static void test_create_leaf_empty_data(void) {
    TEST(create_leaf_empty_data);
    MerkleNode *leaf = merkle_create_leaf("", 0, "empty");
    if (!leaf) { FAIL("allocation failed"); return; }

    uint8_t expected[MERKLE_HASH_SIZE];
    merkle_sha256("", 0, expected);
    if (memcmp(leaf->hash, expected, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("hash mismatch for empty data");
    }
    merkle_free_node(leaf);
}

static void test_leaf_determinism(void) {
    TEST(leaf_determinism);
    const char *data = "deterministic test data";
    MerkleNode *a = merkle_create_leaf(data, strlen(data), NULL);
    MerkleNode *b = merkle_create_leaf(data, strlen(data), NULL);
    if (!a || !b) { FAIL("allocation failed"); goto done; }

    if (memcmp(a->hash, b->hash, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("same data should produce same hash");
    }
done:
    merkle_free_node(a);
    merkle_free_node(b);
}

static void test_leaf_different_data(void) {
    TEST(leaf_different_data);
    MerkleNode *a = merkle_create_leaf("foo", 3, NULL);
    MerkleNode *b = merkle_create_leaf("bar", 3, NULL);
    if (!a || !b) { FAIL("allocation failed"); goto done; }

    if (memcmp(a->hash, b->hash, MERKLE_HASH_SIZE) != 0) {
        PASS();
    } else {
        FAIL("different data should produce different hashes");
    }
done:
    merkle_free_node(a);
    merkle_free_node(b);
}

/* ------------------------------------------------------------------ */
/*  Internal node tests                                                 */
/* ------------------------------------------------------------------ */

static void test_create_internal_basic(void) {
    TEST(create_internal_basic);
    MerkleNode *left  = merkle_create_leaf("left",  4, "L");
    MerkleNode *right = merkle_create_leaf("right", 5, "R");
    if (!left || !right) { FAIL("allocation failed"); goto done; }

    MerkleNode *children[2] = { left, right };
    MerkleNode *parent = merkle_create_internal(children, 2, "parent");
    if (!parent) { FAIL("internal creation failed"); goto done; }

    if (parent->type != MERKLE_NODE_INTERNAL) {
        FAIL("wrong type");
    } else if (parent->nchildren != 2) {
        FAIL("wrong child count");
    } else if (parent->children[0] != left || parent->children[1] != right) {
        FAIL("children not stored correctly");
    } else {
        /* Verify hash: SHA-256(left.hash || right.hash). */
        uint8_t concat[MERKLE_HASH_SIZE * 2];
        memcpy(concat, left->hash, MERKLE_HASH_SIZE);
        memcpy(concat + MERKLE_HASH_SIZE, right->hash, MERKLE_HASH_SIZE);
        uint8_t expected[MERKLE_HASH_SIZE];
        merkle_sha256(concat, sizeof(concat), expected);
        if (memcmp(parent->hash, expected, MERKLE_HASH_SIZE) == 0) {
            PASS();
        } else {
            FAIL("hash mismatch");
        }
    }

    /* free_node_recursive via the parent will free children too. */
    merkle_free_node(right);
    merkle_free_node(left);
    merkle_free_node(parent);
    return;

done:
    merkle_free_node(left);
    merkle_free_node(right);
}

static void test_internal_null_children(void) {
    TEST(internal_null_children);
    MerkleNode *n = merkle_create_internal(NULL, 0, "bad");
    if (n == NULL) {
        PASS();
    } else {
        FAIL("should fail with NULL children");
        merkle_free_node(n);
    }
}

static void test_internal_child_order_matters(void) {
    TEST(internal_child_order_matters);
    MerkleNode *a = merkle_create_leaf("A", 1, NULL);
    MerkleNode *b = merkle_create_leaf("B", 1, NULL);
    if (!a || !b) { FAIL("allocation failed"); goto done; }

    MerkleNode *ab_arr[2] = { a, b };
    MerkleNode *ba_arr[2] = { b, a };
    MerkleNode *ab = merkle_create_internal(ab_arr, 2, NULL);
    MerkleNode *ba = merkle_create_internal(ba_arr, 2, NULL);
    if (!ab || !ba) { FAIL("internal creation failed"); goto done2; }

    if (memcmp(ab->hash, ba->hash, MERKLE_HASH_SIZE) != 0) {
        PASS();
    } else {
        FAIL("child order should affect hash");
    }

done2:
    /* Free only the internal wrappers; children are shared. */
    if (ab) { free(ab->children); free(ab->label); free(ab); }
    if (ba) { free(ba->children); free(ba->label); free(ba); }
done:
    merkle_free_node(a);
    merkle_free_node(b);
}

/* ------------------------------------------------------------------ */
/*  Build tree tests                                                    */
/* ------------------------------------------------------------------ */

static void test_build_tree_single(void) {
    TEST(build_tree_single);
    MerkleNode *leaf = merkle_create_leaf("only", 4, "single");
    if (!leaf) { FAIL("allocation failed"); return; }

    MerkleNode *leaves[1] = { leaf };
    MerkleTree *tree = merkle_build_tree(leaves, 1, "root");
    if (!tree) { FAIL("tree build failed"); merkle_free_node(leaf); return; }

    if (tree->root == NULL) {
        FAIL("root is NULL");
    } else if (tree->root->type != MERKLE_NODE_INTERNAL) {
        FAIL("root should be internal even for single leaf");
    } else if (tree->root->nchildren != 1) {
        FAIL("single-leaf tree root should have 1 child");
    } else if (!tree->root->label || strcmp(tree->root->label, "root") != 0) {
        FAIL("root label mismatch");
    } else {
        PASS();
    }

    merkle_free_tree(tree);
}

static void test_build_tree_two(void) {
    TEST(build_tree_two);
    MerkleNode *a = merkle_create_leaf("aa", 2, NULL);
    MerkleNode *b = merkle_create_leaf("bb", 2, NULL);
    if (!a || !b) { FAIL("allocation failed"); return; }

    MerkleNode *leaves[2] = { a, b };
    MerkleTree *tree = merkle_build_tree(leaves, 2, "pair");
    if (!tree) { FAIL("tree build failed"); return; }

    if (tree->root->nchildren != 2) {
        FAIL("two-leaf tree root should have 2 children");
    } else if (merkle_verify_tree(tree)) {
        PASS();
    } else {
        FAIL("verification failed");
    }

    merkle_free_tree(tree);
}

static void test_build_tree_four(void) {
    TEST(build_tree_four);
    MerkleNode *leaves[4];
    const char *data[] = { "sym", "rul", "sta", "act" };
    for (int i = 0; i < 4; i++) {
        leaves[i] = merkle_create_leaf(data[i], strlen(data[i]), data[i]);
        if (!leaves[i]) { FAIL("allocation failed"); return; }
    }

    MerkleTree *tree = merkle_build_tree(leaves, 4, "grammar");
    if (!tree) { FAIL("tree build failed"); return; }

    /* A balanced tree of 4 leaves should have depth 3 (root -> 2 internals -> 4 leaves). */
    if (tree->root->nchildren != 2) {
        FAIL("root should have 2 children");
    } else if (!merkle_verify_tree(tree)) {
        FAIL("verification failed");
    } else {
        PASS();
    }

    merkle_free_tree(tree);
}

static void test_build_tree_odd(void) {
    TEST(build_tree_odd);
    /* 5 leaves: tests the odd-element promotion path. */
    MerkleNode *leaves[5];
    for (int i = 0; i < 5; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "n%d", i);
        leaves[i] = merkle_create_leaf(buf, strlen(buf), buf);
        if (!leaves[i]) { FAIL("allocation failed"); return; }
    }

    MerkleTree *tree = merkle_build_tree(leaves, 5, "odd-tree");
    if (!tree) { FAIL("tree build failed"); return; }

    if (merkle_verify_tree(tree)) {
        PASS();
    } else {
        FAIL("verification failed for odd-count tree");
    }

    merkle_free_tree(tree);
}

static void test_build_tree_null(void) {
    TEST(build_tree_null);
    MerkleTree *tree = merkle_build_tree(NULL, 0, "empty");
    if (tree == NULL) {
        PASS();
    } else {
        FAIL("should fail for empty input");
        merkle_free_tree(tree);
    }
}

/* ------------------------------------------------------------------ */
/*  Verification tests                                                  */
/* ------------------------------------------------------------------ */

static void test_verify_valid_tree(void) {
    TEST(verify_valid_tree);
    MerkleNode *leaves[3];
    for (int i = 0; i < 3; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        leaves[i] = merkle_create_leaf(buf, strlen(buf), NULL);
    }

    MerkleTree *tree = merkle_build_tree(leaves, 3, NULL);
    if (!tree) { FAIL("tree build failed"); return; }

    if (merkle_verify_tree(tree)) {
        PASS();
    } else {
        FAIL("valid tree should verify");
    }

    merkle_free_tree(tree);
}

static void test_verify_corrupted_tree(void) {
    TEST(verify_corrupted_tree);
    MerkleNode *a = merkle_create_leaf("x", 1, NULL);
    MerkleNode *b = merkle_create_leaf("y", 1, NULL);
    MerkleNode *leaves[2] = { a, b };
    MerkleTree *tree = merkle_build_tree(leaves, 2, NULL);
    if (!tree) { FAIL("tree build failed"); return; }

    /* Corrupt the root hash. */
    tree->root->hash[0] ^= 0xff;

    if (!merkle_verify_tree(tree)) {
        PASS();
    } else {
        FAIL("corrupted tree should not verify");
    }

    merkle_free_tree(tree);
}

static void test_verify_null(void) {
    TEST(verify_null);
    if (!merkle_verify_tree(NULL)) {
        PASS();
    } else {
        FAIL("NULL tree should not verify");
    }
}

/* ------------------------------------------------------------------ */
/*  Equality tests                                                      */
/* ------------------------------------------------------------------ */

static void test_trees_equal_same_data(void) {
    TEST(trees_equal_same_data);
    MerkleNode *la[2], *lb[2];
    la[0] = merkle_create_leaf("p", 1, NULL);
    la[1] = merkle_create_leaf("q", 1, NULL);
    lb[0] = merkle_create_leaf("p", 1, NULL);
    lb[1] = merkle_create_leaf("q", 1, NULL);

    MerkleTree *ta = merkle_build_tree(la, 2, NULL);
    MerkleTree *tb = merkle_build_tree(lb, 2, NULL);

    if (merkle_trees_equal(ta, tb)) {
        PASS();
    } else {
        FAIL("identical data should produce equal trees");
    }

    merkle_free_tree(ta);
    merkle_free_tree(tb);
}

static void test_trees_not_equal(void) {
    TEST(trees_not_equal);
    MerkleNode *la[2], *lb[2];
    la[0] = merkle_create_leaf("p", 1, NULL);
    la[1] = merkle_create_leaf("q", 1, NULL);
    lb[0] = merkle_create_leaf("p", 1, NULL);
    lb[1] = merkle_create_leaf("r", 1, NULL);

    MerkleTree *ta = merkle_build_tree(la, 2, NULL);
    MerkleTree *tb = merkle_build_tree(lb, 2, NULL);

    if (!merkle_trees_equal(ta, tb)) {
        PASS();
    } else {
        FAIL("different data should produce unequal trees");
    }

    merkle_free_tree(ta);
    merkle_free_tree(tb);
}

static void test_trees_equal_null(void) {
    TEST(trees_equal_null);
    MerkleNode *l = merkle_create_leaf("x", 1, NULL);
    MerkleNode *leaves[1] = { l };
    MerkleTree *t = merkle_build_tree(leaves, 1, NULL);

    if (!merkle_trees_equal(NULL, t) && !merkle_trees_equal(t, NULL) &&
        !merkle_trees_equal(NULL, NULL)) {
        PASS();
    } else {
        FAIL("NULL comparisons should return false");
    }

    merkle_free_tree(t);
}

/* ------------------------------------------------------------------ */
/*  Serialization round-trip tests                                      */
/* ------------------------------------------------------------------ */

static void test_serialize_roundtrip_simple(void) {
    TEST(serialize_roundtrip_simple);
    MerkleNode *leaf = merkle_create_leaf("roundtrip", 9, "data");
    MerkleNode *leaves[1] = { leaf };
    MerkleTree *tree = merkle_build_tree(leaves, 1, "root");
    if (!tree) { FAIL("tree build failed"); return; }

    size_t len = 0;
    uint8_t *buf = merkle_serialize(tree, &len);
    if (!buf) { FAIL("serialize failed"); merkle_free_tree(tree); return; }

    MerkleTree *restored = merkle_deserialize(buf, len);
    if (!restored) {
        FAIL("deserialize failed");
    } else if (!merkle_trees_equal(tree, restored)) {
        FAIL("round-trip produced different root hash");
    } else if (!merkle_verify_tree(restored)) {
        FAIL("restored tree verification failed");
    } else {
        PASS();
    }

    merkle_free_tree(restored);
    free(buf);
    merkle_free_tree(tree);
}

static void test_serialize_roundtrip_complex(void) {
    TEST(serialize_roundtrip_complex);
    MerkleNode *leaves[7];
    for (int i = 0; i < 7; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "node-%d", i);
        leaves[i] = merkle_create_leaf(buf, strlen(buf), buf);
        if (!leaves[i]) { FAIL("allocation failed"); return; }
    }

    MerkleTree *tree = merkle_build_tree(leaves, 7, "complex-root");
    if (!tree) { FAIL("tree build failed"); return; }

    size_t len = 0;
    uint8_t *buf = merkle_serialize(tree, &len);
    if (!buf) { FAIL("serialize failed"); merkle_free_tree(tree); return; }

    MerkleTree *restored = merkle_deserialize(buf, len);
    if (!restored) {
        FAIL("deserialize failed");
    } else if (!merkle_trees_equal(tree, restored)) {
        FAIL("round-trip hash mismatch");
    } else if (!merkle_verify_tree(restored)) {
        FAIL("restored tree verification failed");
    } else {
        PASS();
    }

    merkle_free_tree(restored);
    free(buf);
    merkle_free_tree(tree);
}

static void test_deserialize_bad_magic(void) {
    TEST(deserialize_bad_magic);
    uint8_t bad[] = { 'B', 'A', 'D', '!', 0, 0, 0, 1 };
    MerkleTree *t = merkle_deserialize(bad, sizeof(bad));
    if (t == NULL) {
        PASS();
    } else {
        FAIL("bad magic should fail");
        merkle_free_tree(t);
    }
}

static void test_deserialize_truncated(void) {
    TEST(deserialize_truncated);
    /* Valid magic but too short to contain any nodes. */
    uint8_t trunc[] = { 'M', 'R', 'K', '1', 1, 0, 0, 0 };
    MerkleTree *t = merkle_deserialize(trunc, sizeof(trunc));
    if (t == NULL) {
        PASS();
    } else {
        FAIL("truncated data should fail");
        merkle_free_tree(t);
    }
}

static void test_deserialize_null(void) {
    TEST(deserialize_null);
    if (merkle_deserialize(NULL, 0) == NULL) {
        PASS();
    } else {
        FAIL("NULL input should fail");
    }
}

static void test_serialize_null(void) {
    TEST(serialize_null);
    size_t len = 0;
    if (merkle_serialize(NULL, &len) == NULL) {
        PASS();
    } else {
        FAIL("NULL tree should fail serialization");
    }
}

/* ------------------------------------------------------------------ */
/*  Hash hex display test                                               */
/* ------------------------------------------------------------------ */

static void test_hash_to_hex(void) {
    TEST(hash_to_hex);
    uint8_t hash[MERKLE_HASH_SIZE];
    merkle_sha256("test", 4, hash);

    char hex[MERKLE_HASH_SIZE * 2 + 1];
    merkle_hash_to_hex(hash, hex);

    /* SHA-256("test") = 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08 */
    if (strcmp(hex, "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08") == 0) {
        PASS();
    } else {
        FAIL("hex representation mismatch");
        printf("    got: %s\n", hex);
    }
}

/* ------------------------------------------------------------------ */
/*  Large tree performance sanity check                                 */
/* ------------------------------------------------------------------ */

static void test_large_tree(void) {
    TEST(large_tree_1000_leaves);
    const uint32_t N = 1000;
    MerkleNode **leaves = calloc(N, sizeof(MerkleNode *));
    if (!leaves) { FAIL("allocation failed"); return; }

    for (uint32_t i = 0; i < N; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "leaf-%u", i);
        leaves[i] = merkle_create_leaf(buf, strlen(buf), NULL);
        if (!leaves[i]) {
            FAIL("leaf allocation failed");
            for (uint32_t j = 0; j < i; j++) merkle_free_node(leaves[j]);
            free(leaves);
            return;
        }
    }

    MerkleTree *tree = merkle_build_tree(leaves, N, "big");
    if (!tree) {
        FAIL("tree build failed");
        free(leaves);
        return;
    }

    if (merkle_verify_tree(tree)) {
        PASS();
    } else {
        FAIL("large tree verification failed");
    }

    merkle_free_tree(tree);
    free(leaves);
}

/* ------------------------------------------------------------------ */
/*  Compute hash (recomputation) test                                   */
/* ------------------------------------------------------------------ */

static void test_compute_hash_recompute(void) {
    TEST(compute_hash_recompute);
    MerkleNode *a = merkle_create_leaf("alpha", 5, NULL);
    MerkleNode *b = merkle_create_leaf("beta", 4, NULL);
    MerkleNode *children[2] = { a, b };
    MerkleNode *parent = merkle_create_internal(children, 2, NULL);
    if (!parent) { FAIL("creation failed"); return; }

    uint8_t original[MERKLE_HASH_SIZE];
    memcpy(original, parent->hash, MERKLE_HASH_SIZE);

    /* Recompute should yield the same hash. */
    merkle_compute_hash(parent);
    if (memcmp(parent->hash, original, MERKLE_HASH_SIZE) == 0) {
        PASS();
    } else {
        FAIL("recomputed hash differs from original");
    }

    merkle_free_node(a);
    merkle_free_node(b);
    merkle_free_node(parent);
}

/* ------------------------------------------------------------------ */
/*  Free safety tests                                                   */
/* ------------------------------------------------------------------ */

static void test_free_null(void) {
    TEST(free_null_safety);
    /* These should not crash. */
    merkle_free_node(NULL);
    merkle_free_tree(NULL);
    PASS();
}

/* ------------------------------------------------------------------ */
/*  Serialize roundtrip preserves labels                                */
/* ------------------------------------------------------------------ */

static void test_serialize_preserves_labels(void) {
    TEST(serialize_preserves_labels);
    MerkleNode *a = merkle_create_leaf("data-a", 6, "label-a");
    MerkleNode *b = merkle_create_leaf("data-b", 6, "label-b");
    MerkleNode *leaves[2] = { a, b };
    MerkleTree *tree = merkle_build_tree(leaves, 2, "root-label");
    if (!tree) { FAIL("tree build failed"); return; }

    size_t len = 0;
    uint8_t *buf = merkle_serialize(tree, &len);
    if (!buf) { FAIL("serialize failed"); merkle_free_tree(tree); return; }

    MerkleTree *restored = merkle_deserialize(buf, len);
    if (!restored) {
        FAIL("deserialize failed");
    } else if (!restored->root->label || strcmp(restored->root->label, "root-label") != 0) {
        FAIL("root label not preserved");
    } else {
        PASS();
    }

    merkle_free_tree(restored);
    free(buf);
    merkle_free_tree(tree);
}

/* ================================================================== */
/*  Main                                                                */
/* ================================================================== */

int main(void) {
    printf("Merkle tree tests:\n");

    /* SHA-256 */
    test_sha256_empty();
    test_sha256_abc();
    test_sha256_448bit();

    /* Leaf nodes */
    test_create_leaf_basic();
    test_create_leaf_no_label();
    test_create_leaf_empty_data();
    test_leaf_determinism();
    test_leaf_different_data();

    /* Internal nodes */
    test_create_internal_basic();
    test_internal_null_children();
    test_internal_child_order_matters();

    /* Tree building */
    test_build_tree_single();
    test_build_tree_two();
    test_build_tree_four();
    test_build_tree_odd();
    test_build_tree_null();

    /* Verification */
    test_verify_valid_tree();
    test_verify_corrupted_tree();
    test_verify_null();

    /* Equality */
    test_trees_equal_same_data();
    test_trees_not_equal();
    test_trees_equal_null();

    /* Serialization */
    test_serialize_roundtrip_simple();
    test_serialize_roundtrip_complex();
    test_deserialize_bad_magic();
    test_deserialize_truncated();
    test_deserialize_null();
    test_serialize_null();
    test_serialize_preserves_labels();

    /* Hash display */
    test_hash_to_hex();

    /* Recomputation */
    test_compute_hash_recompute();

    /* Null safety */
    test_free_null();

    /* Large tree */
    test_large_tree();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
