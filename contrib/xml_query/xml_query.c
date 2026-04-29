/*
** XML Query Extension Registration
**
** Registers XQuery and XPath as embedded language extensions for the
** Lime parser.  This module demonstrates how to use the extension
** registry and grammar context switching system to add first-class
** XQuery/XPath support to a SQL parser.
**
** The extension adds:
**   - xmlquery() function: SQL/XML standard function for XQuery
**   - xpath() function: SQL/XML standard function for XPath
**   - Grammar context switching from SQL to XQuery/XPath modes
**   - PASSING clause for binding SQL values to XQuery variables
**
** Usage:
**   #include "xml_query.h"
**
**   // Register with the extension registry
**   ExtensionRegistry *reg = extension_registry_create();
**   xml_query_extension_register(reg);
**
**   // Set up grammar context switching
**   GrammarContextStack *ctx = grammar_context_create(sql_snapshot);
**   xml_query_setup_context(ctx, xquery_snap, xpath_snap);
**
** Integration with SQL parser:
**
**   When the SQL parser encounters the token "xmlquery" or "xpath",
**   the grammar context stack switches to the appropriate XQuery or
**   XPath grammar.  The embedded expression is parsed using the
**   XQuery/XPath grammar until the closing parenthesis or PASSING
**   keyword is encountered, at which point context switches back to SQL.
**
**   Example SQL query:
**     SELECT xmlquery(
**       'for $x in /books/book
**        where $x/price > 10
**        return $x/title'
**       PASSING xml_column AS "doc"
**     ) FROM documents;
**
**   Parse flow:
**     SQL mode:  SELECT xmlquery(
**     XQuery mode: for $x in /books/book ...
**     SQL mode:  PASSING xml_column AS "doc" ) FROM documents;
*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "extension_registry.h"
#include "grammar_context.h"

/* ------------------------------------------------------------------ */
/*  Forward declarations for grammar modification types                */
/* ------------------------------------------------------------------ */

/* We use forward declarations to avoid pulling in the full extension.h
** header, which may have platform-specific threading deps. */
struct GrammarModification;

/* ------------------------------------------------------------------ */
/*  Extension metadata constants                                       */
/* ------------------------------------------------------------------ */

#define XML_QUERY_EXT_NAME    "xml_query"
#define XML_QUERY_EXT_VERSION "1.0.0"

/* Token codes for the SQL-side tokens added by this extension.
** Using -1 for auto-assignment. */
#define TK_XMLQUERY  -1
#define TK_XPATH_FN  -1
#define TK_PASSING   -1
#define TK_RETURNING -1

/* ------------------------------------------------------------------ */
/*  Extension registration                                             */
/* ------------------------------------------------------------------ */

/*
** Register the xml_query extension with the extension registry.
** This makes the extension discoverable and enables dependency
** tracking and conflict detection.
*/
bool xml_query_extension_register(ExtensionRegistry *reg) {
    if (reg == NULL) return false;

    GrammarExtensionMetadata meta = {
        .name = XML_QUERY_EXT_NAME,
        .version = XML_QUERY_EXT_VERSION,
        .strategy = DISAMBIG_PRIORITY,
        .priority = 50,  /* Medium priority */
        .policy = EXEC_SEQUENTIAL,
        .oracle = NULL,
        .conflict_threshold = 0.1f,
        .requires = NULL,         /* No dependencies */
        .conflicts_with = NULL,   /* No known conflicts */
        .modifications = NULL,
        .nmodifications = 0,
    };

    return extension_registry_register(reg, &meta);
}

/*
** Unregister the xml_query extension.
*/
bool xml_query_extension_unregister(ExtensionRegistry *reg) {
    if (reg == NULL) return false;
    return extension_registry_unregister(reg, XML_QUERY_EXT_NAME);
}

/* ------------------------------------------------------------------ */
/*  Grammar context setup                                              */
/* ------------------------------------------------------------------ */

/*
** Switch callback that handles the SQL <-> XQuery/XPath transitions.
** Logs context switches for debugging.
*/
static bool xml_query_switch_callback(GrammarMode prev_mode,
                                       GrammarMode new_mode,
                                       void *user_data) {
    (void)user_data;
    (void)prev_mode;
    (void)new_mode;
    /* In a real implementation, this would:
    ** 1. Save the current parser state (stack, lookahead)
    ** 2. Initialize the new grammar's parser state
    ** 3. Set up token mapping between grammars
    */
    return true;
}

/*
** Set up grammar context switching for XML query support.
** Registers XQuery and XPath modes with the context stack and
** installs the switch callback.
**
** Parameters:
**   stack          - Grammar context stack to configure
**   xquery_snap    - Snapshot for XQuery grammar (may be NULL to skip)
**   xpath_snap     - Snapshot for XPath grammar (may be NULL to skip)
*/
bool xml_query_setup_context(GrammarContextStack *stack,
                             ParserSnapshot *xquery_snap,
                             ParserSnapshot *xpath_snap) {
    if (stack == NULL) return false;

    bool ok = true;

    if (xquery_snap != NULL) {
        GrammarModeInfo xq_info = {
            .mode = MODE_XQUERY,
            .name = "xquery",
            .snapshot = xquery_snap,
            .trigger_token = -1,
            .trigger_lexeme = "xmlquery",
            .exit_token = -1,  /* Exit on bracket depth */
        };
        ok = ok && grammar_context_register_mode(stack, &xq_info);
    }

    if (xpath_snap != NULL) {
        GrammarModeInfo xp_info = {
            .mode = MODE_XPATH,
            .name = "xpath",
            .snapshot = xpath_snap,
            .trigger_token = -1,
            .trigger_lexeme = "xpath",
            .exit_token = -1,  /* Exit on bracket depth */
        };
        ok = ok && grammar_context_register_mode(stack, &xp_info);
    }

    grammar_context_set_switch_callback(stack, xml_query_switch_callback, NULL);

    return ok;
}

