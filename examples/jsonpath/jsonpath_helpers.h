/*
 * jsonpath_helpers.h
 *    Declarations for JSONPath grammar helper functions.
 *
 * These functions are used by the Lime-generated parser's reduction
 * actions to build the JsonPathParseItem AST.
 */

#ifndef JSONPATH_HELPERS_H
#define JSONPATH_HELPERS_H

#include "jsonpath_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AST construction helpers */
JsonPathParseItem *makeItemType(JsonPathItemType type);
JsonPathParseItem *makeItemString(JsonPathString *s);
JsonPathParseItem *makeItemVariable(JsonPathString *s);
JsonPathParseItem *makeItemKey(JsonPathString *s);
JsonPathParseItem *makeItemNumeric(JsonPathString *s);
JsonPathParseItem *makeItemBool(bool val);
JsonPathParseItem *makeItemBinary(JsonPathItemType type,
                                  JsonPathParseItem *la,
                                  JsonPathParseItem *ra);
JsonPathParseItem *makeItemUnary(JsonPathItemType type,
                                 JsonPathParseItem *a);
JsonPathParseItem *makeItemList(JsonPathList *list);
JsonPathParseItem *makeIndexArray(JsonPathList *list);
JsonPathParseItem *makeAny(int first, int last);
bool makeItemLikeRegex(JsonPathParseItem *expr,
                       JsonPathString *pattern,
                       JsonPathString *flags,
                       JsonPathParseItem **result,
                       char **errMsg);

/* List helper functions */
JsonPathList *jp_list_make1(void *item);
JsonPathList *jp_list_make2(void *item1, void *item2);
JsonPathList *jp_list_append(JsonPathList *list, void *item);
int jp_list_length(JsonPathList *list);
void *jp_list_first(JsonPathList *list);
void *jp_list_second(JsonPathList *list);
void jp_list_free(JsonPathList *list);

#ifdef __cplusplus
}
#endif

#endif /* JSONPATH_HELPERS_H */
