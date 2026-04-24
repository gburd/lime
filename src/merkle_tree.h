/*
** Merkle tree infrastructure for parser content hashing.
**
** Provides a cryptographic merkle tree built from SHA-256 hashes so that
** two parser grammars (symbols, rules, states, action tables) can be
** compared for structural equality in constant time by comparing root
** hashes.
**
** The tree is constructed bottom-up: leaf nodes hash raw data (e.g. a
** serialised symbol or rule), and internal nodes hash the concatenation
** of their children's hashes.  The 32-byte root hash uniquely identifies
** the parser content with overwhelming probability.
*/
#ifndef MERKLE_TREE_H
#define MERKLE_TREE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 digest length in bytes. */
#define MERKLE_HASH_SIZE 32

/* Maximum number of children an internal node may have.  Chosen to
** accommodate the four major grammar sections (symbols, rules, states,
** action tables) plus headroom for future extensions. */
#define MERKLE_MAX_CHILDREN 16

/* ------------------------------------------------------------------ */
/*  Merkle node                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    MERKLE_NODE_LEAF,
    MERKLE_NODE_INTERNAL
} MerkleNodeType;

typedef struct MerkleNode {
    MerkleNodeType type;

    /* SHA-256 hash for this node.  For a leaf this is SHA-256(data).
    ** For an internal node this is SHA-256(child[0].hash || child[1].hash
    ** || ... || child[n-1].hash). */
    uint8_t hash[MERKLE_HASH_SIZE];

    /* Human-readable label for debugging / serialisation (heap-allocated,
    ** owned by this node). May be NULL. */
    char *label;

    /* Internal-node fields. */
    struct MerkleNode **children;
    uint32_t nchildren;
} MerkleNode;

/* ------------------------------------------------------------------ */
/*  Merkle tree                                                        */
/* ------------------------------------------------------------------ */

typedef struct MerkleTree {
    MerkleNode *root;
} MerkleTree;

/* ------------------------------------------------------------------ */
/*  Low-level SHA-256 utility                                           */
/* ------------------------------------------------------------------ */

/*
** Compute the SHA-256 hash of *data* (length *len*) and store the
** 32-byte digest in *out*.
*/
void merkle_sha256(const void *data, size_t len, uint8_t out[MERKLE_HASH_SIZE]);

/* ------------------------------------------------------------------ */
/*  Node construction                                                   */
/* ------------------------------------------------------------------ */

/*
** Create a leaf node that hashes *data* (length *len*).  An optional
** *label* is deep-copied into the node.  Returns NULL on allocation
** failure.
*/
MerkleNode *merkle_create_leaf(const void *data, size_t len,
                               const char *label);

/*
** Create an internal node whose hash is computed from the concatenation
** of the hashes of *children[0..nchildren-1]*.  The children array is
** shallow-copied; ownership of each child node transfers to this node.
** An optional *label* is deep-copied.  Returns NULL on failure.
*/
MerkleNode *merkle_create_internal(MerkleNode **children, uint32_t nchildren,
                                   const char *label);

/* ------------------------------------------------------------------ */
/*  Hash computation                                                    */
/* ------------------------------------------------------------------ */

/*
** (Re)compute the hash for *node*.  For a leaf this is a no-op (the
** hash was set at creation time).  For an internal node the children's
** hashes are concatenated and SHA-256'd.  Recurses into children first.
*/
void merkle_compute_hash(MerkleNode *node);

/* ------------------------------------------------------------------ */
/*  Tree construction convenience                                       */
/* ------------------------------------------------------------------ */

/*
** Build a MerkleTree from an array of leaf nodes.  The leaves are
** paired bottom-up into internal nodes until a single root remains.
** *label* is applied to the root.  Returns NULL on failure.
**
** The caller still owns the leaf node pointers; they become children
** of the returned tree and are freed when the tree is freed.
*/
MerkleTree *merkle_build_tree(MerkleNode **leaves, uint32_t nleaves,
                              const char *root_label);

/* ------------------------------------------------------------------ */
/*  Verification and comparison                                         */
/* ------------------------------------------------------------------ */

/*
** Verify that every internal node's hash matches the SHA-256 of the
** concatenation of its children's hashes.  Returns true if the tree
** is consistent, false otherwise.
*/
bool merkle_verify_tree(const MerkleTree *tree);

/*
** Compare two trees by their root hashes.  Returns true if the root
** hashes are identical, false otherwise (or if either tree is NULL).
*/
bool merkle_trees_equal(const MerkleTree *a, const MerkleTree *b);

/* ------------------------------------------------------------------ */
/*  Serialization / deserialization                                     */
/* ------------------------------------------------------------------ */

/*
** Serialise *tree* into a malloc'd buffer.  The length is written to
** *out_len*.  Returns NULL on failure.
**
** Wire format (all integers little-endian):
**   4 bytes  magic   "MRK1"
**   4 bytes  node_count
**   For each node (post-order):
**     1 byte   type  (0 = leaf, 1 = internal)
**     2 bytes  label_len  (0 if no label)
**     label_len bytes  label (no NUL terminator)
**     32 bytes hash
**     If internal:
**       4 bytes nchildren
**       4*nchildren bytes  child indices (0-based post-order index)
*/
uint8_t *merkle_serialize(const MerkleTree *tree, size_t *out_len);

/*
** Deserialise a tree from *data* of length *len*.  Returns NULL on
** failure (invalid data, corrupt hashes, etc.).
*/
MerkleTree *merkle_deserialize(const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ */
/*  Cleanup                                                             */
/* ------------------------------------------------------------------ */

/*
** Free a single node and its label.  Does NOT recurse into children.
*/
void merkle_free_node(MerkleNode *node);

/*
** Free a tree and all nodes reachable from its root (depth-first).
*/
void merkle_free_tree(MerkleTree *tree);

/* ------------------------------------------------------------------ */
/*  Debug / display                                                     */
/* ------------------------------------------------------------------ */

/*
** Write a human-readable hex representation of a hash to *buf*.
** *buf* must be at least MERKLE_HASH_SIZE * 2 + 1 bytes.
*/
void merkle_hash_to_hex(const uint8_t hash[MERKLE_HASH_SIZE], char *buf);

#ifdef __cplusplus
}
#endif

#endif /* MERKLE_TREE_H */
