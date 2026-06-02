/*
** Snapshot system core data structures for the extensible SQL parser.
**
** A ParserSnapshot captures the complete state of a parser's tables at a
** point in time. Snapshots use atomic reference counting so they can be
** safely shared across threads: readers acquire a reference before use and
** release it when done. When the last reference is released the snapshot
** and all of its owned memory are freed.
*/
#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdint.h>
#include <stdbool.h>

/* Magic + ABI version stamped on every ParserSnapshot constructed
** via snapshot_build_from_tables.  Bumped on any layout-breaking
** change to the struct. */
#define LIME_SNAPSHOT_MAGIC       0x4C494D45U   /* L,I,M,E */
#define LIME_SNAPSHOT_ABI_VERSION 2


/* Atomic refcount type.
**
** In C this is just <stdatomic.h>'s atomic_uint_fast32_t.  In C++
** the same name is not necessarily available (g++ before C++23
** does not pull in <stdatomic.h>'s typedefs), so we typedef it to
** std::atomic<std::uint_fast32_t> which has the same memory
** layout in practice on every supported platform.  The atomic
** operations themselves only happen in C code in src/snapshot.c
** (compiled as C, sees the C definition); C++ consumers only need
** the field to exist for sizeof / pointer-to-struct purposes. */
#ifdef __cplusplus
#include <atomic>
#include <cstdint>
typedef std::atomic<std::uint_fast32_t> atomic_uint_fast32_t;
#else
#include <stdatomic.h>
#endif

/* Forward declarations for Lemon grammar structures.
** The actual definitions live in lemon.c; snapshot consumers only
** need pointers to these types. */
struct symbol;
struct rule;
struct state;

/* ------------------------------------------------------------------ */
/*  Semantic versioning                                                */
/* ------------------------------------------------------------------ */

/**
 * @brief A parsed semantic version: major.minor.patch with optional
 *        prerelease label (e.g. "1.2.3-beta.1").
 *
 * The prerelease string is heap-allocated and owned by the SemVer
 * struct; it is NULL when not present.
 */
typedef struct SemVer {
    uint32_t major;   /**< Major version component */
    uint32_t minor;   /**< Minor version component */
    uint32_t patch;   /**< Patch version component */
    char *prerelease; /**< Prerelease label (malloc'd; NULL if absent) */
} SemVer;

/**
 * @brief Version constraint operators used in dependency declarations.
 */
typedef enum VersionOp {
    VERSION_OP_EQ = 0, /* == exact match                    */
    VERSION_OP_GTE,    /* >= greater-than-or-equal          */
    VERSION_OP_LTE,    /* <= less-than-or-equal             */
    VERSION_OP_GT,     /* >  strictly greater               */
    VERSION_OP_LT,     /* <  strictly less                  */
    VERSION_OP_CARET,  /* ^  compatible (same major)        */
    VERSION_OP_TILDE,  /* ~  approximately (same major.minor) */
} VersionOp;

/**
 * @brief A single version constraint on a dependency, e.g. ">=1.2.0".
 */
typedef struct VersionConstraint {
    VersionOp op;   /**< Constraint operator */
    SemVer version; /**< Reference version for the operator */
} VersionConstraint;

/* ------------------------------------------------------------------ */
/*  Module dependency metadata                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief A dependency declaration from one module to another.
 *
 * Each dependency names a target module and carries one or more
 * version constraints.  Dependencies may be optional: optional
 * dependencies that cannot be satisfied are silently ignored rather
 * than treated as errors.
 */
typedef struct ParserDependency {
    char *module_name;              /**< Target module name (owned) */
    uint8_t merkle_root[32];        /**< Expected content hash (zero = any) */
    VersionConstraint *constraints; /**< Array of version constraints */
    uint32_t nconstraints;          /**< Number of entries in @ref constraints */
    bool optional;                  /**< If true, unsatisfied is not an error */
} ParserDependency;

/**
 * @brief A parser module: a named, versioned unit of grammar with
 *        explicit dependency, export, and import declarations.
 *
 * Modules are the unit of composition -- the dependency resolver works
 * over graphs of ParserModule nodes.
 */
typedef struct ParserModule {
    char *name;     /**< Unique module name (owned) */
    SemVer version; /**< Module version */

    ParserDependency *dependencies; /**< Array of dependencies (owned) */
    uint32_t ndependencies;         /**< Length of @ref dependencies */

    char **exports;    /**< Symbol names exported by this module */
    uint32_t nexports; /**< Length of @ref exports */

    char **imports;    /**< Symbol names imported from other modules */
    uint32_t nimports; /**< Length of @ref imports */
} ParserModule;

/* ------------------------------------------------------------------ */
/*  Snapshot                                                           */
/* ------------------------------------------------------------------ */

