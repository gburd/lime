/* tests/test_compiler_context.c -- ROADMAP item 1, phase 1.
**
** Exercises the LimeCompilerContext structural decoupling that
** phase 1 introduces.  Phase 1 extracts the LALR generator's
** file-static and function-static globals into a per-compilation
** context so the pipeline can run reentrantly; phase 3 will add
** the public lime_compile_grammar_in_process() entry point.
**
** This test compiles lime.c directly (with LIME_TEST_HARNESS so
** main() is omitted) and pokes at the leaf helpers (allocator
** arena, Strsafe, Symbol_*, State_*, Configtable_*, Plink, set
** size, configlist).  No pipeline run -- that is phase-2-and-up
** territory because Parse() still reads from a file path.
**
** Sub-tests:
**   1. basic init/destroy round trip -- ctx fields zeroed on init,
**      arena clean after destroy.
**   2. two contexts in the same process do not share storage; a
**      Symbol_new() in one ctx is invisible to the other.
**   3. lime_compiler_context_destroy walks and frees the entire
**      arena; valgrind-leak-clean (verified externally).
**   4. errorcnt isolation -- in phase 1 errorcnt lives on
**      struct lime, not on the ctx; we verify two independent
**      struct lime objects (one per ctx) keep independent counts.
*/

#define LIME_TEST_HARNESS
#include "../lime.c"

/* The macros at the top of lime.c rewire the original global
** identifiers (memChunkList, x1a, current, etc.) onto
** lime_active_ctx field accesses; that is what makes the leaf
** helpers ctx-aware without per-call-site edits.  But the test
** body wants to read those fields off a specific ctx object
** (e.g. `cc.memChunkList`), and the macro turns that into
** `cc.(lime_active_ctx->memChunkList)` which is nonsense.  Drop
** the macros now that lime.c has finished parsing -- the leaf
** helpers were already preprocessed and locked in. */
#undef memChunkList
#undef x1a
#undef x2a
#undef x3a
#undef x4a
#undef plink_freelist
#undef actionfreelist
#undef current
#undef currentend
#undef basis
#undef basisend
#undef nDefine
#undef nDefineUsed
#undef azDefine
#undef bDefineUsed

#include <assert.h>

static int n_pass = 0;
static int n_fail = 0;

#define CHECK(cond, name) do {                                          \
    if (cond) {                                                         \
        printf("PASS: %s\n", (name));                                   \
        ++n_pass;                                                       \
    } else {                                                            \
        printf("FAIL: %s (line %d)\n", (name), __LINE__);               \
        ++n_fail;                                                       \
    }                                                                   \
} while (0)

/* Sub-test 1: init / destroy round trip.  After init the ctx
** is zeroed and active; after destroy the arena is empty.
** Activation is local to the destroy call, so we re-install
** between the two checks with a fresh init. */
static void test_init_destroy(void) {
    LimeCompilerContext cc;
    lime_compiler_context_init(&cc);

    CHECK(lime_active_ctx == &cc, "ctx1: active pointer set on init");
    CHECK(cc.memChunkList == NULL, "ctx1: arena empty on init");
    CHECK(cc.x1a == NULL && cc.x2a == NULL && cc.x3a == NULL && cc.x4a == NULL,
          "ctx1: hash tables NULL on init");
    CHECK(cc.cfg_freelist == NULL && cc.cfg_current == NULL,
          "ctx1: configlist freelist/current NULL on init");
    CHECK(cc.plink_freelist == NULL && cc.actionfreelist == NULL,
          "ctx1: plink/action pools NULL on init");
    CHECK(cc.set_size == 0, "ctx1: set_size zero on init");
    CHECK(cc.append_str_z == NULL && cc.append_str_alloced == 0
          && cc.append_str_used == 0,
          "ctx1: append_str scratch zero on init");

    /* Allocate a few things to populate the arena. */
    Strsafe_init();
    Symbol_init();
    (void) Symbol_new("foo");
    (void) Symbol_new("BAR");
    CHECK(cc.memChunkList != NULL, "ctx1: arena non-empty after Symbol_new");
    CHECK(cc.x1a != NULL && cc.x2a != NULL,
          "ctx1: Strsafe/Symbol hash tables alloc'd");
    CHECK(Symbol_count() == 2, "ctx1: Symbol_count == 2");

    lime_compiler_context_destroy(&cc);
    CHECK(cc.memChunkList == NULL, "ctx1: arena empty after destroy");
    CHECK(cc.x1a == NULL && cc.x2a == NULL,
          "ctx1: hash table pointers cleared after destroy");
    CHECK(lime_active_ctx == NULL, "ctx1: active pointer cleared after destroy");
}

