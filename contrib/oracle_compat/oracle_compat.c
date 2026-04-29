/*
** Oracle SQL Compatibility -- Extension Registration
**
** Registers the Oracle compatibility grammar extension with the Lime
** extension registry.  This file ties together the grammar modifications,
** metadata, and conflict resolution callbacks.
**
** Integration pattern:
**   1. oracle_compat_init() creates the GrammarModification array
**      describing Oracle-specific tokens and rules
**   2. oracle_compat_register() registers the extension with full metadata
**   3. When conflicts arise with the base PostgreSQL grammar (e.g.,
**      ROWNUM vs. a regular identifier), the fork-resolve strategy
**      tries both interpretations and picks the winner
**   4. oracle_compat_cleanup() releases all resources
**
** Usage:
**   ExtensionRegistry *reg = extension_registry_create();
**   // ... register postgres_base first ...
**   oracle_compat_register(reg);
**   extension_registry_check_dependencies(reg, &error);
*/

#include "extension.h"           /* GrammarModification, ConflictInfo, etc. */
#include "extension_registry.h"  /* GrammarExtensionMetadata, registry API */
#include "oracle_semantics.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Oracle-specific token definitions                                  */
/* ------------------------------------------------------------------ */

/*
** Token names added by the Oracle compatibility extension.
** These extend the base grammar's token space.
*/
static const char *oracle_token_names[] = {
    "ROWNUM",
    "ROWID",
    "SYSDATE",
    "SYSTIMESTAMP",
    "CONNECT",
    "PRIOR",
    "LEVEL",
    "NOCYCLE",
    "SIBLINGS",
    "DECODE",
    "NVL",
    "NVL2",
    "MINUS_SET",
    "DUAL",
    "CURRVAL",
    "NEXTVAL",
    "OUTER_JOIN_OP",
    NULL,
};

/* ------------------------------------------------------------------ */
/*  Grammar modification array                                         */
/* ------------------------------------------------------------------ */

/*
** Maximum number of modifications.  The actual count is computed
** at init time.
*/
#define MAX_ORACLE_MODS  64

static GrammarModification oracle_mod_storage[MAX_ORACLE_MODS];
static uint32_t oracle_mod_count = 0;

/*
** Helper to add a token modification.
*/
static void add_token_mod(const char *name, const char *desc) {
    if (oracle_mod_count >= MAX_ORACLE_MODS) return;
    GrammarModification *m = &oracle_mod_storage[oracle_mod_count++];
    m->type = MOD_ADD_TOKEN;
    m->description = desc;
    m->u.add_token.name = name;
    m->u.add_token.lexeme = NULL;
    m->u.add_token.token_code = -1;  /* auto-assign */
}

/*
** Helper to add a rule modification.
*/
static void add_rule_mod(const char *lhs, const char **rhs, int nrhs,
                         const char *code, const char *desc, int prec) {
    if (oracle_mod_count >= MAX_ORACLE_MODS) return;
    GrammarModification *m = &oracle_mod_storage[oracle_mod_count++];
    m->type = MOD_ADD_RULE;
    m->description = desc;
    m->u.add_rule.lhs = lhs;
    m->u.add_rule.rhs = rhs;
    m->u.add_rule.nrhs = nrhs;
    m->u.add_rule.code = code;
    m->u.add_rule.precedence = prec;
}

/*
** Helper to add a precedence modification.
*/
static void add_prec_mod(const char *symbol, int prec, int assoc,
                         const char *desc) {
    if (oracle_mod_count >= MAX_ORACLE_MODS) return;
    GrammarModification *m = &oracle_mod_storage[oracle_mod_count++];
    m->type = MOD_MODIFY_PRECEDENCE;
    m->description = desc;
    m->u.modify_prec.symbol = symbol;
    m->u.modify_prec.new_precedence = prec;
    m->u.modify_prec.new_assoc = assoc;
}

/* ------------------------------------------------------------------ */
/*  Rule RHS symbol arrays (static storage)                            */
/* ------------------------------------------------------------------ */

/* ROWNUM as expression */
static const char *rhs_rownum[] = { "ROWNUM", NULL };

/* ROWID as expression */
static const char *rhs_rowid[] = { "ROWID", NULL };

