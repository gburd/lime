/*
** Example Extension: JSONB Operators
**
** Demonstrates how to write a parser extension that adds new tokens and
** grammar rules to an existing SQL parser.  This extension adds the
** PostgreSQL-style JSONB operators:
**
**   ->   (arrow)          Extract JSON object field by key
**   ->>  (double arrow)   Extract JSON object field as text
**   @>   (contains)       Does left JSON value contain right?
**   <@   (contained by)   Is left JSON value contained by right?
**   ?    (exists)         Does key exist in JSON object?
**
** Usage:
**
**   ExtensionRegistry *reg = create_extension_registry();
**   ExtensionID id;
**   register_extension(reg, &jsonb_extension_info, &id);
**   load_extension(reg, id, base_snapshot, &error);
**
** This file is intended as a template for extension developers.
** Each section is annotated to explain the patterns involved.
*/
#include "extension.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Step 1: Define the modifications this extension contributes.       */
/*                                                                     */
/*  Each modification is a GrammarModification struct describing one   */
/*  change: a new token, a new rule, a precedence change, etc.         */
/*  These are returned to the extension system via the                 */
/*  get_modifications callback.                                        */
/* ------------------------------------------------------------------ */

/*
** RHS symbol arrays for the new grammar rules.  Each is a
** NULL-terminated array of symbol name strings.
*/
static const char *rhs_arrow[]    = { "a_expr", "ARROW",    "a_expr", NULL };
static const char *rhs_darrow[]   = { "a_expr", "DARROW",   "a_expr", NULL };
static const char *rhs_contains[] = { "a_expr", "CONTAINS", "a_expr", NULL };
static const char *rhs_within[]   = { "a_expr", "WITHIN",   "a_expr", NULL };
static const char *rhs_exists[]   = { "a_expr", "QMARK",    "SCONST", NULL };

/*
** The modifications array.  This is a static array because the contents
** are known at compile time, but an extension could also build this
** dynamically (e.g. by inspecting the base snapshot to avoid conflicts).
*/
static GrammarModification jsonb_mods[] = {
    /* --- New tokens ------------------------------------------------ */
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB arrow operator (->)",
        .u.add_token = {
            .name       = "ARROW",
            .lexeme     = "->",
            .token_code = -1,   /* -1 = auto-assign */
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB double-arrow operator (->>)",
        .u.add_token = {
            .name       = "DARROW",
            .lexeme     = "->>",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB contains operator (@>)",
        .u.add_token = {
            .name       = "CONTAINS",
            .lexeme     = "@>",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB contained-by operator (<@)",
        .u.add_token = {
            .name       = "WITHIN",
            .lexeme     = "<@",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "JSONB key-exists operator (?)",
        .u.add_token = {
            .name       = "QMARK",
            .lexeme     = "?",
            .token_code = -1,
        },
    },

    /* --- New grammar rules ----------------------------------------- */
    /*
    ** Each rule adds a production:  a_expr -> a_expr <OP> a_expr
    ** (or a_expr -> a_expr ? SCONST for the exists operator).
    **
    ** The reduction action (code) would contain C code executed when the
    ** rule fires.  For this example we use placeholder actions.
    */
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr ARROW a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs_arrow,
            .nrhs       = 3,
            .code       = "/* jsonb_arrow(A, B) */",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr DARROW a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs_darrow,
            .nrhs       = 3,
            .code       = "/* jsonb_double_arrow(A, B) */",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr CONTAINS a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs_contains,
            .nrhs       = 3,
            .code       = "/* jsonb_contains(A, B) */",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr WITHIN a_expr",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs_within,
            .nrhs       = 3,
            .code       = "/* jsonb_within(A, B) */",
            .precedence = -1,
        },
    },
    {
        .type = MOD_ADD_RULE,
        .description = "a_expr -> a_expr QMARK SCONST (key exists)",
        .u.add_rule = {
            .lhs        = "a_expr",
            .rhs        = rhs_exists,
            .nrhs       = 3,
            .code       = "/* jsonb_exists(A, key) */",
            .precedence = -1,
        },
    },

    /* --- Precedence modifications ---------------------------------- */
    /*
    ** The JSONB operators should have the same precedence as other
    ** comparison operators (like =, <, >).  In PostgreSQL these are
    ** at precedence level 3, left-associative.
    */
    {
        .type = MOD_MODIFY_PRECEDENCE,
        .description = "Set ARROW precedence (left, level 3)",
        .u.modify_prec = {
            .symbol         = "ARROW",
            .new_precedence = 3,
            .new_assoc      = 1,  /* 1 = left */
        },
    },
    {
        .type = MOD_MODIFY_PRECEDENCE,
        .description = "Set DARROW precedence (left, level 3)",
        .u.modify_prec = {
            .symbol         = "DARROW",
            .new_precedence = 3,
            .new_assoc      = 1,
        },
    },
};

#define JSONB_MOD_COUNT (sizeof(jsonb_mods) / sizeof(jsonb_mods[0]))

/* ------------------------------------------------------------------ */
/*  Step 2: Implement the extension callbacks.                         */
/*                                                                     */
/*  get_modifications  -- return the array of modifications            */
/*  on_conflict        -- (optional) resolve conflicts with other exts */
/*  on_unload          -- (optional) cleanup when extension is removed */
/* ------------------------------------------------------------------ */

/*
** get_modifications callback.
**
** Called by the extension system when the extension is loaded.
** Receives the current base snapshot (which can be inspected to make
** decisions about what to contribute) and must fill in the output
** parameters with the modifications array.
**
** The user_data pointer is whatever was passed in ExtensionInfo.
** For this example it is unused.
*/
static bool jsonb_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out
) {
    (void)user_data;
    (void)base_snapshot;

    /*
    ** Return our static modifications array.  Because the array is
    ** statically allocated, we don't need to do any cleanup in
    ** on_unload.  If modifications were dynamically built (e.g. by
    ** querying base_snapshot), on_unload should free them.
    */
    *mods_out  = jsonb_mods;
    *nmods_out = JSONB_MOD_COUNT;
    return true;
}

