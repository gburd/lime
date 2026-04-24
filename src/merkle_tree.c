/*
** Merkle tree infrastructure for parser content hashing.
**
** Contains an embedded, portable SHA-256 implementation derived from
** public domain code by Brad Conte (brad@bradconte.com).  No external
** crypto library dependency is required.
*/
#include "merkle_tree.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*  Embedded SHA-256 (public domain, Brad Conte)                       */
/* ================================================================== */

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4    ] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] <<  8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen  = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;

    /* Pad the message. */
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    /* Append the total bit length (big-endian 64-bit). */
    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    /* Produce the final hash value (big-endian). */
    for (i = 0; i < 4; i++) {
        hash[i]      = (uint8_t)(ctx->state[0] >> (24 - i * 8));
        hash[i + 4]  = (uint8_t)(ctx->state[1] >> (24 - i * 8));
        hash[i + 8]  = (uint8_t)(ctx->state[2] >> (24 - i * 8));
        hash[i + 12] = (uint8_t)(ctx->state[3] >> (24 - i * 8));
        hash[i + 16] = (uint8_t)(ctx->state[4] >> (24 - i * 8));
        hash[i + 20] = (uint8_t)(ctx->state[5] >> (24 - i * 8));
        hash[i + 24] = (uint8_t)(ctx->state[6] >> (24 - i * 8));
        hash[i + 28] = (uint8_t)(ctx->state[7] >> (24 - i * 8));
    }
}

/* ================================================================== */
/*  Public SHA-256 wrapper                                              */
/* ================================================================== */

void merkle_sha256(const void *data, size_t len, uint8_t out[MERKLE_HASH_SIZE]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)data, len);
    sha256_final(&ctx, out);
}

/* ================================================================== */
/*  Node construction                                                   */
/* ================================================================== */

MerkleNode *merkle_create_leaf(const void *data, size_t len,
                               const char *label) {
    MerkleNode *node = calloc(1, sizeof(*node));
    if (!node) return NULL;

    node->type = MERKLE_NODE_LEAF;
    node->children = NULL;
    node->nchildren = 0;

    if (label) {
        node->label = strdup(label);
        if (!node->label) {
            free(node);
            return NULL;
        }
    }

    merkle_sha256(data, len, node->hash);
    return node;
}

MerkleNode *merkle_create_internal(MerkleNode **children, uint32_t nchildren,
                                   const char *label) {
    if (!children || nchildren == 0) return NULL;

    MerkleNode *node = calloc(1, sizeof(*node));
    if (!node) return NULL;

    node->type = MERKLE_NODE_INTERNAL;
    node->nchildren = nchildren;
    node->children = calloc(nchildren, sizeof(MerkleNode *));
    if (!node->children) {
        free(node);
        return NULL;
    }
    memcpy(node->children, children, nchildren * sizeof(MerkleNode *));

    if (label) {
        node->label = strdup(label);
        if (!node->label) {
            free(node->children);
            free(node);
            return NULL;
        }
    }

    /* Compute hash from children. */
    merkle_compute_hash(node);
    return node;
}

/* ================================================================== */
/*  Hash computation                                                    */
/* ================================================================== */

void merkle_compute_hash(MerkleNode *node) {
    if (!node) return;

    if (node->type == MERKLE_NODE_LEAF) {
        /* Leaf hash was set at creation time; nothing to recompute
        ** unless the caller re-feeds data (not supported). */
        return;
    }

    /* Recurse into children first. */
    for (uint32_t i = 0; i < node->nchildren; i++) {
        merkle_compute_hash(node->children[i]);
    }

    /* Concatenate all children's hashes and SHA-256 the result. */
    size_t concat_len = (size_t)node->nchildren * MERKLE_HASH_SIZE;
    uint8_t *concat = malloc(concat_len);
    if (!concat) return;

    for (uint32_t i = 0; i < node->nchildren; i++) {
        memcpy(concat + (size_t)i * MERKLE_HASH_SIZE,
               node->children[i]->hash, MERKLE_HASH_SIZE);
    }

    merkle_sha256(concat, concat_len, node->hash);
    free(concat);
}