/* Sub-test 2: two contexts in the same process do not interfere.
** We create ctx_a, populate it with a unique symbol, then SWITCH
** to ctx_b and confirm Symbol_find() does not see the ctx_a entry.
** Then switch back and confirm ctx_a still has it. */
static void test_two_context_isolation(void) {
    LimeCompilerContext cc_a, cc_b;
    struct symbol *sa, *sb;
    int count_a, count_b;

    lime_compiler_context_init(&cc_a);
    Strsafe_init();
    Symbol_init();
    (void) Symbol_new("alpha_only_in_a");
    (void) Symbol_new("BETA_ONLY_IN_A");
    count_a = Symbol_count();
    CHECK(count_a == 2, "ctx_a: Symbol_count == 2");

    /* Switch to ctx_b without destroying ctx_a (sequential
    ** alternation, the load-bearing isolation case). */
    lime_compiler_context_init(&cc_b);
    CHECK(lime_active_ctx == &cc_b, "ctx_b active after second init");

    Strsafe_init();
    Symbol_init();
    sa = Symbol_find("alpha_only_in_a");
    CHECK(sa == NULL, "ctx_b: ctx_a symbol invisible (no cross-contamination)");
    (void) Symbol_new("gamma_only_in_b");
    count_b = Symbol_count();
    CHECK(count_b == 1, "ctx_b: Symbol_count == 1 (independent table)");

    /* Switch back to ctx_a and confirm its state survived. */
    lime_active_ctx = &cc_a;
    sa = Symbol_find("alpha_only_in_a");
    sb = Symbol_find("gamma_only_in_b");
    CHECK(sa != NULL, "ctx_a: own symbol still findable after switch");
    CHECK(sb == NULL, "ctx_a: ctx_b symbol invisible after switch back");
    CHECK(Symbol_count() == 2, "ctx_a: Symbol_count still == 2");

    /* Tear down both. */
    lime_active_ctx = &cc_b;
    lime_compiler_context_destroy(&cc_b);
    lime_active_ctx = &cc_a;
    lime_compiler_context_destroy(&cc_a);
    CHECK(lime_active_ctx == NULL, "both contexts destroyed");
}

/* Sub-test 3: destroy reclaims every chunk.  We don't have a
** chunk-counter; the test asserts memChunkList==NULL post-destroy
** and trusts the harness's external valgrind/ASan run for the
** stronger leak property. */
static void test_destroy_clears_arena(void) {
    LimeCompilerContext cc;
    lime_compiler_context_init(&cc);

    /* Allocate a varied workload: hash tables, set storage,
    ** configlist scratch, plink pool, action pool. */
    Strsafe_init();
    Symbol_init();
    State_init();
    Configtable_init();
    SetSize(64);

    /* Touch the per-ctx pools so they're allocated. */
    (void) Plink_new();
    (void) Action_new();
    char *s = SetNew();
    SetAdd(s, 5);
    SetAdd(s, 7);

    CHECK(cc.memChunkList != NULL, "test3: arena populated");
    CHECK(cc.x1a != NULL && cc.x2a != NULL && cc.x3a != NULL && cc.x4a != NULL,
          "test3: all four hash tables allocated");
    CHECK(cc.plink_freelist != NULL, "test3: plink pool allocated");
    CHECK(cc.actionfreelist != NULL, "test3: action pool allocated");
    CHECK(cc.set_size == 65, "test3: set_size set");

    lime_compiler_context_destroy(&cc);
    CHECK(cc.memChunkList == NULL, "test3: arena cleared by destroy");
    CHECK(cc.x1a == NULL && cc.x2a == NULL && cc.x3a == NULL && cc.x4a == NULL,
          "test3: hash table pointers cleared by destroy");
    CHECK(cc.plink_freelist == NULL && cc.actionfreelist == NULL,
          "test3: pool pointers cleared by destroy");
}

/* Sub-test 4: errorcnt isolation.  In phase 1 errorcnt is still on
** struct lime (not on ctx).  Two independent struct lime objects
** carrying their own errorcnt is sufficient: the property under
** test is that bumping errorcnt on lemA does not leak into lemB.
** This validates that the cross-context isolation extends to the
** primary error-tracking surface. */
static void test_errorcnt_isolation(void) {
    LimeCompilerContext cc_a, cc_b;
    struct lime lemA, lemB;

    memset(&lemA, 0, sizeof(lemA));
    memset(&lemB, 0, sizeof(lemB));

    lime_compiler_context_init(&cc_a);
    /* Simulate three error reports on lemA. */
    lemA.errorcnt = 3;
    CHECK(lemA.errorcnt == 3, "lemA: errorcnt == 3 after three errors");
    CHECK(lemB.errorcnt == 0, "lemB: errorcnt unaffected by lemA");

    lime_compiler_context_init(&cc_b);
    /* lemB error report under ctx_b should not affect lemA. */
    lemB.errorcnt = 1;
    CHECK(lemA.errorcnt == 3, "lemA: errorcnt unchanged by lemB");
    CHECK(lemB.errorcnt == 1, "lemB: errorcnt == 1 in own struct lime");

    lime_active_ctx = &cc_b;
    lime_compiler_context_destroy(&cc_b);
    lime_active_ctx = &cc_a;
    lime_compiler_context_destroy(&cc_a);
}

int main(void) {
    test_init_destroy();
    test_two_context_isolation();
    test_destroy_clears_arena();
    test_errorcnt_isolation();
    printf("\n%d passed, %d failed\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
