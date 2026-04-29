/*
** MySQL SQL Compatibility -- Extension Registration
**
** Registers the MySQL compatibility grammar extension with the Lime
** extension registry.  This file ties together the grammar modifications,
** metadata, and conflict resolution callbacks.
**
** Integration pattern:
**   1. mysql_init_modifications() creates the GrammarModification array
**      describing MySQL-specific tokens and rules
**   2. mysql_compat_register() registers the extension with full metadata
**   3. When conflicts arise with the base PostgreSQL grammar (e.g.,
**      LIMIT is also a standard SQL keyword), the fork-resolve strategy
**      tries both interpretations
**   4. mysql_compat_cleanup() releases all resources
**
** Usage:
**   ExtensionRegistry *reg = extension_registry_create();
**   // ... register postgres_base first ...
**   mysql_compat_register(reg);
**   extension_registry_check_dependencies(reg, &error);
*/

#define _GNU_SOURCE
#include "../../src/extension.h"  /* GrammarModification, ConflictInfo, etc. */
#include "extension_registry.h"   /* GrammarExtensionMetadata, registry API */
#include "mysql_semantics.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  MySQL-specific token definitions                                    */
/* ------------------------------------------------------------------ */

static const char *mysql_token_names[] = {
    "BACKTICK_IDENT",
    "AUTO_INCREMENT",
    "ENGINE",
    "INNODB",
    "MYISAM",
    "MEMORY_ENGINE",
    "CHARSET",
    "COLLATE",
    "SHOW",
    "TABLES",
    "DATABASES",
    "COLUMNS",
    "INDEX",
    "STATUS",
    "VARIABLES",
    "GRANTS",
    "PROCESSLIST",
    "WARNINGS",
    "ERRORS",
    "IFNULL",
    "IF_KW",
    "DIV_KW",
    "NULL_SAFE_EQ",
    "UNSIGNED",
    "IF_NOT_EXISTS",
    "TEMPORARY",
    "GROUP_CONCAT",
    "SEPARATOR",
    "XOR",
    "REGEXP",
    "INTERVAL",
    "IGNORE",
    "HIGH_PRIORITY",
    "SQL_CALC_FOUND_ROWS",
    "USE_INDEX",
    "FORCE_INDEX",
    "ROW_FORMAT",
    "DUPLICATE",
    "UPDATE_KW",
    NULL,
};

/* ------------------------------------------------------------------ */
/*  Grammar modification array                                          */
/* ------------------------------------------------------------------ */

#define MAX_MYSQL_MODS  80

static GrammarModification mysql_mod_storage[MAX_MYSQL_MODS];
static uint32_t mysql_mod_count = 0;

static void add_token_mod(const char *name, const char *desc) {
    if (mysql_mod_count >= MAX_MYSQL_MODS) return;
    GrammarModification *m = &mysql_mod_storage[mysql_mod_count++];
    m->type = MOD_ADD_TOKEN;
    m->description = desc;
    m->u.add_token.name = name;
    m->u.add_token.lexeme = NULL;
    m->u.add_token.token_code = -1;
}

static void add_rule_mod(const char *lhs, const char **rhs, int nrhs,
                         const char *code, const char *desc, int prec) {
    if (mysql_mod_count >= MAX_MYSQL_MODS) return;
    GrammarModification *m = &mysql_mod_storage[mysql_mod_count++];
    m->type = MOD_ADD_RULE;
    m->description = desc;
    m->u.add_rule.lhs = lhs;
    m->u.add_rule.rhs = rhs;
    m->u.add_rule.nrhs = nrhs;
    m->u.add_rule.code = code;
    m->u.add_rule.precedence = prec;
}