/* ------------------------------------------------------------------ */
/*  SQL/XML PASSING clause support                                     */
/* ------------------------------------------------------------------ */

/*
** Information about a single PASSING clause binding.
*/
typedef struct PassingBinding {
    char *sql_expr;       /* SQL expression text (e.g., "xml_column") */
    char *xquery_var;     /* XQuery variable name (e.g., "doc")       */
} PassingBinding;

typedef struct PassingClause {
    PassingBinding *bindings;
    uint32_t count;
    uint32_t capacity;
} PassingClause;

PassingClause *passing_clause_create(void) {
    PassingClause *pc = calloc(1, sizeof(PassingClause));
    if (pc == NULL) return NULL;
    pc->capacity = 4;
    pc->bindings = calloc(pc->capacity, sizeof(PassingBinding));
    if (pc->bindings == NULL) {
        free(pc);
        return NULL;
    }
    return pc;
}

void passing_clause_destroy(PassingClause *pc) {
    if (pc == NULL) return;
    for (uint32_t i = 0; i < pc->count; i++) {
        free(pc->bindings[i].sql_expr);
        free(pc->bindings[i].xquery_var);
    }
    free(pc->bindings);
    free(pc);
}

bool passing_clause_add(PassingClause *pc,
                        const char *sql_expr,
                        const char *xquery_var) {
    if (pc == NULL || sql_expr == NULL || xquery_var == NULL) return false;

    if (pc->count >= pc->capacity) {
        uint32_t new_cap = pc->capacity * 2;
        PassingBinding *p = realloc(pc->bindings, new_cap * sizeof(PassingBinding));
        if (p == NULL) return false;
        pc->bindings = p;
        pc->capacity = new_cap;
    }

    PassingBinding *b = &pc->bindings[pc->count];
    b->sql_expr = strdup(sql_expr);
    b->xquery_var = strdup(xquery_var);
    if (b->sql_expr == NULL || b->xquery_var == NULL) {
        free(b->sql_expr);
        free(b->xquery_var);
        return false;
    }

    pc->count++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Complete integration example                                       */
/* ------------------------------------------------------------------ */

/*
** Demonstrates the full integration pipeline:
** 1. Register extension with extension registry
** 2. Set up grammar context switching
** 3. Detect xmlquery/xpath boundaries
** 4. Parse embedded expressions
** 5. Handle PASSING clause
**
** This is a static demonstration function; in a real system these
** steps would be distributed across the SQL parser, tokenizer, and
** evaluation engine.
*/
void xml_query_demo(void) {
    printf("XML Query Extension Integration Demo\n");
    printf("====================================\n\n");

    /* 1. Extension registry */
    ExtensionRegistry *reg = extension_registry_create();
    if (reg == NULL) {
        printf("ERROR: Failed to create extension registry\n");
        return;
    }

    if (xml_query_extension_register(reg)) {
        printf("  Registered '%s' v%s\n", XML_QUERY_EXT_NAME, XML_QUERY_EXT_VERSION);
    }

    const GrammarExtensionMetadata *meta =
        extension_registry_find(reg, XML_QUERY_EXT_NAME);
    if (meta != NULL) {
        printf("  Found extension: name=%s, priority=%d, strategy=%d\n",
               meta->name, meta->priority, meta->strategy);
    }

    /* Validate dependencies (none in this case) */
    char *dep_error = NULL;
    if (extension_registry_check_dependencies(reg, &dep_error)) {
        printf("  Dependency check: PASSED\n");
    } else {
        printf("  Dependency check: FAILED - %s\n", dep_error);
        free(dep_error);
    }

    /* 2. PASSING clause example */
    PassingClause *pc = passing_clause_create();
    passing_clause_add(pc, "xml_column", "doc");
    passing_clause_add(pc, "10", "min_price");
    printf("\n  PASSING clause bindings:\n");
    for (uint32_t i = 0; i < pc->count; i++) {
        printf("    %s AS \"%s\"\n",
               pc->bindings[i].sql_expr, pc->bindings[i].xquery_var);
    }

    /* 3. Example SQL with embedded XQuery */
    printf("\n  Example SQL:\n");
    printf("    SELECT xmlquery(\n");
    printf("      'for $x in /books/book\n");
    printf("       where $x/price > $min_price\n");
    printf("       return $x/title'\n");
    printf("      PASSING xml_column AS \"doc\",\n");
    printf("              10 AS \"min_price\"\n");
    printf("    ) FROM documents;\n");

    printf("\n  Parse flow:\n");
    printf("    1. SQL parser: SELECT xmlquery(\n");
    printf("    2. Context switch: SQL -> XQuery\n");
    printf("    3. XQuery parser: for $x in /books/book ...\n");
    printf("    4. Context switch: XQuery -> SQL\n");
    printf("    5. SQL parser: PASSING xml_column AS \"doc\" ...\n");
    printf("    6. SQL parser: ) FROM documents;\n");

    /* Cleanup */
    passing_clause_destroy(pc);
    extension_registry_destroy(reg);

    printf("\n  Demo complete.\n");
}

/* ------------------------------------------------------------------ */
/*  Main (for standalone testing)                                      */
/* ------------------------------------------------------------------ */

#ifdef XML_QUERY_STANDALONE
int main(void) {
    xml_query_demo();
    return 0;
}
#endif
