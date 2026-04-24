/*
 * mongodb_helpers.c
 *    Helper functions for the MongoDB Lime grammar.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the MdbNode AST representing MongoDB query documents,
 * update documents, and aggregation pipelines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "mongodb_helpers.h"

/* ======================================================================
 * List helpers
 * ====================================================================== */

static MdbNodeCell *make_cell(MdbNode *data)
{
    MdbNodeCell *cell = (MdbNodeCell *)malloc(sizeof(MdbNodeCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

MdbNodeList *mdb_list_make1(MdbNode *item)
{
    MdbNodeList *list = (MdbNodeList *)malloc(sizeof(MdbNodeList));
    MdbNodeCell *cell = make_cell(item);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

MdbNodeList *mdb_list_append(MdbNodeList *list, MdbNode *item)
{
    if (!list)
        return mdb_list_make1(item);
    MdbNodeCell *cell = make_cell(item);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

int mdb_list_length(MdbNodeList *list)
{
    return list ? list->length : 0;
}

MdbNode *mdb_list_first(MdbNodeList *list)
{
    return (list && list->head) ? list->head->data : NULL;
}

/* ======================================================================
 * Internal allocation helper
 * ====================================================================== */

static MdbNode *alloc_node(MdbNodeType type)
{
    MdbNode *n = (MdbNode *)calloc(1, sizeof(MdbNode));
    n->type = type;
    return n;
}

/* ======================================================================
 * Literal constructors
 * ====================================================================== */

MdbNode *mdb_make_null(void)
{
    return alloc_node(MDB_NULL);
}

MdbNode *mdb_make_bool(bool val)
{
    MdbNode *n = alloc_node(MDB_BOOL);
    n->value.boolean = val;
    return n;
}

MdbNode *mdb_make_int(int64_t val)
{
    MdbNode *n = alloc_node(MDB_INT);
    n->value.integer = val;
    return n;
}

MdbNode *mdb_make_double(double val)
{
    MdbNode *n = alloc_node(MDB_DOUBLE);
    n->value.floating = val;
    return n;
}

MdbNode *mdb_make_string(MdbString *s)
{
    MdbNode *n = alloc_node(MDB_STRING);
    if (s) {
        n->value.string.val = s->val;
        n->value.string.len = s->len;
    }
    return n;
}

MdbNode *mdb_make_regex(MdbString *pattern, MdbString *flags)
{
    MdbNode *n = alloc_node(MDB_REGEX);
    if (pattern) {
        n->value.regex.pattern.val = pattern->val;
        n->value.regex.pattern.len = pattern->len;
    }
    if (flags) {
        n->value.regex.flags.val = flags->val;
        n->value.regex.flags.len = flags->len;
    }
    return n;
}

MdbNode *mdb_make_object_id(MdbString *s)
{
    MdbNode *n = alloc_node(MDB_OBJECT_ID);
    if (s) {
        n->value.string.val = s->val;
        n->value.string.len = s->len;
    }
    return n;
}

/* ======================================================================
 * Structural constructors
 * ====================================================================== */

MdbNode *mdb_make_document(MdbNodeList *pairs)
{
    MdbNode *n = alloc_node(MDB_DOCUMENT);
    n->value.elements = pairs;
    return n;
}

MdbNode *mdb_make_array(MdbNodeList *elems)
{
    MdbNode *n = alloc_node(MDB_ARRAY);
    n->value.elements = elems;
    return n;
}

MdbNode *mdb_make_pair(MdbString *key, MdbNode *value)
{
    MdbNode *n = alloc_node(MDB_PAIR);
    if (key) {
        n->value.pair.key.val = key->val;
        n->value.pair.key.len = key->len;
    }
    n->value.pair.value = value;
    return n;
}

MdbNode *mdb_make_field_path(MdbString *path)
{
    MdbNode *n = alloc_node(MDB_FIELD_PATH);
    if (path) {
        n->value.string.val = path->val;
        n->value.string.len = path->len;
    }
    return n;
}

/* ======================================================================
 * Operator constructors
 * ====================================================================== */

MdbNode *mdb_make_query_op(MdbNodeType type, MdbString *field, MdbNode *operand)
{
    MdbNode *n = alloc_node(type);
    if (field) {
        n->value.op.field.val = field->val;
        n->value.op.field.len = field->len;
    }
    n->value.op.operand = operand;
    return n;
}

MdbNode *mdb_make_logical_op(MdbNodeType type, MdbNode *operand)
{
    MdbNode *n = alloc_node(type);
    n->value.op.operand = operand;
    return n;
}

MdbNode *mdb_make_update_op(MdbNodeType type, MdbNode *spec)
{
    MdbNode *n = alloc_node(type);
    n->value.op.operand = spec;
    return n;
}

MdbNode *mdb_make_agg_stage(MdbNodeType type, MdbNode *spec)
{
    MdbNode *n = alloc_node(type);
    n->value.stage.spec = spec;
    return n;
}

MdbNode *mdb_make_agg_expr(MdbNodeType type, MdbNode *operand)
{
    MdbNode *n = alloc_node(type);
    n->value.op.operand = operand;
    return n;
}

/* ======================================================================
 * Top-level statement constructors
 * ====================================================================== */

MdbNode *mdb_make_find(MdbNode *query, MdbNode *projection)
{
    MdbNode *n = alloc_node(MDB_FIND);
    n->value.find.query = query;
    n->value.find.projection = projection;
    return n;
}

MdbNode *mdb_make_update(MdbNode *query, MdbNode *update)
{
    MdbNode *n = alloc_node(MDB_UPDATE);
    n->value.update.query = query;
    n->value.update.update = update;
    return n;
}

MdbNode *mdb_make_aggregate(MdbNode *pipeline)
{
    MdbNode *n = alloc_node(MDB_AGGREGATE);
    n->value.aggregate.pipeline = pipeline;
    return n;
}

MdbNode *mdb_make_pipeline(MdbNodeList *stages)
{
    MdbNode *n = alloc_node(MDB_PIPELINE);
    n->value.elements = stages;
    return n;
}