static void add_prec_mod(const char *symbol, int prec, int assoc,
                         const char *desc) {
    if (mysql_mod_count >= MAX_MYSQL_MODS) return;
    GrammarModification *m = &mysql_mod_storage[mysql_mod_count++];
    m->type = MOD_MODIFY_PRECEDENCE;
    m->description = desc;
    m->u.modify_prec.symbol = symbol;
    m->u.modify_prec.new_precedence = prec;
    m->u.modify_prec.new_assoc = assoc;
}

/* ------------------------------------------------------------------ */
/*  Rule RHS symbol arrays                                              */
/* ------------------------------------------------------------------ */

/* LIMIT count */
static const char *rhs_limit[] = { "LIMIT", "expr", NULL };

/* LIMIT count OFFSET offset */
static const char *rhs_limit_offset[] = {
    "LIMIT", "expr", "OFFSET", "expr", NULL
};

/* LIMIT offset, count (MySQL shorthand) */
static const char *rhs_limit_comma[] = {
    "LIMIT", "expr", "COMMA", "expr", NULL
};

/* SHOW TABLES */
static const char *rhs_show_tables[] = { "SHOW", "TABLES", NULL };

/* SHOW DATABASES */
static const char *rhs_show_databases[] = { "SHOW", "DATABASES", NULL };

/* SHOW COLUMNS FROM table */
static const char *rhs_show_columns[] = {
    "SHOW", "COLUMNS", "FROM", "IDENT", NULL
};

/* SHOW INDEX FROM table */
static const char *rhs_show_index[] = {
    "SHOW", "INDEX", "FROM", "IDENT", NULL
};

/* SHOW STATUS */
static const char *rhs_show_status[] = { "SHOW", "STATUS", NULL };

/* SHOW VARIABLES */
static const char *rhs_show_variables[] = { "SHOW", "VARIABLES", NULL };

/* SHOW PROCESSLIST */
static const char *rhs_show_processlist[] = { "SHOW", "PROCESSLIST", NULL };

/* SHOW WARNINGS */
static const char *rhs_show_warnings[] = { "SHOW", "WARNINGS", NULL };

/* SHOW ERRORS */
static const char *rhs_show_errors[] = { "SHOW", "ERRORS", NULL };

/* IFNULL(expr, expr) */
static const char *rhs_ifnull[] = {
    "IFNULL", "LPAREN", "expr", "COMMA", "expr", "RPAREN", NULL
};

/* IF(cond, then, else) */
static const char *rhs_if_func[] = {
    "IF_KW", "LPAREN", "expr", "COMMA", "expr", "COMMA", "expr", "RPAREN",
    NULL
};

/* expr DIV expr */
static const char *rhs_div[] = { "expr", "DIV_KW", "expr", NULL };

/* expr <=> expr */
static const char *rhs_nullsafe_eq[] = { "expr", "NULL_SAFE_EQ", "expr", NULL };

/* expr XOR expr */
static const char *rhs_xor[] = { "expr", "XOR", "expr", NULL };

/* expr REGEXP expr */
static const char *rhs_regexp[] = { "expr", "REGEXP", "expr", NULL };

/* ON DUPLICATE KEY UPDATE assignments */
static const char *rhs_upsert[] = {
    "ON", "DUPLICATE", "KEY", "UPDATE_KW", "upsert_assigns", NULL
};

/* ------------------------------------------------------------------ */
/*  Build modification array                                            */
/* ------------------------------------------------------------------ */

static bool mysql_mods_initialized = false;

