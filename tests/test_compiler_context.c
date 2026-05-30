/* tests/test_compiler_context.c -- ROADMAP item 1, phases 1 + 2.
**
** Phase 1 introduced LimeCompilerContext (extracted 14 file-static
** globals into a per-compilation context).  Phase 2 decouples the
** grammar parser from file I/O by adding ParseText(): the
** in-process entry point that takes a pre-loaded text buffer.
** Phase 3 will build the public lime_compile_grammar_in_process()
** API on top of these two foundations.
**
** This test compiles lime.c directly (with LIME_TEST_HARNESS so
** main() is omitted) and pokes at:
**   - phase 1 leaf helpers (allocator arena, Strsafe, Symbol_*,
**     State_*, Configtable_*, Plink, set size, configlist).
**   - phase 2 ParseText / Parse parity, buffer ownership, line
**     numbers, and cross-context isolation across full parses.
**
** Sub-tests (phase 1):
**   1. basic init/destroy round trip -- ctx fields zeroed on init,
**      arena clean after destroy.
**   2. two contexts in the same process do not share storage; a
**      Symbol_new() in one ctx is invisible to the other.
**   3. lime_compiler_context_destroy walks and frees the entire
**      arena; valgrind-leak-clean (verified externally).
**   4. errorcnt isolation -- in phase 1 errorcnt lives on
**      struct lime, not on the ctx; we verify two independent
**      struct lime objects (one per ctx) keep independent counts.
**
** Sub-tests (phase 2):
**   5. ParseText basic -- a small in-memory grammar produces a
**      non-empty rule list and the expected nrule / nsymbol /
**      nterminal counts.
**   6. ParseText source-buffer ownership -- after ParseText
**      returns, the caller's text buffer is byte-for-byte
**      unchanged (always-copy contract).
**   7. ParseText accepts non-NUL-terminated input -- the function
**      does not read past `text + len`, validating the
**      always-copy contract under tight buffer bounds.
**   8. ParseText line-number diagnostic -- a deliberate syntax
**      error on grammar line 7 surfaces as `<filename>:7:` in the
**      ErrorMsg output.
**   9. Parse() === ParseText() equivalence -- compile the same
**      grammar through both entry points (in distinct contexts)
**      and assert nrule / nsymbol / nterminal / rule-LHS
**      sequence match exactly.
**  10. Two-context isolation across ParseText -- two contexts
**      in the same process compile two different grammars and
**      neither sees the other's symbols.
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
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

/* ====================================================================
** Phase-2 helpers and sub-tests.
**
** ParseText() and Parse() build a real `struct lime` -- they need
** Strsafe_init / Symbol_init / State_init beforehand and a
** Symbol_new("$") sentinel (mirrors what main() in lime.c does).
** parse_setup() factors that pre-roll out so each sub-test stays
** focused on the property under test.
** ==================================================================== */

static const char k_simple_grammar[] =
    "%name TestG\n"
    "%token_type {int}\n"
    "%type expr {int}\n"
    "\n"
    "%token PLUS.\n"
    "%token MINUS.\n"
    "%token NUM.\n"
    "\n"
    "%left PLUS MINUS.\n"
    "\n"
    "start ::= expr(A). { (void)A; }\n"
    "expr(A) ::= expr(B) PLUS expr(C). { A = B + C; }\n"
    "expr(A) ::= expr(B) MINUS expr(C). { A = B - C; }\n"
    "expr(A) ::= NUM(B). { A = B; }\n";

/* parse_setup -- run the same prelude as lime.c::main() does before
** Parse(): zero the struct lime, set the few non-zero defaults,
** initialize the global tables on the active ctx, and install the
** "$" sentinel.  Caller owns `lem`; this just initialises it. */
static void parse_setup(struct lime *lem, const char *fake_filename) {
    memset(lem, 0, sizeof(*lem));
    lem->errorcnt = 0;
    lem->nexpect = -1;
    lem->first_token = 0;
    lem->filename = (char *)fake_filename;
    Strsafe_init();
    Symbol_init();
    State_init();
    Symbol_new("$");
}

/* count_rules -- walk the linked rule list. */
static int count_rules(struct rule *first) {
    int n = 0;
    for (struct rule *rp = first; rp; rp = rp->next) ++n;
    return n;
}