/*
** A ParserSnapshot holds a frozen copy of every table the generated parser
** needs at runtime. Fields fall into three groups:
**
**   1. Bookkeeping   - version, refcount, timestamps
**   2. Grammar data  - symbols, rules, states (deep copies)
**   3. Action tables  - the compact arrays that drive the parse engine
**   4. Module data   - optional module identity and content hash
*/
typedef struct ParserSnapshot {
    /* ================================================================
    ** CACHELINE 0 (bytes 0..63): hot pointer reads.
    **
    ** Every parse_token call reads from these.  Layout-critical: do
    ** not move fields around within this block without rerunning
    ** bench/bench_parse_fanout (the layout was chosen so all 8
    ** pointers occupy exactly one 64-byte cacheline on x86_64 +
    ** aarch64).  L1 data prefetcher pulls the whole line in as a
    ** unit on the first miss.
    ** ================================================================ */

    /** Cached pointer to the JIT'd jit_find_shift_action function, or
    ** NULL if no JIT is attached.  Read FIRST in find_shift_action --
    ** taking the JIT path skips the rest of the snapshot reads.  Type
    ** is uint32_t (*)(uint32_t state, uint32_t lookahead) -- declared
    ** as void* so this header doesn't need to pull in jit_context.h. */
    void *jit_find_shift_fn;

    uint16_t *yy_action;       /**< Combined shift+reduce action array */
    uint16_t *yy_lookahead;    /**< Lookahead values parallel to yy_action */
    int32_t  *yy_shift_ofst;   /**< Per-state offset into yy_action for shifts.
                               ** int32 (not int16) so grammars with action tables
                               ** larger than 32k entries (e.g. PostgreSQL's
                               ** ~145k-entry action table) can be represented
                               ** without overflow.  Memory cost on small grammars
                               ** is at most 4 * nstate bytes; trivial. */
    int32_t  *yy_reduce_ofst;  /**< Per-state offset into yy_action for reduces.
                               ** Same int32 reasoning as yy_shift_ofst above. */
    uint16_t *yy_default;      /**< Default action for each state */
    int16_t  *yy_rule_info_lhs;/**< LHS symbol code per rule */
    int8_t   *yy_rule_info_nrhs;/**< Negative number of RHS symbols per rule */

    /* ================================================================
    ** CACHELINE 1 (bytes 64..127): hot scalars + counts.
    **
    ** Action-table dispatch constants the parse_engine_step hot path
    ** consults to classify action-table entries (shift vs reduce vs
    ** accept) and to bounds-check indices into yy_action / yy_lookahead.
    ** ================================================================ */

    uint32_t lookahead_count;  /**< Number of entries in yy_lookahead */
    uint32_t action_count;     /**< Number of entries in yy_action */
    uint32_t nrule;            /**< Total number of rules */
    uint32_t nstate;           /**< Total number of parser states */

    uint16_t yy_max_shift;       /**< 0..YY_MAX_SHIFT = shift to that state */
    uint16_t yy_min_shiftreduce; /**< Shift-reduce range minimum */
    uint16_t yy_max_shiftreduce; /**< Shift-reduce range maximum */
    uint16_t yy_error_action;    /**< Marker for syntax error */
    uint16_t yy_accept_action;   /**< Marker for parser accept */
    uint16_t yy_no_action;       /**< Marker for unused slot */
    uint16_t yy_min_reduce;      /**< Reduce range minimum (max = nstate+nrule) */
    uint16_t yy_ntoken;          /**< Highest terminal code + 1 */
    /** %first_token directive value: subtracted from external token
    ** codes to get the internal action-table index.  Zero when the
    ** grammar didn't declare %first_token.  Mirrors the YYFIRSTTOKEN
    ** define in generated parsers (limpar.c template). */
    uint16_t yy_first_token;

    /* Cacheline 1 has 6 bytes of padding here to land yy_fallback on
    ** an 8-byte boundary.  Compiler inserts implicitly. */

    /** Optional fallback table (length = nfallback, may be NULL). */
    uint16_t *yy_fallback;
    uint32_t nfallback;          /**< Length of yy_fallback */
    uint32_t reserved_pad;       /**< Padding -- atomics on next line. */

    /* ================================================================
    ** CACHELINE 2 (bytes 128..191): refcount + magic.
    **
    ** Refcount is the only WRITTEN field on the parse path (via
    ** atomic_fetch_add/sub in snapshot_acquire/release).  Keep it
    ** away from the read-only hot lines above so threads doing
    ** parse_begin do not invalidate the cachelines other threads
    ** are reading yy_action etc from.
    **
    ** parse_begin_borrowed bypasses the refcount entirely (see
    ** include/parse_context.h); for the borrowed-API path this
    ** cacheline is never touched on the hot path.
    ** ================================================================ */

    /** Magic 'LIME' + ABI version stamped by snapshot_build_from_tables
    ** at construction time.  Sanity-check before dereferencing.
    ** Mismatched magic = treat the pointer as garbage.  Added v0.7.0. */
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved16; /**< Padding -- next field is 8-byte aligned. */

    /** Monotonically increasing version number.  Each new snapshot
    ** produced from a grammar modification gets a higher version
    ** than the last. */
    uint64_t version;

    /** Atomic reference count.  Starts at 1 on creation.  Every call to
    ** snapshot_acquire() adds 1; every call to snapshot_release() subtracts
    ** 1.  When the count drops to 0 the snapshot is destroyed. */
    atomic_uint_fast32_t refcount;

    uint32_t reserved_pad2;      /**< Padding to 8-byte boundary. */

    /* ================================================================
    ** COLD: setup-time + introspection fields (rarely read after
    ** snapshot construction).
    ** ================================================================ */

    /* --- Grammar data (deep-copied, owned by this snapshot) ----------- */

    struct symbol **symbols; /**< Array of pointers to symbol structs */
    uint32_t nsymbol;        /**< Total number of symbols */
    uint32_t nterminal;      /**< Number of terminal symbols */

    struct rule *rules; /**< Linked list of production rules */

    struct state **states; /**< Array of pointers to state structs */

    /** Optional original grammar source text.  Populated by `lime -n`
    ** for snapshots produced via snapshot_build_from_tables().  NULL
    ** when the source is not retained. */
    char *grammar_source;
    uint32_t grammar_source_len;
    uint32_t reserved_pad3;

    /* --- Module identity (optional, NULL when not part of a module) --- */

    uint8_t merkle_root[32]; /**< Content hash of grammar data */
    ParserModule *module;    /**< Owning module metadata, or NULL */

    /** Nanosecond-precision wall-clock time when this snapshot was created. */
    uint64_t create_time_ns;

    /** JIT compilation context. */
    void *jit_ctx;
} ParserSnapshot;

