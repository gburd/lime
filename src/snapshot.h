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
#include <stdatomic.h>

/* Forward declarations for Lemon grammar structures.
** The actual definitions live in lemon.c; snapshot consumers only
** need pointers to these types. */
struct symbol;
struct rule;
struct state;

/*
** A ParserSnapshot holds a frozen copy of every table the generated parser
** needs at runtime. Fields fall into three groups:
**
**   1. Bookkeeping   - version, refcount, timestamps
**   2. Grammar data  - symbols, rules, states (deep copies)
**   3. Action tables  - the compact arrays that drive the parse engine
*/
typedef struct ParserSnapshot {
    /* Monotonically increasing version number. Each new snapshot produced
    ** from a grammar modification gets a higher version than the last. */
    uint64_t version;

    /* Atomic reference count.  Starts at 1 on creation.  Every call to
    ** snapshot_acquire() adds 1; every call to snapshot_release() subtracts
    ** 1.  When the count drops to 0 the snapshot is destroyed. */
    atomic_uint_fast32_t refcount;

    /* --- Grammar data (deep-copied, owned by this snapshot) ----------- */

    struct symbol **symbols;       /* Array of pointers to symbol structs   */
    uint32_t nsymbol;              /* Total number of symbols               */
    uint32_t nterminal;            /* Number of terminal symbols            */

    struct rule *rules;            /* Linked list of production rules       */
    uint32_t nrule;                /* Total number of rules                 */

    struct state **states;         /* Array of pointers to state structs    */
    uint32_t nstate;               /* Total number of parser states         */

    /* --- Compact action tables (heap-allocated, owned) ---------------- */

    uint16_t *yy_action;          /* Combined shift+reduce action array    */
    uint16_t *yy_lookahead;       /* Lookahead values parallel to yy_action*/
    int16_t  *yy_shift_ofst;      /* Per-state offset into yy_action for shifts  */
    int16_t  *yy_reduce_ofst;     /* Per-state offset into yy_action for reduces */
    uint16_t *yy_default;         /* Default action for each state         */
    uint32_t action_count;         /* Number of entries in yy_action        */
    uint32_t lookahead_count;      /* Number of entries in yy_lookahead     */

    /* Nanosecond-precision wall-clock time when this snapshot was created */
    uint64_t create_time_ns;

    /* Reserved for a future JIT compilation context that can cache
    ** machine code generated from the action tables. */
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
** Create a base snapshot by parsing *grammar_file* through the Lemon
** parser generator.  On success a new snapshot with refcount == 1 is
** returned and *error is set to NULL.  On failure NULL is returned and
** *error points to a malloc'd error message that the caller must free.
**
** NOTE: This is currently a stub.  Full implementation comes when Lemon
** is modified to emit dynamic tables (Task #3).
*/
ParserSnapshot *create_base_snapshot(const char *grammar_file, char **error);

#endif /* SNAPSHOT_H */