/* ================================================================== */
/*  Tree construction                                                   */
/* ================================================================== */

MerkleTree *merkle_build_tree(MerkleNode **leaves, uint32_t nleaves,
                              const char *root_label) {
    if (!leaves || nleaves == 0) return NULL;

    MerkleTree *tree = calloc(1, sizeof(*tree));
    if (!tree) return NULL;

    if (nleaves == 1) {
        /* Single leaf becomes the root (wrap in an internal node so
        ** the tree always has a proper root). */
        tree->root = merkle_create_internal(leaves, 1, root_label);
        if (!tree->root) {
            free(tree);
            return NULL;
        }
        return tree;
    }

    /* Bottom-up pairing.  We work through levels: at each level we
    ** pair adjacent nodes into internal nodes until one remains. */
    uint32_t count = nleaves;

    /* Working array (we need to mutate it). */
    MerkleNode **current = calloc(count, sizeof(MerkleNode *));
    if (!current) {
        free(tree);
        return NULL;
    }
    memcpy(current, leaves, count * sizeof(MerkleNode *));

    while (count > 1) {
        uint32_t next_count = (count + 1) / 2;
        MerkleNode **next = calloc(next_count, sizeof(MerkleNode *));
        if (!next) {
            free(current);
            free(tree);
            return NULL;
        }

        for (uint32_t i = 0; i < count; i += 2) {
            if (i + 1 < count) {
                MerkleNode *pair[2] = { current[i], current[i + 1] };
                next[i / 2] = merkle_create_internal(pair, 2, NULL);
            } else {
                /* Odd one out: promote directly. */
                MerkleNode *single[1] = { current[i] };
                next[i / 2] = merkle_create_internal(single, 1, NULL);
            }
            if (!next[i / 2]) {
                /* Cleanup on failure: free the internal nodes we
                ** already created at this level. */
                for (uint32_t j = 0; j < i / 2; j++) {
                    /* Only free the internal wrapper, not children. */
                    free(next[j]->children);
                    free(next[j]->label);
                    free(next[j]);
                }
                free(next);
                free(current);
                free(tree);
                return NULL;
            }
        }

        free(current);
        current = next;
        count = next_count;
    }

    /* Apply root label. */
    free(current[0]->label);
    current[0]->label = root_label ? strdup(root_label) : NULL;

    tree->root = current[0];
    free(current);
    return tree;
}

/* ================================================================== */
/*  Verification and comparison                                         */
/* ================================================================== */

static bool verify_node(const MerkleNode *node) {
    if (!node) return false;

    if (node->type == MERKLE_NODE_LEAF) return true;

    /* Verify children recursively. */
    for (uint32_t i = 0; i < node->nchildren; i++) {
        if (!verify_node(node->children[i])) return false;
    }

    /* Recompute hash and compare. */
    size_t concat_len = (size_t)node->nchildren * MERKLE_HASH_SIZE;
    uint8_t *concat = malloc(concat_len);
    if (!concat) return false;

    for (uint32_t i = 0; i < node->nchildren; i++) {
        memcpy(concat + (size_t)i * MERKLE_HASH_SIZE,
               node->children[i]->hash, MERKLE_HASH_SIZE);
    }

    uint8_t expected[MERKLE_HASH_SIZE];
    merkle_sha256(concat, concat_len, expected);
    free(concat);

    return memcmp(node->hash, expected, MERKLE_HASH_SIZE) == 0;
}

bool merkle_verify_tree(const MerkleTree *tree) {
    if (!tree || !tree->root) return false;
    return verify_node(tree->root);
}

bool merkle_trees_equal(const MerkleTree *a, const MerkleTree *b) {
    if (!a || !b) return false;
    if (!a->root || !b->root) return false;
    return memcmp(a->root->hash, b->root->hash, MERKLE_HASH_SIZE) == 0;
}

/* ================================================================== */
/*  Serialization                                                       */
/* ================================================================== */

