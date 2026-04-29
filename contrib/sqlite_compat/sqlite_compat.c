/* Enable POSIX types (pthread_rwlock_t) in strict C11 mode */
#define _GNU_SOURCE

/*
** SQLite Compatibility Grammar Extension - Registration
**
** Registers the SQLite SQL dialect as a grammar extension using the
** Lime extension system.  This extension adds SQLite-specific syntax
** (WITHOUT ROWID, AUTOINCREMENT, PRAGMA, UPSERT, JSON operators, etc.)
** on top of a base SQL grammar.
**
** The extension is designed to coexist with other SQL dialect extensions
** (PostgreSQL, Oracle, MySQL) through the disambiguation and execution
** policy systems.
**
** Usage (via extension registry with rich metadata):
**
**   ExtensionRegistry *reg = extension_registry_create();
**   extension_registry_register(reg, &sqlite_metadata);
**
** Usage (via basic extension system):
**
**   ExtensionRegistry *basic_reg = create_extension_registry();
**   ExtensionID id;
**   register_extension(basic_reg, &sqlite_extension_info, &id);
**   load_extension(basic_reg, id, base_snapshot, &error);
**
** Build as a shared library:
**
**   cc -shared -fPIC -o sqlite_compat.so sqlite_compat.c sqlite_semantics.c
**      -I../../include -I../../src
*/

#include "sqlite_semantics.h"

/*
** Internal extension API: GrammarModification, ExtensionInfo,
** ConflictResolution, etc.  We use the path relative to the src/
** directory to avoid picking up include/extension.h (the public stub).
*/
#include "../../src/extension.h"

/*
** Extension registry API types.  We use the GrammarExtensionMetadata
** from extension_registry.h for rich metadata registration.  However,
** including that header directly causes naming conflicts with the
** internal ExtensionRegistry struct from src/extension.h.  So we
** forward-declare only what we need for the metadata struct.
*/

/* Disambiguation strategies (from include/extension_registry.h) */
#ifndef DISAMBIG_FORK_RESOLVE
#define DISAMBIG_FORK_RESOLVE 1
#endif

/* Execution policies (from include/extension_registry.h) */
#ifndef EXEC_SEQUENTIAL
#define EXEC_SEQUENTIAL 0
#endif

/* Forward declare the rich registry types.  When the full extension
** registry header is available, these will be type-compatible.  */
typedef struct SqliteExtMetadata {
    const char *name;
    const char *version;
    int strategy;
    int priority;
    int policy;
    void *oracle;
    float conflict_threshold;
    const char **requires;
    const char **conflicts_with;
    GrammarModification *modifications;
    uint32_t nmodifications;
} SqliteExtMetadata;

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/*  Step 1: Define the grammar modifications                           */
/*                                                                     */
/*  Each modification maps to a SQLite-specific grammar rule or token  */
/*  defined in sqlite_grammar.lime.                                    */
/* ================================================================== */

