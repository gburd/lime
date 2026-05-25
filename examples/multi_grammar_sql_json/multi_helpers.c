/*
** multi_helpers.c -- SQL AST node constructors / destructor / printer
** for the multi-grammar example.
**
** SQL columns either hold a plain identifier or a JsonValue parsed
** from an embedded `json '...'` literal.  The column print path
** delegates to json_print() for the literal case so the printed form
** shows both halves of the parse cleanly.
*/
#include "multi.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Column constructors                                                */
/* ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (s == NULL) return NULL;
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (d == NULL) return NULL;
    memcpy(d, s, n + 1);
    return d;
}

SqlColumn *sql_col_ident(const char *name, int line, int col) {
    SqlColumn *c = calloc(1, sizeof(SqlColumn));
    if (c == NULL) return NULL;
    c->kind = SQL_COL_IDENT;
    c->ident = xstrdup(name);
    c->line = line;
    c->col = col;
    return c;
}

SqlColumn *sql_col_json(JsonValue *root, int line, int col) {
    SqlColumn *c = calloc(1, sizeof(SqlColumn));
    if (c == NULL) return NULL;
    c->kind = SQL_COL_JSON_LITERAL;
    c->json_root = root;
    c->line = line;
    c->col = col;
    return c;
}

static void sql_col_destroy(SqlColumn *c) {
    if (c == NULL) return;
    if (c->kind == SQL_COL_IDENT) {
        free(c->ident);
    } else if (c->kind == SQL_COL_JSON_LITERAL) {
        json_free(c->json_root);
    }
    free(c);
}

/* ------------------------------------------------------------------ */
/*  Select constructor                                                 */
/* ------------------------------------------------------------------ */

SqlSelect *sql_select_new(void) {
    SqlSelect *s = calloc(1, sizeof(SqlSelect));
    return s;
}

void sql_select_add_column(SqlSelect *s, SqlColumn *col) {
    if (s == NULL || col == NULL) return;
    if (s->ncolumns == s->cap) {
        size_t ncap = s->cap == 0 ? 4 : s->cap * 2;
        SqlColumn **nbuf = realloc(s->columns, ncap * sizeof(SqlColumn *));
        if (nbuf == NULL) return;
        s->columns = nbuf;
        s->cap = ncap;
    }
    s->columns[s->ncolumns++] = col;
}

void sql_select_destroy(SqlSelect *s) {
    if (s == NULL) return;
    for (size_t i = 0; i < s->ncolumns; i++) {
        sql_col_destroy(s->columns[i]);
    }
    free(s->columns);
    free(s->table);
    free(s->where_lhs);
    free(s);
}

/* ------------------------------------------------------------------ */
/*  Pretty printer                                                     */
/* ------------------------------------------------------------------ */

void sql_select_print(FILE *out, const SqlSelect *s) {
    if (s == NULL) {
        fprintf(out, "(null)\n");
        return;
    }
    fprintf(out, "SELECT\n");
    for (size_t i = 0; i < s->ncolumns; i++) {
        const SqlColumn *c = s->columns[i];
        const char *sep = (i + 1 < s->ncolumns) ? "," : "";
        if (c->kind == SQL_COL_IDENT) {
            fprintf(out, "  ident: \"%s\"  (line %d, col %d)%s\n", c->ident, c->line, c->col, sep);
        } else {
            fprintf(out, "  json:  (line %d, col %d)\n", c->line, c->col);
            json_print(out, c->json_root, 4);
            fprintf(out, "%s\n", sep);
        }
    }
    fprintf(out, "FROM \"%s\"\n", s->table != NULL ? s->table : "(null)");
    fprintf(out, "WHERE \"%s\" = %ld\n", s->where_lhs != NULL ? s->where_lhs : "(null)",
            s->where_rhs);
}