/* LEVEL as expression */
static const char *rhs_level[] = { "LEVEL", NULL };

/* SYSDATE as expression */
static const char *rhs_sysdate[] = { "SYSDATE", NULL };

/* SYSTIMESTAMP as expression */
static const char *rhs_systimestamp[] = { "SYSTIMESTAMP", NULL };

/* PRIOR expr */
static const char *rhs_prior_expr[] = { "PRIOR", "expr", NULL };

/* DECODE(...) */
static const char *rhs_decode[] = {
    "DECODE", "LPAREN", "expr", "COMMA", "decode_args", "RPAREN", NULL
};

/* NVL(expr, expr) */
static const char *rhs_nvl[] = {
    "NVL", "LPAREN", "expr", "COMMA", "expr", "RPAREN", NULL
};

/* NVL2(expr, expr, expr) */
static const char *rhs_nvl2[] = {
    "NVL2", "LPAREN", "expr", "COMMA", "expr", "COMMA", "expr", "RPAREN", NULL
};

/* CONNECT BY expr */
static const char *rhs_connect_by[] = { "CONNECT", "BY", "expr", NULL };

/* CONNECT BY NOCYCLE expr */
static const char *rhs_connect_by_nocycle[] = {
    "CONNECT", "BY", "NOCYCLE", "expr", NULL
};

/* START WITH expr */
static const char *rhs_start_with[] = { "START", "WITH", "expr", NULL };

/* sequence.CURRVAL */
static const char *rhs_seq_currval[] = { "IDENT", "DOT", "CURRVAL", NULL };

/* sequence.NEXTVAL */
static const char *rhs_seq_nextval[] = { "IDENT", "DOT", "NEXTVAL", NULL };

/* expr (+) */
static const char *rhs_outer_join[] = { "expr", "OUTER_JOIN_OP", NULL };

/* SELECT ... FROM DUAL */
static const char *rhs_select_dual[] = {
    "SELECT", "select_list", "FROM", "DUAL", NULL
};

/* MINUS set operator */
static const char *rhs_minus_set[] = {
    "select_stmt", "MINUS_SET", "select_stmt", NULL
};

/* ORDER SIBLINGS BY */
static const char *rhs_order_siblings[] = {
    "ORDER", "SIBLINGS", "BY", "order_list", NULL
};

/* ------------------------------------------------------------------ */
/*  Initialization: build the modification array                       */
/* ------------------------------------------------------------------ */

static bool oracle_mods_initialized = false;

