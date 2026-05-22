/*
** Grammar modification serializer.
**
** Emits a `.lime`-syntax text fragment from an array of
** GrammarModifications.  When concatenated with the base grammar text
** and re-parsed by the `lime` generator, the result is equivalent to
** applying the modifications to a freshly-built snapshot -- the
** "subprocess fallback" path for runtime grammar modification that
** works today even though in-process apply_add_rule() is stubbed
** (Task #3 / P0-1).
**
** Not all modifications round-trip cleanly:
**
**   * MOD_ADD_RULE with .reduce != NULL and .code == NULL is skipped.
**     The reduce function pointer has no text representation, so the
**     action body is lost.  Callers that registered runtime callbacks
**     must keep their dispatch state alive separately.
**
**   * MOD_REMOVE_RULE is skipped.  The .lime concatenation model
**     cannot express rule removal; the caller must either filter the
**     base grammar text before concatenation, or accept that removals
**     won't take effect in a subprocess rebuild.
**
**   * MOD_ADD_RULE's integer `.precedence` field cannot round-trip
**     through .lime syntax (which uses a [SYMBOL] marker, not a
**     number).  Emitted as a comment; the concatenated grammar will
**     not apply the precedence override.  A future add_rule revision
**     should carry the precedence symbol name as a string.
**
** The count of skipped entries is reported via `*skipped_out` when
** non-NULL so callers can decide whether subprocess fallback is
** viable for their modification set.
*/
#ifndef LIME_MOD_SERIALIZE_H
#define LIME_MOD_SERIALIZE_H

#include "extension.h"

#include <stddef.h>
#include <stdint.h>

/*
** Emit .lime-syntax text for the given modifications.
**
** Returns a malloc'd NUL-terminated buffer on success; NULL on
** allocation failure.  Caller owns the buffer and must free() it.
**
** `skipped_out` (optional): receives the number of modifications
** that could not be serialized (see the module-level comment above).
** A non-zero skipped count does not fail the call; callers that
** require lossless serialization should inspect it.
**
** `error` (optional): on any non-allocation failure (currently only
** NULL `mods` with `nmods > 0`), receives a malloc'd error message.
*/
char *lime_modifications_to_grammar_text(const GrammarModification *mods, uint32_t nmods,
                                         uint32_t *skipped_out, char **error);

#endif /* LIME_MOD_SERIALIZE_H */