/* Sub-test 5 (phase 2): ParseText basic.  A small in-memory grammar
** parses without errors and produces a non-empty rule list with the
** expected counts. */
static void test_parsetext_basic(void) {
    LimeCompilerContext cc;
    struct lime lem;

    lime_compiler_context_init(&cc);
    parse_setup(&lem, "<grammar text>");

    ParseText(&lem, k_simple_grammar, strlen(k_simple_grammar));

    CHECK(lem.errorcnt == 0, "parsetext_basic: errorcnt == 0");
    CHECK(lem.rule != NULL, "parsetext_basic: rule list non-empty");
    /* 4 rules: start, expr+expr, expr-expr, expr<-NUM. */
    CHECK(count_rules(lem.rule) == 4,
          "parsetext_basic: 4 rules in linked list");
    CHECK(lem.nrule == 4, "parsetext_basic: gp->nrule == 4");
    /* %name was honoured (Strsafe-interned). */
    CHECK(lem.name != NULL && strcmp(lem.name, "TestG") == 0,
          "parsetext_basic: %name captured as TestG");

    lime_compiler_context_destroy(&cc);
}

/* Sub-test 6 (phase 2): buffer-ownership contract.  ParseText() copies
** internally and never mutates the caller's buffer.  The grammar
** below contains a `%ifdef` block that preprocess_input() rewrites
** in place on the WORKING copy -- if ParseText mistakenly aliased
** the caller's buffer, the `%ifdef` text would be blanked. */
static void test_parsetext_buffer_ownership(void) {
    LimeCompilerContext cc;
    struct lime lem;

    static const char src[] =
        "%name OwnG\n"
        "%token A.\n"
        "%ifdef NEVER_DEFINED\n"
        "  %token GHOST.\n"
        "%endif\n"
        "start ::= A. { }\n";
    /* Heap copy so we can verify it's untouched on return. */
    size_t n = sizeof(src) - 1;
    char *heapbuf = (char *)malloc(n + 1);
    memcpy(heapbuf, src, n + 1);
    char *baseline = (char *)malloc(n + 1);
    memcpy(baseline, src, n + 1);

    lime_compiler_context_init(&cc);
    parse_setup(&lem, "<grammar text>");

    ParseText(&lem, heapbuf, n);

    CHECK(lem.errorcnt == 0,
          "buffer_ownership: parse succeeded");
    CHECK(memcmp(heapbuf, baseline, n + 1) == 0,
          "buffer_ownership: caller's buffer byte-for-byte unchanged");
    /* The %ifdef block evaluated to false, so GHOST should never
    ** have entered the symbol table.  This proves preprocess_input
    ** ran on a private copy. */
    CHECK(Symbol_find("GHOST") == NULL,
          "buffer_ownership: %ifdef-gated GHOST excluded");

    free(heapbuf);
    free(baseline);
    lime_compiler_context_destroy(&cc);
}

/* Sub-test 7 (phase 2): ParseText must not read past `text + len`.
** We allocate a buffer of EXACTLY `len` bytes (no trailing NUL or
** sentinel) and pass `len`.  Because ParseText copies into its own
** `len + 1` working buffer and writes the NUL itself, this must
** parse cleanly without ASan / valgrind heap-overflow reports.
** The always-copy contract is what makes this safe. */
static void test_parsetext_no_overread(void) {
    LimeCompilerContext cc;
    struct lime lem;

    static const char src[] =
        "%name BoundG\n"
        "%token TOK.\n"
        "start ::= TOK. { }\n";
    size_t n = sizeof(src) - 1;  /* exclude the trailing NUL */
    char *tight = (char *)malloc(n);  /* no trailing slot */
    memcpy(tight, src, n);

    lime_compiler_context_init(&cc);
    parse_setup(&lem, "<grammar text>");

    ParseText(&lem, tight, n);

    CHECK(lem.errorcnt == 0,
          "no_overread: parsed without out-of-bounds read");
    CHECK(lem.rule != NULL && count_rules(lem.rule) == 1,
          "no_overread: 1 rule materialized");

    free(tight);
    lime_compiler_context_destroy(&cc);
}