/*
** on_conflict callback (optional).
**
** Called when one of our modifications conflicts with another
** extension's modification (e.g. two extensions try to add the same
** token).  We choose CONFLICT_KEEP_EXISTING to be conservative --
** the first extension to load wins.
*/
static ConflictResolution jsonb_on_conflict(
    void *user_data,
    const ConflictInfo *info
) {
    (void)user_data;
    (void)info;

    /*
    ** In a real extension you might inspect info->existing_mod and
    ** info->new_mod to decide.  Returning CONFLICT_MERGE would require
    ** providing a merged modification.
    */
    return CONFLICT_KEEP_EXISTING;
}

/*
** on_unload callback (optional).
**
** Called when the extension is being removed.  Since our modifications
** are static, there is nothing to free.  A dynamic extension would
** free its modifications array and any other allocated resources here.
*/
static void jsonb_on_unload(void *user_data) {
    (void)user_data;
    /* Nothing to free -- modifications are statically allocated. */
}

/* ------------------------------------------------------------------ */
/*  Step 3: Define the ExtensionInfo descriptor.                       */
/*                                                                     */
/*  This is the struct you pass to register_extension().  It provides  */
/*  the extension's name, version, and callback function pointers.     */
/* ------------------------------------------------------------------ */

/*
** Public descriptor for the JSONB extension.
**
** To register:
**   ExtensionID id;
**   register_extension(registry, &jsonb_extension_info, &id);
*/
const ExtensionInfo jsonb_extension_info = {
    .name               = "jsonb_operators",
    .version            = "1.0.0",
    .get_modifications  = jsonb_get_modifications,
    .on_conflict        = jsonb_on_conflict,
    .on_unload          = jsonb_on_unload,
    .user_data          = NULL,
};

/* ------------------------------------------------------------------ */
/*  Step 4 (optional): Provide a convenience registration function.    */
/*                                                                     */
/*  This is not required but can make it easier for callers to load    */
/*  the extension without knowing the internal details.                */
/* ------------------------------------------------------------------ */

/*
** Register and load the JSONB extension in one call.
**
** Returns the assigned ExtensionID on success, or 0 on failure.
** If error_out is non-NULL, it receives a malloc'd error message
** on failure (caller must free).
*/
ExtensionID jsonb_extension_register_and_load(
    ExtensionRegistry *reg,
    const struct ParserSnapshot *base_snapshot,
    char **error_out
) {
    ExtensionID id = 0;

    if (!register_extension(reg, &jsonb_extension_info, &id)) {
        if (error_out) {
            const char msg[] = "jsonb: failed to register extension";
            *error_out = malloc(sizeof(msg));
            if (*error_out) memcpy(*error_out, msg, sizeof(msg));
        }
        return 0;
    }

    char *load_error = NULL;
    if (!load_extension(reg, id, base_snapshot, &load_error)) {
        if (error_out) {
            *error_out = load_error;
        } else {
            free(load_error);
        }
        return 0;
    }

    if (error_out) *error_out = NULL;
    return id;
}
