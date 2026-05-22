/*
** Grammar modification serializer.  See mod_serialize.h for the
** module-level contract.
**
** Implementation notes:
**
**   * Uses a simple grow-by-doubling malloc buffer.  Single producer,
**     single consumer; no concurrency.
**
**   * Escapes nothing.  The input strings (symbol names, type names,
**     action code) come from the GrammarModification, which is
**     typically constructed by extension authors with C string
**     literals.  A deranged caller who embeds unmatched '}' inside a
**     .code block will produce invalid .lime output; that's a caller
**     bug, not one the serializer guards against.
**
**   * Output is designed for human readability as much as for
**     re-parseability.  Each modification becomes one or two logical
**     lines with a descriptive comment.
*/
#include "mod_serialize.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Dynamic text buffer                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int oom; /* sticky out-of-memory flag */
} TextBuf;

static void tb_init(TextBuf *tb) {
    tb->data = NULL;
    tb->len = 0;
    tb->cap = 0;
    tb->oom = 0;
}

static int tb_reserve(TextBuf *tb, size_t need) {
    if (tb->oom) return 0;
    if (tb->len + need + 1 <= tb->cap) return 1;
    size_t new_cap = tb->cap ? tb->cap * 2 : 256;
    while (new_cap < tb->len + need + 1)
        new_cap *= 2;
    char *p = realloc(tb->data, new_cap);
    if (p == NULL) {
        tb->oom = 1;
        return 0;
    }
    tb->data = p;
    tb->cap = new_cap;
    return 1;
}

static void tb_append(TextBuf *tb, const char *s) {
    if (s == NULL) return;
    size_t n = strlen(s);
    if (!tb_reserve(tb, n)) return;
    memcpy(tb->data + tb->len, s, n);
    tb->len += n;
    tb->data[tb->len] = '\0';
}