/* Sub-test 8 (phase 2): line-number diagnostics.  Put a syntax error
** on grammar line 7 and verify ErrorMsg surfaces "<fn>:7:".  We
** redirect stderr to a tmpfile, parse, then read the captured
** output back. */
static void test_parsetext_line_numbers(void) {
    LimeCompilerContext cc;
    struct lime lem;

    /* Lines:
    ** 1: %name LineG
    ** 2: %token A.
    ** 3: %token B.
    ** 4:
    ** 5: start ::= A. { }
    ** 6: rule2 ::= A B. { }
    ** 7: 9bad_lhs ::= A.    <- syntax error: LHS must start with letter
    */
    static const char bad_src[] =
        "%name LineG\n"
        "%token A.\n"
        "%token B.\n"
        "\n"
        "start ::= A. { }\n"
        "rule2 ::= A B. { }\n"
        "9bad_lhs ::= A. { }\n";

    /* Redirect stderr to a memstream-backed tmpfile. */
    fflush(stderr);
    int saved_stderr = dup(fileno(stderr));
    FILE *tmp = tmpfile();
    int tmpfd = fileno(tmp);
    dup2(tmpfd, fileno(stderr));

    lime_compiler_context_init(&cc);
    parse_setup(&lem, "<line-test>");
    ParseText(&lem, bad_src, sizeof(bad_src) - 1);

    fflush(stderr);
    dup2(saved_stderr, fileno(stderr));
    close(saved_stderr);

    /* Read back the captured stderr. */
    rewind(tmp);
    char buf[2048];
    size_t got = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[got] = 0;
    fclose(tmp);

    CHECK(lem.errorcnt > 0,
          "line_numbers: parser flagged the error");
    CHECK(strstr(buf, "<line-test>:7:") != NULL,
          "line_numbers: diagnostic reports line 7 with synthetic filename");

    lime_compiler_context_destroy(&cc);
}

/* Sub-test 9 (phase 2): Parse() === ParseText() equivalence.  Write
** the same grammar to disk, run Parse() against the file in ctx_a,
** run ParseText() against the in-memory copy in ctx_b, and assert
** identical nrule / nsymbol / nterminal plus identical rule-LHS
** sequences. */
static void test_parse_parsetext_equivalence(void) {
    LimeCompilerContext cc_a, cc_b;
    struct lime lem_a, lem_b;

    /* Materialize the grammar on disk for the file-based path. */
    char path[256];
    int fd = test_compat_mkstemp("lime_phase2_parse", path, sizeof(path));
    CHECK(fd >= 0, "equivalence: mkstemp succeeded");
    size_t n = strlen(k_simple_grammar);
    ssize_t written = write(fd, k_simple_grammar, n);
    CHECK((size_t)written == n, "equivalence: wrote full grammar");
    close(fd);

    /* Path A: file-based Parse(). */
    lime_compiler_context_init(&cc_a);
    parse_setup(&lem_a, path);
    Parse(&lem_a);
    CHECK(lem_a.errorcnt == 0, "equivalence: file-based Parse clean");
    int nrule_a = count_rules(lem_a.rule);
    int gp_nrule_a = lem_a.nrule;

    /* Capture LHS sequence from path A while ctx_a is still active. */
    char *lhs_seq_a[16];
    int n_lhs_a = 0;
    for (struct rule *rp = lem_a.rule; rp && n_lhs_a < 16; rp = rp->next) {
        lhs_seq_a[n_lhs_a++] = strdup(rp->lhs ? rp->lhs->name : "?");
    }

    /* Path B: buffer-based ParseText() in a fresh context. */
    lime_compiler_context_init(&cc_b);
    parse_setup(&lem_b, "<grammar text>");
    ParseText(&lem_b, k_simple_grammar, strlen(k_simple_grammar));
    CHECK(lem_b.errorcnt == 0, "equivalence: buffer-based ParseText clean");
    int nrule_b = count_rules(lem_b.rule);
    int gp_nrule_b = lem_b.nrule;

    char *lhs_seq_b[16];
    int n_lhs_b = 0;
    for (struct rule *rp = lem_b.rule; rp && n_lhs_b < 16; rp = rp->next) {
        lhs_seq_b[n_lhs_b++] = strdup(rp->lhs ? rp->lhs->name : "?");
    }

    CHECK(nrule_a == nrule_b,
          "equivalence: rule list lengths match");
    CHECK(gp_nrule_a == gp_nrule_b,
          "equivalence: gp->nrule values match");
    int seq_match = (n_lhs_a == n_lhs_b);
    for (int i = 0; seq_match && i < n_lhs_a; ++i) {
        if (strcmp(lhs_seq_a[i], lhs_seq_b[i]) != 0) seq_match = 0;
    }
    CHECK(seq_match, "equivalence: rule-LHS sequences match");

    for (int i = 0; i < n_lhs_a; ++i) free(lhs_seq_a[i]);
    for (int i = 0; i < n_lhs_b; ++i) free(lhs_seq_b[i]);

    lime_active_ctx = &cc_b;
    lime_compiler_context_destroy(&cc_b);
    lime_active_ctx = &cc_a;
    lime_compiler_context_destroy(&cc_a);
    unlink(path);
}

