/*
** DuckDB Features Extension - Registration
**
** Registers the DuckDB features grammar extension with the extension
** registry.  DuckDB is an analytics-oriented SQL dialect that adds
** LIST/STRUCT types, lambda functions, PIVOT/UNPIVOT, and other
** features to standard SQL.
**
** Unlike the EDN extension (which uses grammar context switching for
** embedded language support), DuckDB syntax integrates directly with
** SQL -- its features are extensions of SQL syntax rather than an
** embedded sub-language.  No context switching is required because
** DuckDB's additions (brackets for lists, braces for structs, arrow
** for lambdas) are recognized by the SQL lexer with minor token
** additions.
**
** The extension uses DISAMBIG_PRIORITY with priority 4, the same
** level as other SQL dialect extensions.
*/

#include "duckdb_semantics.h"

#include <stddef.h>
#include <string.h>

/* Pull in the extension registry API */
#include "extension_registry.h"

/* ------------------------------------------------------------------ */
/*  Grammar modification count                                         */
/*                                                                     */
/*  Approximate count of grammar productions added:                    */
/*    - 2 top-level input rules                                        */
/*    - 3 list literal (list_literal, empty, elements base/recursive)  */
/*    - 5 struct literal (struct_literal, empty, fields, field x2)     */
/*    - 3 lambda (expr, single-param, multi-param, params)             */
/*    - 9 list functions (transform, filter, reduce, contains,         */
/*      extract, sort, distinct, agg, flatten)                         */
/*    - 2 struct/subscript access                                      */
/*    - 3 exclude clause                                               */
/*    - 4 replace clause                                               */
/*    - 1 qualify clause                                               */
/*    - 6 sample clause variants                                       */
/*    - 6 pivot/unpivot (pivot, unpivot, col_list, value_list)         */
/*    - 2 columns expression                                           */
/*    - 16 standard expression rules                                   */
/*    - 2 function call + arg list                                     */
/* ------------------------------------------------------------------ */
#define DUCKDB_NUM_MODIFICATIONS  62

/* ------------------------------------------------------------------ */
/*  Extension metadata                                                 */
/* ------------------------------------------------------------------ */

static const char *duckdb_requires[] = {"sql_base", NULL};

static GrammarExtensionMetadata duckdb_metadata = {
    .name               = "duckdb_features",
    .version            = "1.0.0",
    .strategy           = DISAMBIG_PRIORITY,
    .priority           = 4,
    .policy             = EXEC_SEQUENTIAL,
    .oracle             = NULL,
    .conflict_threshold = 0.0f,
    .requires           = duckdb_requires,
    .conflicts_with     = NULL,
    .modifications      = NULL,   /* Loaded from compiled grammar */
    .nmodifications     = DUCKDB_NUM_MODIFICATIONS,
};

/* ------------------------------------------------------------------ */
/*  Extension registration                                             */
/* ------------------------------------------------------------------ */

/*
** Register the DuckDB features extension with the extension registry.
** Returns true on success, false if registration fails.
*/
bool duckdb_features_register(ExtensionRegistry *reg) {
    if (reg == NULL) return false;
    return extension_registry_register(reg, &duckdb_metadata);
}

/* ------------------------------------------------------------------ */
/*  DuckDB expression type name (for diagnostics)                      */
/* ------------------------------------------------------------------ */

const char *duckdb_expr_type_name(DuckdbExprType type) {
    switch (type) {
    case DUCK_EXPR_IDENT:           return "identifier";
    case DUCK_EXPR_QUALIFIED_IDENT: return "qualified_identifier";
    case DUCK_EXPR_ICONST:          return "integer";
    case DUCK_EXPR_FCONST:          return "float";
    case DUCK_EXPR_SCONST:          return "string";
    case DUCK_EXPR_NULL:            return "null";
    case DUCK_EXPR_BOOL:            return "boolean";
    case DUCK_EXPR_LIST:            return "list";
    case DUCK_EXPR_STRUCT:          return "struct";
    case DUCK_EXPR_LAMBDA:          return "lambda";
    case DUCK_EXPR_STRUCT_ACCESS:   return "struct_access";
    case DUCK_EXPR_LIST_TRANSFORM:  return "list_transform";
    case DUCK_EXPR_LIST_FILTER:     return "list_filter";
    case DUCK_EXPR_LIST_REDUCE:     return "list_reduce";
    case DUCK_EXPR_LIST_CONTAINS:   return "list_contains";
    case DUCK_EXPR_LIST_EXTRACT:    return "list_extract";
    case DUCK_EXPR_LIST_SORT:       return "list_sort";
    case DUCK_EXPR_LIST_DISTINCT:   return "list_distinct";
    case DUCK_EXPR_LIST_AGG:        return "list_agg";
    case DUCK_EXPR_FLATTEN:         return "flatten";
    case DUCK_EXPR_BINOP:           return "binary_op";
    case DUCK_EXPR_UNARYOP:         return "unary_op";
    case DUCK_EXPR_IS_NULL:         return "is_null";
    case DUCK_EXPR_FUNC_CALL:       return "function_call";
    case DUCK_EXPR_COLUMNS:         return "columns";
    case DUCK_EXPR_COLUMNS_REGEX:   return "columns_regex";
    case DUCK_EXPR_SUBSCRIPT:       return "subscript";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/*  DuckDB sample method name (for diagnostics)                        */
/* ------------------------------------------------------------------ */

const char *duckdb_sample_method_name(DuckdbSampleMethod method) {
    switch (method) {
    case DUCK_SAMPLE_PERCENT:   return "percent";
    case DUCK_SAMPLE_ROWS:      return "rows";
    case DUCK_SAMPLE_RESERVOIR: return "reservoir";
    case DUCK_SAMPLE_BERNOULLI: return "bernoulli";
    case DUCK_SAMPLE_SYSTEM:    return "system";
    }
    return "unknown";
}