/* RHS symbol arrays for production rules */
static const char *rhs_create_without_rowid[] = {
    "CREATE", "TABLE", "table_name", "LPAREN", "column_def_list",
    "RPAREN", "WITHOUT", "ROWID", "SEMICOLON", NULL
};
static const char *rhs_create_strict[] = {
    "CREATE", "TABLE", "table_name", "LPAREN", "column_def_list",
    "RPAREN", "STRICT", "SEMICOLON", NULL
};
static const char *rhs_autoincrement[] = {
    "PRIMARY", "KEY", "AUTOINCREMENT", NULL
};
static const char *rhs_insert_upsert[] = {
    "INSERT", "INTO", "table_name", "opt_column_list", "VALUES",
    "value_list", "ON", "CONFLICT", "conflict_target", "DO",
    "UPDATE", "SET", "update_set_list", "SEMICOLON", NULL
};
static const char *rhs_insert_upsert_nothing[] = {
    "INSERT", "INTO", "table_name", "opt_column_list", "VALUES",
    "value_list", "ON", "CONFLICT", "conflict_target", "DO",
    "NOTHING", "SEMICOLON", NULL
};
static const char *rhs_insert_or_replace[] = {
    "INSERT", "OR", "REPLACE", "INTO", "table_name",
    "opt_column_list", "VALUES", "value_list", "SEMICOLON", NULL
};
static const char *rhs_insert_or_ignore[] = {
    "INSERT", "OR", "IGNORE", "INTO", "table_name",
    "opt_column_list", "VALUES", "value_list", "SEMICOLON", NULL
};
static const char *rhs_pragma_get[] = {
    "PRAGMA", "IDENT", "SEMICOLON", NULL
};
static const char *rhs_pragma_set[] = {
    "PRAGMA", "IDENT", "EQ", "pragma_value", "SEMICOLON", NULL
};
static const char *rhs_pragma_call[] = {
    "PRAGMA", "IDENT", "LPAREN", "pragma_value", "RPAREN",
    "SEMICOLON", NULL
};
static const char *rhs_json_arrow[] = {
    "expr", "ARROW", "expr", NULL
};
static const char *rhs_json_darrow[] = {
    "expr", "DARROW", "expr", NULL
};
static const char *rhs_json_extract[] = {
    "JSON_EXTRACT", "LPAREN", "expr_list", "RPAREN", NULL
};
static const char *rhs_json_array[] = {
    "JSON_ARRAY", "LPAREN", "opt_expr_list", "RPAREN", NULL
};
static const char *rhs_json_object[] = {
    "JSON_OBJECT", "LPAREN", "opt_expr_list", "RPAREN", NULL
};
static const char *rhs_glob[] = {
    "expr", "GLOB", "expr", NULL
};
static const char *rhs_not_glob[] = {
    "expr", "NOT", "GLOB", "expr", NULL
};
static const char *rhs_vacuum[] = {
    "VACUUM", "SEMICOLON", NULL
};
static const char *rhs_vacuum_into[] = {
    "VACUUM", "INTO", "SCONST", "SEMICOLON", NULL
};
static const char *rhs_attach[] = {
    "ATTACH", "DATABASE", "expr", "AS", "IDENT", "SEMICOLON", NULL
};
static const char *rhs_detach[] = {
    "DETACH", "DATABASE", "IDENT", "SEMICOLON", NULL
};
static const char *rhs_explain[] = {
    "EXPLAIN", "sql_stmt", NULL
};
static const char *rhs_explain_qp[] = {
    "EXPLAIN", "QUERY", "PLAN", "sql_stmt", NULL
};
static const char *rhs_indexed_by[] = {
    "table_name", "INDEXED", "BY", "IDENT", NULL
};
static const char *rhs_not_indexed[] = {
    "table_name", "NOT", "INDEXED", NULL
};
static const char *rhs_isnull[] = {
    "expr", "ISNULL", NULL
};
static const char *rhs_notnull[] = {
    "expr", "NOTNULL", NULL
};
static const char *rhs_cast[] = {
    "CAST", "LPAREN", "expr", "AS", "type_name", "RPAREN", NULL
};
static const char *rhs_reindex[] = {
    "REINDEX", "SEMICOLON", NULL
};
static const char *rhs_reindex_name[] = {
    "REINDEX", "IDENT", "SEMICOLON", NULL
};

