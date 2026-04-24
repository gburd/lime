/*
 * jsonpath_helpers.c
 *    Helper functions for the JSONPath Lime grammar.
 *
 * These functions were originally in the epilogue of jsonpath_gram.y.
 * They are used by the Lime-generated parser's reduction actions to
 * build the JsonPathParseItem AST.
 *
 * Original Copyright (c) 2019-2026, PostgreSQL Global Development Group
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "jsonpath_helpers.h"

/* ======================================================================
 * List helpers (replacing PostgreSQL's List API)
 * ====================================================================== */

static JsonPathListCell *make_cell(void *data)
{
    JsonPathListCell *cell = (JsonPathListCell *)malloc(sizeof(JsonPathListCell));
    cell->data = data;
    cell->next = NULL;
    return cell;
}

JsonPathList *jp_list_make1(void *item)
{
    JsonPathList *list = (JsonPathList *)malloc(sizeof(JsonPathList));
    JsonPathListCell *cell = make_cell(item);
    list->length = 1;
    list->head = cell;
    list->tail = cell;
    return list;
}

JsonPathList *jp_list_make2(void *item1, void *item2)
{
    JsonPathList *list = jp_list_make1(item1);
    return jp_list_append(list, item2);
}

JsonPathList *jp_list_append(JsonPathList *list, void *item)
{
    if (!list)
        return jp_list_make1(item);
    JsonPathListCell *cell = make_cell(item);
    list->tail->next = cell;
    list->tail = cell;
    list->length++;
    return list;
}

int jp_list_length(JsonPathList *list)
{
    return list ? list->length : 0;
}

void *jp_list_first(JsonPathList *list)
{
    return (list && list->head) ? list->head->data : NULL;
}

void *jp_list_second(JsonPathList *list)
{
    return (list && list->head && list->head->next) ? list->head->next->data : NULL;
}

void jp_list_free(JsonPathList *list)
{
    if (!list) return;
    JsonPathListCell *cell = list->head;
    while (cell) {
        JsonPathListCell *next = cell->next;
        free(cell);
        cell = next;
    }
    free(list);
}

/* ======================================================================
 * AST construction helpers (from jsonpath_gram.y epilogue)
 * ====================================================================== */

JsonPathParseItem *
makeItemType(JsonPathItemType type)
{
    JsonPathParseItem *v = (JsonPathParseItem *)calloc(1, sizeof(JsonPathParseItem));
    v->type = type;
    v->next = NULL;
    return v;
}

JsonPathParseItem *
makeItemString(JsonPathString *s)
{
    JsonPathParseItem *v;

    if (s == NULL) {
        v = makeItemType(jpiNull);
    } else {
        v = makeItemType(jpiString);
        v->value.string.val = s->val;
        v->value.string.len = s->len;
    }
    return v;
}

JsonPathParseItem *
makeItemVariable(JsonPathString *s)
{
    JsonPathParseItem *v = makeItemType(jpiVariable);
    v->value.string.val = s->val;
    v->value.string.len = s->len;
    return v;
}

JsonPathParseItem *
makeItemKey(JsonPathString *s)
{
    JsonPathParseItem *v = makeItemString(s);
    v->type = jpiKey;
    return v;
}

JsonPathParseItem *
makeItemNumeric(JsonPathString *s)
{
    JsonPathParseItem *v = makeItemType(jpiNumeric);
    /* In standalone mode, store as string (no PostgreSQL Numeric type) */
    v->value.numeric = s->val;
    return v;
}

JsonPathParseItem *
makeItemBool(bool val)
{
    JsonPathParseItem *v = makeItemType(jpiBool);
    v->value.boolean = val;
    return v;
}

JsonPathParseItem *
makeItemBinary(JsonPathItemType type, JsonPathParseItem *la, JsonPathParseItem *ra)
{
    JsonPathParseItem *v = makeItemType(type);
    v->value.args.left = la;
    v->value.args.right = ra;
    return v;
}