/* Count all nodes reachable from *node* (post-order). */
static uint32_t count_nodes(const MerkleNode *node) {
    if (!node) return 0;
    uint32_t n = 1;
    for (uint32_t i = 0; i < node->nchildren; i++) {
        n += count_nodes(node->children[i]);
    }
    return n;
}

/* Flatten tree into a post-order array.  Returns the number of nodes
** written starting at *out + *pos*. */
static void flatten_postorder(const MerkleNode *node,
                              const MerkleNode **out,
                              uint32_t *pos) {
    if (!node) return;
    for (uint32_t i = 0; i < node->nchildren; i++) {
        flatten_postorder(node->children[i], out, pos);
    }
    out[*pos] = node;
    (*pos)++;
}

/* Look up the post-order index of *target* in the flat array. */
static uint32_t find_index(const MerkleNode **flat, uint32_t count,
                           const MerkleNode *target) {
    for (uint32_t i = 0; i < count; i++) {
        if (flat[i] == target) return i;
    }
    return UINT32_MAX; /* should never happen */
}

/* Write a little-endian uint16_t to buf. */
static void write_le16(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
}

/* Write a little-endian uint32_t to buf. */
static void write_le32(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

/* Read a little-endian uint16_t from buf. */
static uint16_t read_le16(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

/* Read a little-endian uint32_t from buf. */
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

static const uint8_t MERKLE_MAGIC[4] = { 'M', 'R', 'K', '1' };

uint8_t *merkle_serialize(const MerkleTree *tree, size_t *out_len) {
    if (!tree || !tree->root || !out_len) return NULL;

    uint32_t node_count = count_nodes(tree->root);
    const MerkleNode **flat = calloc(node_count, sizeof(MerkleNode *));
    if (!flat) return NULL;

    uint32_t pos = 0;
    flatten_postorder(tree->root, flat, &pos);

    /* First pass: compute total buffer size. */
    size_t total = 4 + 4; /* magic + node_count */
    for (uint32_t i = 0; i < node_count; i++) {
        const MerkleNode *n = flat[i];
        uint16_t label_len = n->label ? (uint16_t)strlen(n->label) : 0;
        total += 1 + 2 + label_len + MERKLE_HASH_SIZE; /* type + label_len + label + hash */
        if (n->type == MERKLE_NODE_INTERNAL) {
            total += 4 + (size_t)n->nchildren * 4; /* nchildren + child indices */
        }
    }

    uint8_t *buf = malloc(total);
    if (!buf) {
        free(flat);
        return NULL;
    }

    size_t off = 0;
    memcpy(buf + off, MERKLE_MAGIC, 4); off += 4;
    write_le32(buf + off, node_count);  off += 4;

    for (uint32_t i = 0; i < node_count; i++) {
        const MerkleNode *n = flat[i];
        uint16_t label_len = n->label ? (uint16_t)strlen(n->label) : 0;

        buf[off++] = (uint8_t)n->type;
        write_le16(buf + off, label_len); off += 2;
        if (label_len > 0) {
            memcpy(buf + off, n->label, label_len);
            off += label_len;
        }
        memcpy(buf + off, n->hash, MERKLE_HASH_SIZE);
        off += MERKLE_HASH_SIZE;

        if (n->type == MERKLE_NODE_INTERNAL) {
            write_le32(buf + off, n->nchildren); off += 4;
            for (uint32_t c = 0; c < n->nchildren; c++) {
                uint32_t idx = find_index(flat, node_count, n->children[c]);
                write_le32(buf + off, idx); off += 4;
            }
        }
    }

    free(flat);
    *out_len = total;
    return buf;
}

/* ================================================================== */
/*  Deserialization                                                     */
/* ================================================================== */

MerkleTree *merkle_deserialize(const uint8_t *data, size_t len) {
    if (!data || len < 8) return NULL;

    /* Check magic. */
    if (memcmp(data, MERKLE_MAGIC, 4) != 0) return NULL;

    uint32_t node_count = read_le32(data + 4);
    if (node_count == 0) return NULL;

    MerkleNode **nodes = calloc(node_count, sizeof(MerkleNode *));
    if (!nodes) return NULL;

    size_t off = 8;
    bool ok = true;

    for (uint32_t i = 0; i < node_count && ok; i++) {
        if (off + 1 + 2 > len) { ok = false; break; }

        uint8_t type = data[off++];
        uint16_t label_len = read_le16(data + off); off += 2;

        if (off + label_len + MERKLE_HASH_SIZE > len) { ok = false; break; }

        MerkleNode *node = calloc(1, sizeof(*node));
        if (!node) { ok = false; break; }

        node->type = (MerkleNodeType)type;

        if (label_len > 0) {
            node->label = malloc(label_len + 1);
            if (!node->label) { free(node); ok = false; break; }
            memcpy(node->label, data + off, label_len);
            node->label[label_len] = '\0';
            off += label_len;
        } else {
            off += label_len; /* 0 */
        }

        memcpy(node->hash, data + off, MERKLE_HASH_SIZE);
        off += MERKLE_HASH_SIZE;

        if (node->type == MERKLE_NODE_INTERNAL) {
            if (off + 4 > len) { merkle_free_node(node); ok = false; break; }
            uint32_t nc = read_le32(data + off); off += 4;

            if (off + (size_t)nc * 4 > len) { merkle_free_node(node); ok = false; break; }

            node->nchildren = nc;
            node->children = calloc(nc, sizeof(MerkleNode *));
            if (!node->children) { merkle_free_node(node); ok = false; break; }

            for (uint32_t c = 0; c < nc; c++) {
                uint32_t idx = read_le32(data + off); off += 4;
                if (idx >= i) {
                    /* Child index must refer to an already-deserialised
                    ** (earlier in post-order) node. */
                    merkle_free_node(node);
                    ok = false;
                    break;
                }
                node->children[c] = nodes[idx];
            }
            if (!ok) break;
        }

        nodes[i] = node;
    }

    if (!ok) {
        /* Free only top-level wrappers to avoid double-frees of shared
        ** children.  We walk nodes in reverse and only free internal
        ** node wrappers; leaves and the deepest children are freed
        ** last via the forward pass. Actually, since post-order means
        ** children come before parents, we free in reverse order. */
        for (uint32_t i = node_count; i > 0; i--) {
            if (nodes[i - 1]) {
                free(nodes[i - 1]->children);
                free(nodes[i - 1]->label);
                free(nodes[i - 1]);
            }
        }
        free(nodes);
        return NULL;
    }

    MerkleTree *tree = calloc(1, sizeof(*tree));
    if (!tree) {
        for (uint32_t i = node_count; i > 0; i--) {
            if (nodes[i - 1]) {
                free(nodes[i - 1]->children);
                free(nodes[i - 1]->label);
                free(nodes[i - 1]);
            }
        }
        free(nodes);
        return NULL;
    }

    /* The last node in post-order is the root. */
    tree->root = nodes[node_count - 1];
    free(nodes);
    return tree;
}

/* ================================================================== */
/*  Cleanup                                                             */
/* ================================================================== */

void merkle_free_node(MerkleNode *node) {
    if (!node) return;
    free(node->label);
    free(node->children);
    free(node);
}

static void free_node_recursive(MerkleNode *node) {
    if (!node) return;
    for (uint32_t i = 0; i < node->nchildren; i++) {
        free_node_recursive(node->children[i]);
    }
    merkle_free_node(node);
}

void merkle_free_tree(MerkleTree *tree) {
    if (!tree) return;
    free_node_recursive(tree->root);
    free(tree);
}

/* ================================================================== */
/*  Debug / display                                                     */
/* ================================================================== */

void merkle_hash_to_hex(const uint8_t hash[MERKLE_HASH_SIZE], char *buf) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < MERKLE_HASH_SIZE; i++) {
        buf[i * 2]     = hex[hash[i] >> 4];
        buf[i * 2 + 1] = hex[hash[i] & 0x0f];
    }
    buf[MERKLE_HASH_SIZE * 2] = '\0';
}
