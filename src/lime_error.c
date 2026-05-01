/*
** lime_error.c -- Implementation of the LimeError helpers declared in
** include/lime_error.h.
**
** These are optional utilities for accumulating parser errors during
** a parse session.  Hosts with their own error-tracking structures can
** ignore this file.
*/
#include "lime_error.h"

#include <stdlib.h>
#include <string.h>

/*
** Duplicate a C string using malloc.  Returns NULL if the input is
** NULL or on allocation failure.
*/
static char *dup_or_null(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (out == NULL) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

LimeError *lime_error_append(LimeError *list,
                             const char *message,
                             const char *expected,
                             uint32_t line,
                             uint32_t column,
                             const char *filename)
{
    LimeError *node = (LimeError *)malloc(sizeof(*node));
    if (node == NULL) return list;

    node->line = line;
    node->column = column;
    node->filename = filename; /* borrowed */
    node->message = dup_or_null(message);
    node->expected = dup_or_null(expected);
    node->next = NULL;

    /* If either duplication failed on a non-NULL input, bail cleanly. */
    if ((message != NULL && node->message == NULL) ||
        (expected != NULL && node->expected == NULL)) {
        free(node->message);
        free(node->expected);
        free(node);
        return list;
    }

    if (list == NULL) return node;

    LimeError *tail = list;
    while (tail->next != NULL) tail = tail->next;
    tail->next = node;
    return list;
}

size_t lime_error_count(const LimeError *list)
{
    size_t n = 0;
    while (list != NULL) {
        n++;
        list = list->next;
    }
    return n;
}

void lime_error_free(LimeError *err)
{
    while (err != NULL) {
        LimeError *next = err->next;
        free(err->message);
        free(err->expected);
        free(err);
        err = next;
    }
}