/*
** Acquire a reference to an existing snapshot.  The caller must eventually
** call snapshot_release() to avoid leaking the snapshot.
**
** Returns the same pointer that was passed in, for convenience:
**     ParserSnapshot *my_ref = snapshot_acquire(shared_snap);
**
** Passing NULL is safe and returns NULL.
*/
ParserSnapshot *snapshot_acquire(ParserSnapshot *snap);

/*
** Release a reference previously obtained via snapshot_acquire() or
** create_base_snapshot().  When the last reference is released the
** snapshot and all memory it owns are freed.
**
** Passing NULL is safe and does nothing.
*/
void snapshot_release(ParserSnapshot *snap);

/*
** Create a base snapshot from *grammar_file*.
**
** The runtime delegates to a per-grammar <Prefix>BuildSnapshot()
** function emitted by `lime -n grammar.y` (see snapshot_build.h
** and the LimeParserTables struct).  When no such builder is linked
** into the host process this function returns NULL with an
** actionable error message.
**
** Building a snapshot directly from a grammar file at runtime --
** without a pre-compiled <Prefix>BuildSnapshot() symbol -- is
** under construction.  It requires exposing the lime generator's
** LALR(1) Build()/ReportTable() phases as a library callable from
** the runtime; the generator's algorithm is fully implemented in
** lime.c but not yet refactored into a public function.
**
** Returns a snapshot with refcount == 1 on success, or NULL with
** *error pointing to a malloc'd message on failure.
*/
ParserSnapshot *create_base_snapshot(const char *grammar_file, char **error);

/* ------------------------------------------------------------------ */
/*  SemVer utilities                                                   */
/* ------------------------------------------------------------------ */

/*
** Parse a semantic version string ("1.2.3" or "1.2.3-beta.1") into a
** SemVer struct.  Returns true on success.  On failure *out is zeroed
** and the function returns false.
*/
bool semver_parse(const char *str, SemVer *out);

/*
** Compare two semantic versions.  Returns <0, 0, or >0 following the
** same convention as strcmp.  Prerelease versions sort before their
** release counterpart (e.g. 1.0.0-alpha < 1.0.0).
*/
int semver_compare(const SemVer *a, const SemVer *b);

/*
** Check whether *ver* satisfies *constraint*.
*/
bool semver_satisfies(const SemVer *ver, const VersionConstraint *constraint);

/*
** Free resources owned by a SemVer (just the prerelease string).
** Does not free the SemVer struct itself.
*/
void semver_destroy(SemVer *v);

/* ------------------------------------------------------------------ */
/*  Module lifecycle helpers                                           */
/* ------------------------------------------------------------------ */

/*
** Deep-free a ParserModule and all memory it owns (name, version,
** dependencies, exports, imports).  Does not free the pointer itself
** unless *mod* was heap-allocated by the caller.
*/
void parser_module_destroy_contents(ParserModule *mod);

/*
** Deep-free a ParserDependency's owned memory.
*/
void parser_dependency_destroy_contents(ParserDependency *dep);

#endif /* SNAPSHOT_H */
