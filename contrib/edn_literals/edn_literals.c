/*
** EDN Literals Extension - Registration and Context Integration
**
** Registers the EDN literals grammar extension with the extension
** registry and configures grammar context switching so that the parser
** automatically enters EDN mode when it encounters EDN boundary tokens.
**
** Context switching:
**   Entry triggers:
**     - "{:" prefix (LBRACE followed by KEYWORD) -> map mode
**     - "[" in value position -> vector mode
**     - "#{" -> set mode
**   Exit:
**     - Matching closing delimiter at same nesting depth
**
** The extension uses DISAMBIG_PRIORITY with priority 4, placing it
** after core SQL dialects but before user extensions.  EDN syntax is
** unambiguous with respect to SQL (the delimiters {:, [, #{ do not
** appear in standard SQL), so no fork-resolve is needed.
*/

#include "edn_semantics.h"

#include <stddef.h>
#include <string.h>

/* Pull in the extension registry and grammar context APIs */
#include "extension_registry.h"
#include "grammar_context.h"

/* ------------------------------------------------------------------ */
/*  Grammar modifications                                              */
/*                                                                     */
/*  These describe the grammar rules added by the EDN extension.       */
/*  In a full integration, GrammarModification entries would reference */
/*  the actual rule data structures.  Here we declare them as          */
/*  descriptive metadata for the extension registry.                   */
/* ------------------------------------------------------------------ */

/*
** Forward declaration -- GrammarModification is defined in the core
** extension system headers.  We use an opaque pointer here since the
** actual modifications are loaded from the compiled grammar.
*/

/* Number of grammar modifications added by this extension:
**   - 1 edn_input production
**   - 11 edn_value alternatives (nil, bool x2, int, float, string,
**     keyword, symbol, vector, map, set)
**   - 1 edn_keyword production
**   - 2 edn_vector productions (with/without elements)
**   - 2 edn_vector_elements productions (base/recursive)
**   - 2 edn_map productions (with/without entries)
**   - 2 edn_map_entries productions (base/recursive)
**   - 1 edn_map_entry production
**   - 2 edn_set productions (with/without elements)
**   - 2 edn_set_elements productions (base/recursive)
**   - 1 edn_literal_expr production
*/
#define EDN_NUM_MODIFICATIONS  27

/* ------------------------------------------------------------------ */
/*  Extension metadata                                                 */
/* ------------------------------------------------------------------ */

static const char *edn_requires[] = {"sql_base", NULL};

static GrammarExtensionMetadata edn_metadata = {
    .name               = "edn_literals",
    .version            = "1.0.0",
    .strategy           = DISAMBIG_PRIORITY,
    .priority           = 4,
    .policy             = EXEC_SEQUENTIAL,
    .oracle             = NULL,
    .conflict_threshold = 0.0f,
    .requires           = edn_requires,
    .conflicts_with     = NULL,
    .modifications      = NULL,   /* Loaded from compiled grammar */
    .nmodifications     = EDN_NUM_MODIFICATIONS,
};

/* ------------------------------------------------------------------ */
/*  Context switch callback                                            */
/* ------------------------------------------------------------------ */

/*
** Called when the grammar context system detects a switch into or
** out of EDN mode.  This allows the lexer to adjust its token
** recognition rules (e.g., recognize ':' as keyword prefix rather
** than a SQL operator).
*/
static bool edn_context_switch_cb(GrammarMode prev_mode,
                                   GrammarMode new_mode,
                                   void *user_data) {
    (void)user_data;
    (void)prev_mode;
    (void)new_mode;

    /*
    ** Accept all transitions involving MODE_EDN.  A real implementation
    ** would notify the lexer to switch token tables here.
    */
    return true;
}

/* ------------------------------------------------------------------ */
/*  Mode registration helper                                           */
/* ------------------------------------------------------------------ */

/*
** Register the EDN grammar mode with a context stack.  This sets up
** the trigger tokens that cause automatic context switching.
**
** The EDN mode uses bracket-depth-based exit: when the closing
** delimiter (} or ]) is consumed and the bracket depth returns to
** the level where EDN mode was entered, the context is automatically
** popped.
**
** Parameters:
**   stack        - The grammar context stack to register with
**   edn_snapshot - Parser snapshot for the EDN sub-grammar
**
** Returns true on success.
*/
bool edn_register_grammar_mode(GrammarContextStack *stack,
                                ParserSnapshot *edn_snapshot) {
    if (stack == NULL || edn_snapshot == NULL) return false;

    GrammarModeInfo edn_mode = {
        .mode            = MODE_EDN,
        .name            = "edn",
        .snapshot        = edn_snapshot,
        .trigger_token   = -1,      /* Use lexeme-based trigger */
        .trigger_lexeme  = "{:",     /* Detect {: prefix for maps */
        .exit_token      = -1,       /* Bracket-depth-based exit */
    };

    if (!grammar_context_register_mode(stack, &edn_mode)) {
        return false;
    }

    /* Register the context switch callback */
    grammar_context_set_switch_callback(stack, edn_context_switch_cb, NULL);

    return true;
}

/* ------------------------------------------------------------------ */
/*  Extension registration                                             */
/* ------------------------------------------------------------------ */

/*
** Register the EDN literals extension with the extension registry.
** This makes the extension discoverable and enables dependency
** checking and load ordering.
**
** Returns true on success, false if registration fails.
*/
bool edn_literals_register(ExtensionRegistry *reg) {
    if (reg == NULL) return false;
    return extension_registry_register(reg, &edn_metadata);
}