static void oracle_init_modifications(void) {
    if (oracle_mods_initialized) return;
    oracle_mod_count = 0;

    /* -- Add Oracle-specific tokens -- */
    for (int i = 0; oracle_token_names[i] != NULL; i++) {
        char desc[128];
        snprintf(desc, sizeof(desc), "Oracle token: %s", oracle_token_names[i]);
        /* desc is a stack string, but add_token_mod only stores a pointer;
        ** in a real implementation these would be static strings or strdup'd.
        ** For this example, use static descriptions. */
        add_token_mod(oracle_token_names[i], "Oracle-specific token");
    }

    /* -- Add precedence for PRIOR -- */
    add_prec_mod("PRIOR", 90, 2 /* right */,
                 "PRIOR binds tightly in CONNECT BY clauses");

    /* -- Add Oracle-specific grammar rules -- */

    /* Pseudo-columns as expressions */
    add_rule_mod("expr", rhs_rownum, 1,
                 "{ A = oracle_make_rownum(pstate); }",
                 "ROWNUM pseudo-column", -1);

    add_rule_mod("expr", rhs_rowid, 1,
                 "{ A = oracle_make_rowid(pstate); }",
                 "ROWID pseudo-column", -1);

    add_rule_mod("expr", rhs_level, 1,
                 "{ A = oracle_make_level(pstate); }",
                 "LEVEL pseudo-column for CONNECT BY", -1);

    /* Date/time functions */
    add_rule_mod("expr", rhs_sysdate, 1,
                 "{ A = oracle_make_sysdate(pstate); }",
                 "SYSDATE function", -1);

    add_rule_mod("expr", rhs_systimestamp, 1,
                 "{ A = oracle_make_systimestamp(pstate); }",
                 "SYSTIMESTAMP function", -1);

    /* PRIOR operator */
    add_rule_mod("expr", rhs_prior_expr, 2,
                 "{ A = oracle_make_prior(pstate, E); }",
                 "PRIOR expression for hierarchical queries", -1);

    /* DECODE function */
    add_rule_mod("expr", rhs_decode, 6,
                 "{ A = oracle_make_decode(pstate, E, Args); }",
                 "DECODE() -- Oracle CASE equivalent", -1);

    /* NVL function */
    add_rule_mod("expr", rhs_nvl, 6,
                 "{ A = oracle_make_nvl(pstate, E1, E2); }",
                 "NVL() -- Oracle null handling", -1);

    /* NVL2 function */
    add_rule_mod("expr", rhs_nvl2, 8,
                 "{ A = oracle_make_nvl2(pstate, E1, E2, E3); }",
                 "NVL2() -- Oracle conditional null handling", -1);

    /* CONNECT BY clause */
    add_rule_mod("connect_by_clause", rhs_connect_by, 3,
                 "{ A = oracle_make_connect_by(pstate, E, false); }",
                 "CONNECT BY clause for hierarchical queries", -1);

    add_rule_mod("connect_by_clause", rhs_connect_by_nocycle, 4,
                 "{ A = oracle_make_connect_by(pstate, E, true); }",
                 "CONNECT BY NOCYCLE clause", -1);

    /* START WITH clause */
    add_rule_mod("start_with_clause", rhs_start_with, 3,
                 "{ A = E; }",
                 "START WITH clause for hierarchical queries", -1);

    /* Sequence references */
    add_rule_mod("expr", rhs_seq_currval, 3,
                 "{ A = oracle_make_seq_currval(pstate, S.sval); }",
                 "sequence.CURRVAL", -1);

    add_rule_mod("expr", rhs_seq_nextval, 3,
                 "{ A = oracle_make_seq_nextval(pstate, S.sval); }",
                 "sequence.NEXTVAL", -1);

    /* Outer join operator */
    add_rule_mod("outer_join_expr", rhs_outer_join, 2,
                 "{ A = oracle_make_outer_join(pstate, E); }",
                 "(+) outer join operator", -1);

    /* SELECT FROM DUAL */
    add_rule_mod("select_stmt", rhs_select_dual, 4,
                 "{ A = oracle_make_select_dual(pstate, L); }",
                 "SELECT ... FROM DUAL", -1);

    /* MINUS set operator */
    add_rule_mod("select_stmt", rhs_minus_set, 3,
                 "{ A = oracle_make_minus(pstate, L, R); }",
                 "MINUS set operator (Oracle EXCEPT)", -1);

    /* ORDER SIBLINGS BY */
    add_rule_mod("order_clause", rhs_order_siblings, 4,
                 "{ A = oracle_make_order_clause(pstate, L, true); }",
                 "ORDER SIBLINGS BY for hierarchical queries", -1);

    oracle_mods_initialized = true;
}

/* ------------------------------------------------------------------ */
/*  Extension get_modifications callback                               */
/* ------------------------------------------------------------------ */