static void tb_printf(TextBuf *tb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int n = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (n < 0) {
        tb->oom = 1;
        va_end(ap);
        return;
    }
    if (!tb_reserve(tb, (size_t)n)) {
        va_end(ap);
        return;
    }
    vsnprintf(tb->data + tb->len, tb->cap - tb->len, fmt, ap);
    tb->len += (size_t)n;
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/*  Per-modification emitters                                           */
/* ------------------------------------------------------------------ */

/*
** Emit a single MOD_ADD_TOKEN: "%token NAME."
*/
static void emit_add_token(TextBuf *tb, const GrammarModification *mod) {
    const char *name = mod->u.add_token.name;
    if (name == NULL) return;

    if (mod->description != NULL) {
        tb_printf(tb, "/* %s */\n", mod->description);
    }
    tb_printf(tb, "%%token %s.\n", name);
}

/*
** Emit a MOD_ADD_RULE as "LHS ::= RHS0 RHS1 ... . { code }".
**
** Returns 1 if the rule was emitted, 0 if it was skipped.
*/
static int emit_add_rule(TextBuf *tb, const GrammarModification *mod) {
    const char *lhs = mod->u.add_rule.lhs;
    if (lhs == NULL) return 0;

    /* Runtime-callback-only rules cannot be serialized: there is no
    ** way to embed a function pointer in .lime text. */
    if (mod->u.add_rule.reduce != NULL && mod->u.add_rule.code == NULL) {
        if (mod->description != NULL) {
            tb_printf(tb, "/* SKIPPED (runtime reduce callback, no .code): %s */\n",
                      mod->description);
        } else {
            tb_printf(tb,
                      "/* SKIPPED MOD_ADD_RULE for %s "
                      "(runtime reduce callback, no .code) */\n",
                      lhs);
        }
        return 0;
    }

    if (mod->description != NULL) {
        tb_printf(tb, "/* %s */\n", mod->description);
    }
    tb_printf(tb, "%s ::=", lhs);

    const char *const *rhs = mod->u.add_rule.rhs;
    int nrhs = mod->u.add_rule.nrhs;
    for (int i = 0; i < nrhs; i++) {
        if (rhs == NULL || rhs[i] == NULL) {
            tb_append(tb, " /* missing RHS symbol */");
        } else {
            tb_printf(tb, " %s", rhs[i]);
        }
    }
    tb_append(tb, " .");

    if (mod->u.add_rule.code != NULL) {
        tb_printf(tb, " { %s }", mod->u.add_rule.code);
    }

    /*
    ** The integer .precedence field has no direct .lime representation
    ** (the [SYMBOL] marker takes a symbol name, not a number).  Note
    ** this as a comment on the emitted rule so reviewers see that the
    ** subprocess-rebuilt parser will not honour the override.
    */
    if (mod->u.add_rule.precedence >= 0) {
        tb_printf(tb,
                  "\n/* NOTE: precedence=%d on above rule not expressible in "
                  ".lime text;\n"
                  "** concatenated grammar will NOT apply the override. */",
                  mod->u.add_rule.precedence);
    }

    tb_append(tb, "\n");
    return 1;
}

/*
** Emit MOD_MODIFY_PRECEDENCE.
**
** Semantically this is additive rather than modifying: a re-parsed
** grammar with a trailing %left/%right/%nonassoc picks up the later
** declaration.  Callers wanting true "modify" semantics would have to
** rewrite the base grammar text, which is out of scope here.
*/
static void emit_modify_prec(TextBuf *tb, const GrammarModification *mod) {
    const char *sym = mod->u.modify_prec.symbol;
    if (sym == NULL) return;

    const char *kw;
    switch (mod->u.modify_prec.new_assoc) {
    case 1:
        kw = "left";
        break;
    case 2:
        kw = "right";
        break;
    case 3:
        kw = "nonassoc";
        break;
    default:
        /* assoc == 0 = \"none\" isn't expressible as a single
            ** directive; skip with a marker. */
        tb_printf(tb,
                  "/* SKIPPED MOD_MODIFY_PRECEDENCE for %s "
                  "(new_assoc=0 not expressible) */\n",
                  sym);
        return;
    }

    if (mod->description != NULL) {
        tb_printf(tb, "/* %s */\n", mod->description);
    }
    tb_printf(tb, "%%%s %s.\n", kw, sym);
}

/*
** Emit MOD_ADD_TYPE as "%type NAME {DATATYPE}".
*/
static void emit_add_type(TextBuf *tb, const GrammarModification *mod) {
    const char *name = mod->u.add_type.name;
    const char *dt = mod->u.add_type.datatype;
    if (name == NULL || dt == NULL) return;

    if (mod->description != NULL) {
        tb_printf(tb, "/* %s */\n", mod->description);
    }
    tb_printf(tb, "%%type %s {%s}\n", name, dt);
}

/*
** Emit MOD_REMOVE_RULE as a comment only.
*/
static void emit_remove_rule(TextBuf *tb, const GrammarModification *mod) {
    const char *lhs = mod->u.remove_rule.lhs;
    int idx = mod->u.remove_rule.rule_index;
    tb_printf(tb,
              "/* SKIPPED MOD_REMOVE_RULE (lhs=%s, rule_index=%d): "
              "rule removal not expressible via .lime concatenation. */\n",
              lhs ? lhs : "(null)", idx);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

char *lime_modifications_to_grammar_text(const GrammarModification *mods, uint32_t nmods,
                                         uint32_t *skipped_out, char **error) {
    if (skipped_out != NULL) *skipped_out = 0;
    if (error != NULL) *error = NULL;

    if (nmods > 0 && mods == NULL) {
        if (error != NULL) {
            *error = strdup("mods is NULL but nmods > 0");
        }
        return NULL;
    }

    TextBuf tb;
    tb_init(&tb);
    tb_append(&tb, "/*\n"
                   "** Auto-generated by lime_modifications_to_grammar_text().\n"
                   "** Concatenate this fragment after the base grammar text and\n"
                   "** re-run `lime` to produce a parser equivalent to the base\n"
                   "** snapshot with these modifications applied.\n"
                   "*/\n");

    uint32_t skipped = 0;
    for (uint32_t i = 0; i < nmods; i++) {
        const GrammarModification *m = &mods[i];
        switch (m->type) {
        case MOD_ADD_TOKEN:
            emit_add_token(&tb, m);
            break;
        case MOD_ADD_RULE:
            if (!emit_add_rule(&tb, m)) skipped++;
            break;
        case MOD_MODIFY_PRECEDENCE:
            emit_modify_prec(&tb, m);
            break;
        case MOD_ADD_TYPE:
            emit_add_type(&tb, m);
            break;
        case MOD_REMOVE_RULE:
            emit_remove_rule(&tb, m);
            skipped++;
            break;
        default:
            tb_printf(&tb, "/* SKIPPED unknown modification type %d at index %u */\n", (int)m->type,
                      (unsigned)i);
            skipped++;
            break;
        }
    }

    if (tb.oom) {
        free(tb.data);
        if (error != NULL && *error == NULL) {
            *error = strdup("allocation failure in serializer");
        }
        return NULL;
    }

    if (skipped_out != NULL) *skipped_out = skipped;
    return tb.data;
}