/* ------------------------------------------------------------------ */
/*  EDN value serialization to SQL                                     */
/* ------------------------------------------------------------------ */

/*
** Serialize an EDN value to a PostgreSQL-compatible string
** representation.  This is used when converting EDN literals in
** SQL expressions to their SQL equivalents.
**
** Conversion rules:
**   nil        -> NULL
**   true/false -> TRUE / FALSE
**   integer    -> integer literal
**   float      -> float literal
**   string     -> 'string' (single-quoted)
**   keyword    -> 'keyword_name' (as string)
**   vector     -> ARRAY[...] (PostgreSQL array literal)
**   map        -> jsonb '{"key": val, ...}'
**   set        -> ARRAY[...] (deduplicated)
**
** The output buffer must be pre-allocated by the caller.  Returns
** the number of characters written, or -1 on error.
*/
int edn_value_to_sql(const EdnValue *val, char *buf, size_t bufsize) {
    if (val == NULL || buf == NULL || bufsize == 0) return -1;

    int written = 0;

    switch (val->type) {
    case EDN_VAL_NIL:
        written = snprintf(buf, bufsize, "NULL");
        break;

    case EDN_VAL_BOOL:
        written = snprintf(buf, bufsize, "%s",
                           val->u.bval ? "TRUE" : "FALSE");
        break;

    case EDN_VAL_INTEGER:
        written = snprintf(buf, bufsize, "%lld",
                           (long long)val->u.ival);
        break;

    case EDN_VAL_FLOAT:
        written = snprintf(buf, bufsize, "%g", val->u.fval);
        break;

    case EDN_VAL_STRING:
        written = snprintf(buf, bufsize, "'%s'",
                           val->u.string.value ? val->u.string.value : "");
        break;

    case EDN_VAL_KEYWORD:
        if (val->u.keyword.ns != NULL) {
            written = snprintf(buf, bufsize, "'%s/%s'",
                               val->u.keyword.ns, val->u.keyword.name);
        } else {
            written = snprintf(buf, bufsize, "'%s'",
                               val->u.keyword.name ? val->u.keyword.name : "");
        }
        break;

    case EDN_VAL_SYMBOL:
        written = snprintf(buf, bufsize, "'%s'",
                           val->u.symbol.name ? val->u.symbol.name : "");
        break;

    case EDN_VAL_VECTOR:
    case EDN_VAL_SET: {
        EdnValueList *list = (val->type == EDN_VAL_VECTOR)
                             ? val->u.vector : val->u.set;
        int pos = snprintf(buf, bufsize, "ARRAY[");
        if (list != NULL) {
            for (int i = 0; i < list->count; i++) {
                if (i > 0) {
                    pos += snprintf(buf + pos, bufsize - (size_t)pos, ", ");
                }
                pos += edn_value_to_sql(list->items[i],
                                         buf + pos,
                                         bufsize - (size_t)pos);
            }
        }
        pos += snprintf(buf + pos, bufsize - (size_t)pos, "]");
        written = pos;
        break;
    }

    case EDN_VAL_MAP: {
        int pos = snprintf(buf, bufsize, "'{");
        EdnMap *map = val->u.map;
        if (map != NULL) {
            EdnMapEntry *entry = map->head;
            int idx = 0;
            while (entry != NULL) {
                if (idx > 0) {
                    pos += snprintf(buf + pos, bufsize - (size_t)pos, ", ");
                }
                /* Key */
                pos += snprintf(buf + pos, bufsize - (size_t)pos, "\"");
                if (entry->key->type == EDN_VAL_KEYWORD) {
                    if (entry->key->u.keyword.ns != NULL) {
                        pos += snprintf(buf + pos, bufsize - (size_t)pos,
                                        "%s/%s",
                                        entry->key->u.keyword.ns,
                                        entry->key->u.keyword.name);
                    } else {
                        pos += snprintf(buf + pos, bufsize - (size_t)pos,
                                        "%s",
                                        entry->key->u.keyword.name);
                    }
                } else {
                    /* Serialize non-keyword keys */
                    char keybuf[256];
                    edn_value_to_sql(entry->key, keybuf, sizeof(keybuf));
                    pos += snprintf(buf + pos, bufsize - (size_t)pos,
                                    "%s", keybuf);
                }
                pos += snprintf(buf + pos, bufsize - (size_t)pos, "\": ");

                /* Value */
                char valbuf[256];
                edn_value_to_sql(entry->value, valbuf, sizeof(valbuf));
                pos += snprintf(buf + pos, bufsize - (size_t)pos,
                                "%s", valbuf);

                entry = entry->next;
                idx++;
            }
        }
        pos += snprintf(buf + pos, bufsize - (size_t)pos, "}'::jsonb");
        written = pos;
        break;
    }
    }

    return written;
}

/* ------------------------------------------------------------------ */
/*  EDN value type name (for diagnostics)                              */
/* ------------------------------------------------------------------ */

const char *edn_value_type_name(EdnValueType type) {
    switch (type) {
    case EDN_VAL_NIL:     return "nil";
    case EDN_VAL_BOOL:    return "boolean";
    case EDN_VAL_INTEGER: return "integer";
    case EDN_VAL_FLOAT:   return "float";
    case EDN_VAL_STRING:  return "string";
    case EDN_VAL_KEYWORD: return "keyword";
    case EDN_VAL_SYMBOL:  return "symbol";
    case EDN_VAL_VECTOR:  return "vector";
    case EDN_VAL_MAP:     return "map";
    case EDN_VAL_SET:     return "set";
    }
    return "unknown";
}