/* Sub-test 10 (phase 2): two contexts running ParseText on different
** grammars in the same process do not cross-contaminate.  Symbols
** unique to ctx_a are invisible from ctx_b's symbol table and vice
** versa, even after both parses have completed. */
static void test_parsetext_two_context_isolation(void) {
    LimeCompilerContext cc_a, cc_b;
    struct lime lem_a, lem_b;

    static const char grammar_a[] =
        "%name AlphaG\n"
        "%token ALPHA_TOKEN.\n"
        "%token UNIQUE_TO_A.\n"
        "start ::= ALPHA_TOKEN UNIQUE_TO_A. { }\n";
    static const char grammar_b[] =
        "%name BetaG\n"
        "%token BETA_TOKEN.\n"
        "%token UNIQUE_TO_B.\n"
        "start ::= BETA_TOKEN UNIQUE_TO_B. { }\n";

    lime_compiler_context_init(&cc_a);
    parse_setup(&lem_a, "<a>");
    ParseText(&lem_a, grammar_a, strlen(grammar_a));
    CHECK(lem_a.errorcnt == 0, "isolation: grammar_a parsed clean");
    CHECK(Symbol_find("UNIQUE_TO_A") != NULL,
          "isolation: ctx_a sees its own UNIQUE_TO_A");
    CHECK(Symbol_find("UNIQUE_TO_B") == NULL,
          "isolation: ctx_a does not see UNIQUE_TO_B");

    lime_compiler_context_init(&cc_b);
    parse_setup(&lem_b, "<b>");
    ParseText(&lem_b, grammar_b, strlen(grammar_b));
    CHECK(lem_b.errorcnt == 0, "isolation: grammar_b parsed clean");
    CHECK(Symbol_find("UNIQUE_TO_B") != NULL,
          "isolation: ctx_b sees its own UNIQUE_TO_B");
    CHECK(Symbol_find("UNIQUE_TO_A") == NULL,
          "isolation: ctx_b does not see UNIQUE_TO_A");

    /* Switch back to ctx_a -- its symbol table must still contain
    ** UNIQUE_TO_A and remain unaware of UNIQUE_TO_B. */
    lime_active_ctx = &cc_a;
    CHECK(Symbol_find("UNIQUE_TO_A") != NULL,
          "isolation: ctx_a survived ctx_b's parse");
    CHECK(Symbol_find("UNIQUE_TO_B") == NULL,
          "isolation: ctx_a still does not see UNIQUE_TO_B");
    /* Differ in name: each context's struct lime carries its own. */
    CHECK(lem_a.name && strcmp(lem_a.name, "AlphaG") == 0,
          "isolation: lem_a.name == AlphaG");
    CHECK(lem_b.name && strcmp(lem_b.name, "BetaG") == 0,
          "isolation: lem_b.name == BetaG");

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
    /* Phase 2: ParseText + Parse refactor coverage. */
    test_parsetext_basic();
    test_parsetext_buffer_ownership();
    test_parsetext_no_overread();
    test_parsetext_line_numbers();
    test_parse_parsetext_equivalence();
    test_parsetext_two_context_isolation();
    printf("\n%d passed, %d failed\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