/*
** The modifications array.  Organized by feature area.
*/
static GrammarModification sqlite_mods[] = {
    /* ================================================================
    ** New tokens
    ** ================================================================ */
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite AUTOINCREMENT keyword",
        .u.add_token = { .name = "AUTOINCREMENT", .lexeme = "AUTOINCREMENT", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite WITHOUT keyword",
        .u.add_token = { .name = "WITHOUT", .lexeme = "WITHOUT", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite ROWID keyword",
        .u.add_token = { .name = "ROWID", .lexeme = "ROWID", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite PRAGMA keyword",
        .u.add_token = { .name = "PRAGMA", .lexeme = "PRAGMA", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite VACUUM keyword",
        .u.add_token = { .name = "VACUUM", .lexeme = "VACUUM", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite ATTACH keyword",
        .u.add_token = { .name = "ATTACH", .lexeme = "ATTACH", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite DETACH keyword",
        .u.add_token = { .name = "DETACH", .lexeme = "DETACH", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite EXPLAIN keyword",
        .u.add_token = { .name = "EXPLAIN", .lexeme = "EXPLAIN", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite GLOB keyword/operator",
        .u.add_token = { .name = "GLOB", .lexeme = "GLOB", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite INDEXED keyword",
        .u.add_token = { .name = "INDEXED", .lexeme = "INDEXED", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite STRICT keyword",
        .u.add_token = { .name = "STRICT", .lexeme = "STRICT", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite CONFLICT keyword",
        .u.add_token = { .name = "CONFLICT", .lexeme = "CONFLICT", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite NOTHING keyword (for DO NOTHING)",
        .u.add_token = { .name = "NOTHING", .lexeme = "NOTHING", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite EXCLUDED keyword (for upsert references)",
        .u.add_token = { .name = "EXCLUDED", .lexeme = "EXCLUDED", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite ISNULL postfix operator",
        .u.add_token = { .name = "ISNULL", .lexeme = "ISNULL", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite NOTNULL postfix operator",
        .u.add_token = { .name = "NOTNULL", .lexeme = "NOTNULL", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite REINDEX keyword",
        .u.add_token = { .name = "REINDEX", .lexeme = "REINDEX", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSON arrow operator (->)",
        .u.add_token = { .name = "ARROW", .lexeme = "->", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSON double-arrow operator (->>)",
        .u.add_token = { .name = "DARROW", .lexeme = "->>", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite JSON_EXTRACT function token",
        .u.add_token = { .name = "JSON_EXTRACT", .lexeme = "json_extract", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite JSON_ARRAY function token",
        .u.add_token = { .name = "JSON_ARRAY", .lexeme = "json_array", .token_code = -1 },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "SQLite JSON_OBJECT function token",
        .u.add_token = { .name = "JSON_OBJECT", .lexeme = "json_object", .token_code = -1 },
    },

    /* ================================================================
    ** Production rules: WITHOUT ROWID / STRICT
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "CREATE TABLE ... WITHOUT ROWID",
        .u.add_rule = {
            .lhs = "create_table_stmt", .rhs = rhs_create_without_rowid,
            .nrhs = 9,
            .code = "{ sqlite_create_table(pstate, 0, T, C, SQLITE_TBL_WITHOUT_ROWID); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "CREATE TABLE ... STRICT",
        .u.add_rule = {
            .lhs = "create_table_stmt", .rhs = rhs_create_strict,
            .nrhs = 8,
            .code = "{ sqlite_create_table(pstate, 0, T, C, SQLITE_TBL_STRICT); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: AUTOINCREMENT
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "column_constraint: PRIMARY KEY AUTOINCREMENT",
        .u.add_rule = {
            .lhs = "column_constraint", .rhs = rhs_autoincrement,
            .nrhs = 3,
            .code = "{ sqlite_set_autoincrement(pstate); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: UPSERT (ON CONFLICT)
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "INSERT ... ON CONFLICT DO UPDATE",
        .u.add_rule = {
            .lhs = "insert_stmt", .rhs = rhs_insert_upsert,
            .nrhs = 14,
            .code = "{ sqlite_insert(pstate, 0, T, C, V, U); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "INSERT ... ON CONFLICT DO NOTHING",
        .u.add_rule = {
            .lhs = "insert_stmt", .rhs = rhs_insert_upsert_nothing,
            .nrhs = 12,
            .code = "{ sqlite_insert(pstate, 0, T, C, V, NULL); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "INSERT OR REPLACE INTO ...",
        .u.add_rule = {
            .lhs = "insert_stmt", .rhs = rhs_insert_or_replace,
            .nrhs = 9,
            .code = "{ sqlite_insert(pstate, SQLITE_CONFLICT_REPLACE, T, C, V, NULL); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "INSERT OR IGNORE INTO ...",
        .u.add_rule = {
            .lhs = "insert_stmt", .rhs = rhs_insert_or_ignore,
            .nrhs = 9,
            .code = "{ sqlite_insert(pstate, SQLITE_CONFLICT_IGNORE, T, C, V, NULL); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: PRAGMA
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "PRAGMA name;",
        .u.add_rule = {
            .lhs = "pragma_stmt", .rhs = rhs_pragma_get,
            .nrhs = 3,
            .code = "{ sqlite_pragma_get(pstate, N); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "PRAGMA name = value;",
        .u.add_rule = {
            .lhs = "pragma_stmt", .rhs = rhs_pragma_set,
            .nrhs = 5,
            .code = "{ sqlite_pragma_set(pstate, N, V); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "PRAGMA name(value);",
        .u.add_rule = {
            .lhs = "pragma_stmt", .rhs = rhs_pragma_call,
            .nrhs = 6,
            .code = "{ sqlite_pragma_call(pstate, N, V); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: JSON operators and functions
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "expr -> expr ARROW expr (JSON extract)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_json_arrow,
            .nrhs = 3,
            .code = "{ A = sqlite_json_extract(pstate, X, Y); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr -> expr DARROW expr (JSON extract text)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_json_darrow,
            .nrhs = 3,
            .code = "{ A = sqlite_json_extract_text(pstate, X, Y); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr -> json_extract(expr_list)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_json_extract,
            .nrhs = 4,
            .code = "{ A = sqlite_json_func(pstate, \"json_extract\", L); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr -> json_array(opt_expr_list)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_json_array,
            .nrhs = 4,
            .code = "{ A = sqlite_json_func(pstate, \"json_array\", L); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr -> json_object(opt_expr_list)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_json_object,
            .nrhs = 4,
            .code = "{ A = sqlite_json_func(pstate, \"json_object\", L); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: GLOB operator
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "expr GLOB expr",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_glob,
            .nrhs = 3,
            .code = "{ A = sqlite_glob(pstate, X, Y); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr NOT GLOB expr",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_not_glob,
            .nrhs = 4,
            .code = "{ A = sqlite_not_glob(pstate, X, Y); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: VACUUM
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "VACUUM;",
        .u.add_rule = {
            .lhs = "vacuum_stmt", .rhs = rhs_vacuum,
            .nrhs = 2,
            .code = "{ sqlite_vacuum(pstate, (SqliteToken){0}, (SqliteToken){0}); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "VACUUM INTO filename;",
        .u.add_rule = {
            .lhs = "vacuum_stmt", .rhs = rhs_vacuum_into,
            .nrhs = 4,
            .code = "{ sqlite_vacuum(pstate, (SqliteToken){0}, F); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: ATTACH / DETACH
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "ATTACH DATABASE expr AS name;",
        .u.add_rule = {
            .lhs = "attach_stmt", .rhs = rhs_attach,
            .nrhs = 6,
            .code = "{ sqlite_attach(pstate, F, N); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "DETACH DATABASE name;",
        .u.add_rule = {
            .lhs = "detach_stmt", .rhs = rhs_detach,
            .nrhs = 4,
            .code = "{ sqlite_detach(pstate, N); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: EXPLAIN
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "EXPLAIN stmt",
        .u.add_rule = {
            .lhs = "explain_stmt", .rhs = rhs_explain,
            .nrhs = 2,
            .code = "{ sqlite_explain(pstate, S, 0); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "EXPLAIN QUERY PLAN stmt",
        .u.add_rule = {
            .lhs = "explain_stmt", .rhs = rhs_explain_qp,
            .nrhs = 4,
            .code = "{ sqlite_explain(pstate, S, 1); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: INDEXED BY / NOT INDEXED
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "from_item: table INDEXED BY index",
        .u.add_rule = {
            .lhs = "from_item", .rhs = rhs_indexed_by,
            .nrhs = 4,
            .code = "{ A = sqlite_indexed_by(pstate, T, I); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "from_item: table NOT INDEXED",
        .u.add_rule = {
            .lhs = "from_item", .rhs = rhs_not_indexed,
            .nrhs = 3,
            .code = "{ A = sqlite_not_indexed(pstate, T); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: ISNULL / NOTNULL / CAST
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "expr ISNULL",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_isnull,
            .nrhs = 2,
            .code = "{ A = sqlite_isnull(pstate, X); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "expr NOTNULL",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_notnull,
            .nrhs = 2,
            .code = "{ A = sqlite_notnull(pstate, X); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "CAST(expr AS type_name)",
        .u.add_rule = {
            .lhs = "expr", .rhs = rhs_cast,
            .nrhs = 6,
            .code = "{ A = sqlite_cast(pstate, X, T); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Production rules: REINDEX
    ** ================================================================ */
    {
        .type = MOD_ADD_RULE,
        .description = "REINDEX;",
        .u.add_rule = {
            .lhs = "reindex_stmt", .rhs = rhs_reindex,
            .nrhs = 2,
            .code = "{ sqlite_reindex(pstate, (SqliteToken){0}); }",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "REINDEX name;",
        .u.add_rule = {
            .lhs = "reindex_stmt", .rhs = rhs_reindex_name,
            .nrhs = 3,
            .code = "{ sqlite_reindex(pstate, N); }",
            .precedence = -1,
        },
    },

    /* ================================================================
    ** Precedence modifications
    **
    ** JSON arrow operators should bind tighter than comparison
    ** operators but looser than arithmetic.
    ** ================================================================ */
    {
        .type = MOD_MODIFY_PRECEDENCE,
        .description = "Set ARROW precedence (left, level 5)",
        .u.modify_prec = {
            .symbol = "ARROW",
            .new_precedence = 5,
            .new_assoc = 1,  /* left */
        },
    },
    {
        .type = MOD_MODIFY_PRECEDENCE,
        .description = "Set DARROW precedence (left, level 5)",
        .u.modify_prec = {
            .symbol = "DARROW",
            .new_precedence = 5,
            .new_assoc = 1,  /* left */
        },
    },
    {
        .type = MOD_MODIFY_PRECEDENCE,
        .description = "Set GLOB precedence (left, level 3, same as LIKE)",
        .u.modify_prec = {
            .symbol = "GLOB",
            .new_precedence = 3,
            .new_assoc = 1,  /* left */
        },
    },
};

#define SQLITE_MOD_COUNT (sizeof(sqlite_mods) / sizeof(sqlite_mods[0]))

/* ================================================================== */
/*  Step 2: Extension callbacks                                        */
/* ================================================================== */

/*
** Return the modifications array to the extension system.
*/
static bool sqlite_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    (void)user_data;
    (void)base_snapshot;

    *mods_out  = sqlite_mods;
    *nmods_out = (uint32_t)SQLITE_MOD_COUNT;
    return true;
}

/*
** Conflict resolution callback.
**
** SQLite is conservative: if another extension already registered
** a conflicting token or rule, keep the existing one.  The ARROW
** and DARROW operators may conflict with PostgreSQL's JSONB operators;
** in that case, the earlier-loaded extension wins.
*/
static ConflictResolution sqlite_on_conflict(
    void *user_data,
    const ConflictInfo *info
) {
    (void)user_data;
    (void)info;
    return CONFLICT_KEEP_EXISTING;
}

/*
** Cleanup callback.  Nothing to free since modifications are static.
*/
static void sqlite_on_unload(void *user_data) {
    (void)user_data;
}

/* ================================================================== */
/*  Step 3: Extension descriptors                                      */
/* ================================================================== */

/*
** Basic extension info (for the ExtensionRegistry from src/extension.h).
*/
const ExtensionInfo sqlite_extension_info = {
    .name               = "sqlite_compat",
    .version            = "1.0.0",
    .get_modifications  = sqlite_get_modifications,
    .on_conflict        = sqlite_on_conflict,
    .on_unload          = sqlite_on_unload,
    .user_data          = NULL,
};

/*
** Rich metadata (for the ExtensionRegistry from include/extension_registry.h).
** This includes disambiguation strategy, execution policy, dependency
** declarations, and conflict thresholds.
*/
static const char *sqlite_requires[] = { "sql_base", NULL };

static SqliteExtMetadata sqlite_reg_metadata = {
    .name               = "sqlite_compat",
    .version            = "1.0.0",
    .strategy           = DISAMBIG_FORK_RESOLVE,
    .priority           = 3,     /* After Postgres (1) and Oracle (2) */
    .policy             = EXEC_SEQUENTIAL,
    .oracle             = NULL,
    .conflict_threshold = 0.0,   /* No conflicts tolerated */
    .requires           = sqlite_requires,
    .conflicts_with     = NULL,
    .modifications      = sqlite_mods,
    .nmodifications     = sizeof(sqlite_mods) / sizeof(sqlite_mods[0]),
};

/* ================================================================== */
/*  Step 4: Public registration functions                              */
/* ================================================================== */

/*
** Register and load the SQLite extension via the basic API.
**
** Returns the assigned ExtensionID on success, or 0 on failure.
*/
ExtensionID sqlite_extension_register_and_load(
    ExtensionRegistry *reg,
    const struct ParserSnapshot *base_snapshot,
    char **error_out
) {
    ExtensionID id = 0;

    if (!register_extension(reg, &sqlite_extension_info, &id)) {
        if (error_out) {
            const char msg[] = "sqlite_compat: failed to register extension";
            *error_out = malloc(sizeof(msg));
            if (*error_out) memcpy(*error_out, msg, sizeof(msg));
        }
        return 0;
    }

    char *load_error = NULL;
    if (!load_extension(reg, id, base_snapshot, &load_error)) {
        if (error_out) {
            *error_out = load_error;
        } else {
            free(load_error);
        }
        return 0;
    }

    if (error_out) *error_out = NULL;
    return id;
}

/*
** Get the rich extension metadata for registration with the
** extension_registry system.  The caller should cast this to
** GrammarExtensionMetadata* when calling extension_registry_register().
**
** Returns a pointer to the static metadata struct.
*/
const SqliteExtMetadata *sqlite_extension_get_metadata(void) {
    return &sqlite_reg_metadata;
}

/*
** Return the modification count (useful for testing).
*/
uint32_t sqlite_extension_mod_count(void) {
    return (uint32_t)SQLITE_MOD_COUNT;
}