JsonPathParseItem *
makeItemUnary(JsonPathItemType type, JsonPathParseItem *a)
{
    JsonPathParseItem *v;

    /* Optimization: unary plus on numeric is identity */
    if (type == jpiPlus && a && a->type == jpiNumeric && !a->next)
        return a;

    /* Optimization: unary minus on numeric folds into the literal */
    if (type == jpiMinus && a && a->type == jpiNumeric && !a->next) {
        v = makeItemType(jpiNumeric);
        /* Prepend '-' to the numeric string */
        int len = (int)strlen(a->value.numeric);
        char *neg = (char *)malloc(len + 2);
        neg[0] = '-';
        memcpy(neg + 1, a->value.numeric, len + 1);
        v->value.numeric = neg;
        return v;
    }

    v = makeItemType(type);
    v->value.arg = a;
    return v;
}

JsonPathParseItem *
makeItemList(JsonPathList *list)
{
    JsonPathParseItem *head, *end;
    JsonPathListCell *cell;

    if (!list || !list->head)
        return NULL;

    head = end = (JsonPathParseItem *)list->head->data;

    if (list->length == 1)
        return head;

    /* Append items to the end of already existing chain */
    while (end->next)
        end = end->next;

    for (cell = list->head->next; cell; cell = cell->next) {
        JsonPathParseItem *c = (JsonPathParseItem *)cell->data;
        end->next = c;
        end = c;
    }

    return head;
}

JsonPathParseItem *
makeIndexArray(JsonPathList *list)
{
    JsonPathParseItem *v = makeItemType(jpiIndexArray);
    JsonPathListCell *cell;
    int i = 0;

    if (!list || list->length == 0)
        return v;

    v->value.array.nelems = list->length;
    v->value.array.elems = malloc(sizeof(v->value.array.elems[0]) *
                                  (size_t)v->value.array.nelems);

    for (cell = list->head; cell; cell = cell->next) {
        JsonPathParseItem *jpi = (JsonPathParseItem *)cell->data;
        /* Each index_elem is a jpiSubscript with args.left/right */
        v->value.array.elems[i].from = jpi->value.args.left;
        v->value.array.elems[i].to = jpi->value.args.right;
        i++;
    }

    return v;
}

JsonPathParseItem *
makeAny(int first, int last)
{
    JsonPathParseItem *v = makeItemType(jpiAny);
    v->value.anybounds.first = (first >= 0) ? (uint32_t)first : UINT32_MAX;
    v->value.anybounds.last = (last >= 0) ? (uint32_t)last : UINT32_MAX;
    return v;
}

bool
makeItemLikeRegex(JsonPathParseItem *expr, JsonPathString *pattern,
                  JsonPathString *flags, JsonPathParseItem **result,
                  char **errMsg)
{
    JsonPathParseItem *v = makeItemType(jpiLikeRegex);
    int i;

    v->value.like_regex.expr = expr;
    v->value.like_regex.pattern = pattern->val;
    v->value.like_regex.patternlen = pattern->len;
    v->value.like_regex.flags = 0;

    /* Parse flags string */
    for (i = 0; flags && i < flags->len; i++) {
        switch (flags->val[i]) {
            case 'i': v->value.like_regex.flags |= JSP_REGEX_ICASE;  break;
            case 's': v->value.like_regex.flags |= JSP_REGEX_DOTALL; break;
            case 'm': v->value.like_regex.flags |= JSP_REGEX_MLINE;  break;
            case 'x': v->value.like_regex.flags |= JSP_REGEX_WSPACE; break;
            case 'q': v->value.like_regex.flags |= JSP_REGEX_QUOTE;  break;
            default:
                if (errMsg)
                    *errMsg = "unrecognized flag character in LIKE_REGEX predicate";
                return false;
        }
    }

    /*
     * In the standalone parser we skip regex compilation validation
     * (no pg_regcomp available). The pattern is stored as-is.
     */

    *result = v;
    return true;
}
