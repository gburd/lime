/*
 * mongodb_helpers.h
 *    Helper function declarations for the MongoDB Lime grammar.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the MdbNode AST.
 */

#ifndef MONGODB_HELPERS_H
#define MONGODB_HELPERS_H

#include "mongodb_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- List helpers --- */
MdbNodeList *mdb_list_make1(MdbNode *item);
MdbNodeList *mdb_list_append(MdbNodeList *list, MdbNode *item);
int          mdb_list_length(MdbNodeList *list);
MdbNode     *mdb_list_first(MdbNodeList *list);

/* --- Literal constructors --- */
MdbNode *mdb_make_null(void);
MdbNode *mdb_make_bool(bool val);
MdbNode *mdb_make_int(int64_t val);
MdbNode *mdb_make_double(double val);
MdbNode *mdb_make_string(MdbString *s);
MdbNode *mdb_make_regex(MdbString *pattern, MdbString *flags);
MdbNode *mdb_make_object_id(MdbString *s);

/* --- Structural constructors --- */
MdbNode *mdb_make_document(MdbNodeList *pairs);
MdbNode *mdb_make_array(MdbNodeList *elems);
MdbNode *mdb_make_pair(MdbString *key, MdbNode *value);
MdbNode *mdb_make_field_path(MdbString *path);

/* --- Operator constructors --- */
MdbNode *mdb_make_query_op(MdbNodeType type, MdbString *field, MdbNode *operand);
MdbNode *mdb_make_logical_op(MdbNodeType type, MdbNode *operand);
MdbNode *mdb_make_update_op(MdbNodeType type, MdbNode *spec);
MdbNode *mdb_make_agg_stage(MdbNodeType type, MdbNode *spec);
MdbNode *mdb_make_agg_expr(MdbNodeType type, MdbNode *operand);

/* --- Top-level statement constructors --- */
MdbNode *mdb_make_find(MdbNode *query, MdbNode *projection);
MdbNode *mdb_make_update(MdbNode *query, MdbNode *update);
MdbNode *mdb_make_aggregate(MdbNode *pipeline);
MdbNode *mdb_make_pipeline(MdbNodeList *stages);

#ifdef __cplusplus
}
#endif

#endif /* MONGODB_HELPERS_H */