static bool oracle_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out)
{
    (void)user_data;
    (void)base_snapshot;

    oracle_init_modifications();

    *mods_out = oracle_mod_storage;
    *nmods_out = oracle_mod_count;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Conflict resolution callback                                       */
/* ------------------------------------------------------------------ */

/*
** When Oracle tokens conflict with PostgreSQL tokens (e.g., CONNECT,
** START, LEVEL might be used as identifiers in PostgreSQL), prefer
** the Oracle interpretation when the oracle_compat extension is active.
**
** For more nuanced conflicts, the fork-resolve strategy is used.
*/
static ConflictResolution oracle_on_conflict(
    void *user_data,
    const ConflictInfo *info)
{
    (void)user_data;

    /*
    ** If the conflict is between our extension and the base grammar,
    ** and the conflicting item is one of our key tokens, prefer our
    ** interpretation.  The fork-resolve strategy will handle the rest.
    */
    if (info->new_mod != NULL &&
        info->new_mod->type == MOD_ADD_TOKEN) {
        const char *name = info->new_mod->u.add_token.name;

        /* Core Oracle tokens that should win over identifiers */
        if (strcmp(name, "ROWNUM") == 0 ||
            strcmp(name, "SYSDATE") == 0 ||
            strcmp(name, "DECODE") == 0 ||
            strcmp(name, "NVL") == 0 ||
            strcmp(name, "NVL2") == 0 ||
            strcmp(name, "ROWID") == 0 ||
            strcmp(name, "LEVEL") == 0) {
            return CONFLICT_USE_NEW;
        }
    }

    /* For other conflicts, defer to the disambiguation strategy */
    return CONFLICT_UNRESOLVED;
}

/* ------------------------------------------------------------------ */
/*  Cleanup callback                                                   */
/* ------------------------------------------------------------------ */

static void oracle_on_unload(void *user_data) {
    (void)user_data;
    oracle_mods_initialized = false;
    oracle_mod_count = 0;
}

/* ------------------------------------------------------------------ */
/*  Extension metadata                                                 */
/* ------------------------------------------------------------------ */

static const char *oracle_requires[] = { "postgres_base", NULL };

/*
** Full metadata for the Oracle compatibility extension.
**
** Strategy: FORK_RESOLVE -- when Oracle syntax conflicts with PostgreSQL
** (e.g., ROWNUM could be a table alias in PostgreSQL), the parser forks
** and tries both interpretations.  The winning interpretation is selected
** based on which parse succeeds without errors.
**
** Priority: 2 (lower than postgres_base at 1), meaning in a pure
** priority-based resolution, PostgreSQL syntax wins.  Fork-resolve
** overrides this by testing both.
**
** Policy: SEQUENTIAL -- Oracle modifications are applied after the
** base PostgreSQL grammar is established.
*/
static GrammarExtensionMetadata oracle_metadata = {
    .name = "oracle_compat",
    .version = "1.0.0",
    .strategy = DISAMBIG_FORK_RESOLVE,
    .priority = 2,
    .policy = EXEC_SEQUENTIAL,
    .oracle = NULL,
    .conflict_threshold = 0.3f,  /* Tolerate up to 30% conflicting mods */
    .requires = oracle_requires,
    .conflicts_with = NULL,
    .modifications = oracle_mod_storage,
    .nmodifications = 0,  /* Updated at registration time */
};

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Register the Oracle compatibility extension with an extension registry.
**
** The registry must already contain a "postgres_base" extension (or
** dependency checking will fail).
**
** Returns true on success, false on failure (e.g., already registered,
** allocation failure).
*/
bool oracle_compat_register(ExtensionRegistry *reg) {
    if (reg == NULL) return false;

    /* Build the modification array */
    oracle_init_modifications();
    oracle_metadata.nmodifications = oracle_mod_count;
    oracle_metadata.modifications = oracle_mod_storage;

    return extension_registry_register(reg, &oracle_metadata);
}

/*
** Register the Oracle compatibility extension using the internal
** extension registry API (register_extension + ExtensionInfo).
**
** This variant integrates with the lower-level extension system that
** supports get_modifications/on_conflict/on_unload callbacks.
*/
bool oracle_compat_register_ext(void *ext_registry, uint32_t *id_out) {
    /* Import the internal registry type */
    typedef struct ExtensionRegistry ExtReg;
    typedef struct {
        const char *name;
        const char *version;
        bool (*get_modifications)(void *, const struct ParserSnapshot *,
                                  GrammarModification **, uint32_t *);
        ConflictResolution (*on_conflict)(void *, const ConflictInfo *);
        void (*on_unload)(void *);
        void *user_data;
    } ExtInfo;

    ExtInfo info = {
        .name = "oracle_compat",
        .version = "1.0.0",
        .get_modifications = oracle_get_modifications,
        .on_conflict = oracle_on_conflict,
        .on_unload = oracle_on_unload,
        .user_data = NULL,
    };

    /* Use the external symbol if available */
    extern bool register_extension(void *, const void *, uint32_t *);
    return register_extension(ext_registry, &info, id_out);
}

/*
** Get the extension metadata (for inspection or testing).
*/
const GrammarExtensionMetadata *oracle_compat_get_metadata(void) {
    oracle_init_modifications();
    oracle_metadata.nmodifications = oracle_mod_count;
    return &oracle_metadata;
}

/*
** Clean up Oracle extension resources.
*/
void oracle_compat_cleanup(void) {
    oracle_on_unload(NULL);
}