static void mysql_init_modifications(void) {
    if (mysql_mods_initialized) return;
    mysql_mod_count = 0;

    /* Add MySQL-specific tokens */
    for (int i = 0; mysql_token_names[i] != NULL; i++) {
        add_token_mod(mysql_token_names[i], "MySQL-specific token");
    }

    /* MySQL DIV integer division precedence */
    add_prec_mod("DIV_KW", 50, 1 /* left */,
                 "DIV integer division precedence");

    /* <=> null-safe equality precedence */
    add_prec_mod("NULL_SAFE_EQ", 30, 1 /* left */,
                 "Null-safe equality precedence");

    /* XOR precedence (between OR and AND in MySQL) */
    add_prec_mod("XOR", 15, 1 /* left */,
                 "XOR logical operator precedence");

    /* LIMIT clause rules */
    add_rule_mod("limit_clause", rhs_limit, 2,
                 "{ A = mysql_make_limit(pstate, E, NULL); }",
                 "LIMIT count", -1);

    add_rule_mod("limit_clause", rhs_limit_offset, 4,
                 "{ A = mysql_make_limit(pstate, E1, E2); }",
                 "LIMIT count OFFSET offset", -1);

    add_rule_mod("limit_clause", rhs_limit_comma, 4,
                 "{ A = mysql_make_limit(pstate, E2, E1); }",
                 "LIMIT offset, count (MySQL shorthand)", -1);

    /* SHOW statement rules */
    add_rule_mod("show_stmt", rhs_show_tables, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_TABLES, NULL, NULL); }",
                 "SHOW TABLES", -1);

    add_rule_mod("show_stmt", rhs_show_databases, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_DATABASES, NULL, NULL); }",
                 "SHOW DATABASES", -1);

    add_rule_mod("show_stmt", rhs_show_columns, 4,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_COLUMNS, NULL, T.sval); }",
                 "SHOW COLUMNS FROM table", -1);

    add_rule_mod("show_stmt", rhs_show_index, 4,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_INDEX, NULL, T.sval); }",
                 "SHOW INDEX FROM table", -1);

    add_rule_mod("show_stmt", rhs_show_status, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_STATUS, NULL, NULL); }",
                 "SHOW STATUS", -1);

    add_rule_mod("show_stmt", rhs_show_variables, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_VARIABLES, NULL, NULL); }",
                 "SHOW VARIABLES", -1);

    add_rule_mod("show_stmt", rhs_show_processlist, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_PROCESSLIST, NULL, NULL); }",
                 "SHOW PROCESSLIST", -1);

    add_rule_mod("show_stmt", rhs_show_warnings, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_WARNINGS, NULL, NULL); }",
                 "SHOW WARNINGS", -1);

    add_rule_mod("show_stmt", rhs_show_errors, 2,
                 "{ A = mysql_make_show(pstate, MYSQL_SHOW_ERRORS, NULL, NULL); }",
                 "SHOW ERRORS", -1);

    /* MySQL functions */
    add_rule_mod("expr", rhs_ifnull, 6,
                 "{ A = mysql_make_ifnull(pstate, E1, E2); }",
                 "IFNULL() null-coalescing function", -1);

    add_rule_mod("expr", rhs_if_func, 8,
                 "{ A = mysql_make_if_func(pstate, C, T, E); }",
                 "IF() conditional function", -1);

    /* MySQL-specific operators */
    add_rule_mod("expr", rhs_div, 3,
                 "{ A = mysql_make_binop(pstate, MYSQL_OP_INT_DIV, L, R); }",
                 "DIV integer division operator", -1);

    add_rule_mod("expr", rhs_nullsafe_eq, 3,
                 "{ A = mysql_make_binop(pstate, MYSQL_OP_NULL_SAFE_EQ, L, R); }",
                 "<=> null-safe equality operator", -1);

    add_rule_mod("expr", rhs_xor, 3,
                 "{ A = mysql_make_binop(pstate, MYSQL_OP_XOR, L, R); }",
                 "XOR logical operator", -1);

    add_rule_mod("expr", rhs_regexp, 3,
                 "{ A = mysql_make_binop(pstate, MYSQL_OP_REGEXP, L, R); }",
                 "REGEXP pattern matching operator", -1);

    /* ON DUPLICATE KEY UPDATE */
    add_rule_mod("upsert_clause", rhs_upsert, 5,
                 "{ A = mysql_make_upsert(pstate, assigns); }",
                 "ON DUPLICATE KEY UPDATE (UPSERT)", -1);

    mysql_mods_initialized = true;
}

