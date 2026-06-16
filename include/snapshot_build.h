/**
 * @file snapshot_build.h
 * @brief Build a runtime ParserSnapshot from generated parser tables.
 *
 * A Lime-generated parser contains everything needed to drive an
 * LALR(1) parse: action tables, rule metadata, and the constants that
 * partition the action range into shifts / reduces / errors.  The
 * generator emits these as static arrays inside the parser .c file.
 *
 * This header provides the bridge that lets a generated parser hand
 * those tables to the runtime so the runtime push parser
 * (parse_begin / parse_token / parse_end) and the JIT can drive them.
 *
 * Two ways to use this:
 *
 *   1. Call lime_snapshot_create("foo.y", &err) which subprocesses
 *      `lime -n foo.y` to produce a *_snapshot.c file, compiles it,
 *      dlopens it, and calls the registration entry point below.
 *      Used for true runtime grammar loading.
 *
 *   2. The generated parser's *_snapshot.c file calls
 *      snapshot_build_from_tables() at static init / on-demand to
 *      hand its tables to a snapshot the application acquires.
 *      Used for "static parser, runtime API" applications.
 *
 * The snapshot returned takes a *shared* reference to the static
 * tables -- it does not free them -- so the same generated parser can
 * be used by many concurrent ParseContext instances.  When extensions
 * mutate the snapshot via apply_modification, the snapshot's tables
 * are first deep-copied; the originals stay untouched.
 */
#ifndef SNAPSHOT_BUILD_H
#define SNAPSHOT_BUILD_H

#include "snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bundle of pointers + sizes the generator emits for a parser.
 *
 * Every Lime-generated parser exposes (via its *_snapshot.c output, or
 * directly if the application links against the static tables) the
 * fields below.  Field names match the existing limpar.c emitted
 * symbols so the generator can wire them up mechanically.
 */
typedef struct LimeParserTables {
    /* Action tables */
    const uint16_t *yy_action;
    uint32_t yy_action_count;
    const uint16_t *yy_lookahead;
    uint32_t yy_lookahead_count;
    const int32_t *yy_shift_ofst;
    const int32_t *yy_reduce_ofst;
    const uint16_t *yy_default;
    uint32_t nstate;

    /* Rule metadata */
    const int16_t *yy_rule_info_lhs;
    const int8_t *yy_rule_info_nrhs;
    uint32_t nrule;

    /* Counts */
    uint32_t nsymbol;
    uint32_t nterminal;
    uint16_t ntoken; /* YYNTOKEN = highest terminal + 1 */
    /* YYFIRSTTOKEN: the %first_token directive value (0 when not
    ** declared).  Subtracted from external token codes by parse_token
    ** to compute the internal action-table index.  Paired with
    ** ntoken: the valid external code range is
    ** [first_token, first_token + ntoken). */
    uint16_t first_token;

    /* Action-range constants */
    uint16_t yy_max_shift;
    uint16_t yy_min_shiftreduce;
    uint16_t yy_max_shiftreduce;
    uint16_t yy_error_action;
    uint16_t yy_accept_action;
    uint16_t yy_no_action;
    uint16_t yy_min_reduce;

    /* Optional fallback (NULL when grammar has no %fallback) */
    const uint16_t *yy_fallback;
    uint32_t nfallback;

    /* Optional original grammar source text.  Embedded by `lime -n`
    ** so the runtime extension framework can rebuild the LALR(1)
    ** automaton from a base snapshot plus a list of modifications:
    ** publish_modified_snapshot() concatenates this text with
    ** lime_modifications_to_grammar_text(mods) and reruns the
    ** subprocess pipeline (lime + cc) on the merged grammar.
    **
    ** NULL when the snapshot was built by a path that did not have
    ** access to the original .y text (e.g. a hand-built snapshot
    ** for testing).  Callers that want runtime grammar mutation
    ** must supply this, or a base snapshot whose generated builder
    ** populated it.  Length is the byte count NOT including a
    ** trailing NUL (the array always has one for C-string-friendly
    ** consumption). */
    const char *grammar_source;
    uint32_t grammar_source_len;

    /* Magic + ABI version pair.  Set unconditionally by the
    ** generator.  snapshot_build_from_tables checks both and
    ** rejects mismatched bundles with a clear error rather than
    ** silently producing a wrong snapshot.  v0.6.3 generated
    ** *_snapshot.c files leave these zero (partial initialiser);
    ** v0.7.0+ runtime detects that and reports "snapshot was
    ** built against pre-v0.7.0 lime; rebuild required". */
    uint32_t magic;       /* LIME_TABLES_MAGIC */
    uint16_t abi_version; /* LIME_TABLES_ABI_VERSION */

    /* Optional host-reduce hook (Letter 30).  `lime -n` sets this to
    ** the generated, exported <Name>HostReduce wrapper so the push
    ** parser can run BASE-grammar reduce actions over the snapshot.
    ** NULL for recognition-only snapshots (and for tables emitted by
    ** pre-host-reduce lime), in which case parse_token accepts/rejects
    ** without running actions.  Copied verbatim into the snapshot. */
    LimeHostReduceFn host_reduce;
    void *host_reduce_user;
} LimeParserTables;

#define LIME_TABLES_MAGIC       0x4C494D45U   /* 'L','I','M','E' */
#define LIME_TABLES_ABI_VERSION 1

/**
 * @brief Build a fresh ParserSnapshot from a LimeParserTables bundle.
 *
 * The snapshot deep-copies the action tables and rule metadata so that
 * subsequent extension modifications do not touch the static
 * generator-emitted arrays.  Returns a snapshot with refcount 1, or
 * NULL on allocation failure.
 *
 * @param tables Compile-time table bundle from a generated parser.
 * @return New snapshot, or NULL on allocation failure.
 */
ParserSnapshot *snapshot_build_from_tables(const LimeParserTables *tables);

#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_BUILD_H */