/* ------------------------------------------------------------------ */
/*  Extension callbacks                                                 */
/* ------------------------------------------------------------------ */

static bool mysql_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out)
{
    (void)user_data;
    (void)base_snapshot;

    mysql_init_modifications();

    *mods_out = mysql_mod_storage;
    *nmods_out = mysql_mod_count;
    return true;
}

static ConflictResolution mysql_on_conflict(
    void *user_data,
    const ConflictInfo *info)
{
    (void)user_data;

    /*
    ** When MySQL tokens conflict with PostgreSQL tokens, prefer MySQL
    ** for our unique tokens but defer to fork-resolve for ambiguous ones.
    */
    if (info->new_mod != NULL &&
        info->new_mod->type == MOD_ADD_TOKEN) {
        const char *name = info->new_mod->u.add_token.name;

        /* Tokens unique to MySQL that should win */
        if (strcmp(name, "BACKTICK_IDENT") == 0 ||
            strcmp(name, "AUTO_INCREMENT") == 0 ||
            strcmp(name, "IFNULL") == 0 ||
            strcmp(name, "DIV_KW") == 0 ||
            strcmp(name, "NULL_SAFE_EQ") == 0 ||
            strcmp(name, "GROUP_CONCAT") == 0 ||
            strcmp(name, "UNSIGNED") == 0) {
            return CONFLICT_USE_NEW;
        }
    }

    /* For other conflicts, defer to the disambiguation strategy */
    return CONFLICT_UNRESOLVED;
}

static void mysql_on_unload(void *user_data) {
    (void)user_data;
    mysql_mods_initialized = false;
    mysql_mod_count = 0;
}

/* ------------------------------------------------------------------ */
/*  Extension metadata                                                  */
/* ------------------------------------------------------------------ */

static const char *mysql_requires[] = { "postgres_base", NULL };

/*
** Full metadata for the MySQL compatibility extension.
**
** Strategy: FORK_RESOLVE -- when MySQL syntax conflicts with PostgreSQL
** (e.g., LIMIT has different syntax in MySQL), the parser forks and
** tries both interpretations.
**
** Priority: 3 (lower than postgres_base=1 and oracle_compat=2).
** In priority-based resolution, PostgreSQL and Oracle syntax win.
**
** Policy: SEQUENTIAL -- MySQL modifications are applied after the
** base grammar.
*/
static GrammarExtensionMetadata mysql_metadata = {
    .name = "mysql_compat",
    .version = "1.0.0",
    .strategy = DISAMBIG_FORK_RESOLVE,
    .priority = 3,
    .policy = EXEC_SEQUENTIAL,
    .oracle = NULL,
    .conflict_threshold = 0.3f,
    .requires = mysql_requires,
    .conflicts_with = NULL,
    .modifications = mysql_mod_storage,
    .nmodifications = 0,
};

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

bool mysql_compat_register(ExtensionRegistry *reg) {
    if (reg == NULL) return false;

    mysql_init_modifications();
    mysql_metadata.nmodifications = mysql_mod_count;
    mysql_metadata.modifications = mysql_mod_storage;

    return extension_registry_register(reg, &mysql_metadata);
}

bool mysql_compat_register_ext(void *ext_registry, uint32_t *id_out) {
    ExtensionInfo info = {
        .name = "mysql_compat",
        .version = "1.0.0",
        .get_modifications = mysql_get_modifications,
        .on_conflict = mysql_on_conflict,
        .on_unload = mysql_on_unload,
        .user_data = NULL,
    };

    return register_extension((ExtensionRegistry *)ext_registry,
                              &info, id_out);
}

const GrammarExtensionMetadata *mysql_compat_get_metadata(void) {
    mysql_init_modifications();
    mysql_metadata.nmodifications = mysql_mod_count;
    return &mysql_metadata;
}

void mysql_compat_cleanup(void) {
    mysql_on_unload(NULL);
}
