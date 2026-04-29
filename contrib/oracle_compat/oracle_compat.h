/*
** Oracle SQL Compatibility Extension -- Public Header
**
** Include this header to register the Oracle compatibility extension
** with the Lime extension registry or internal extension system.
*/
#ifndef ORACLE_COMPAT_H
#define ORACLE_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct ExtensionRegistry;
struct GrammarExtensionMetadata;

/*
** Register the Oracle compatibility extension with a high-level
** extension registry (extension_registry.h).
**
** Prerequisites: "postgres_base" must already be registered.
** Returns true on success.
*/
bool oracle_compat_register(struct ExtensionRegistry *reg);

/*
** Register using the internal extension system (src/extension.h).
** Returns true on success, sets *id_out to the assigned ID.
*/
bool oracle_compat_register_ext(void *ext_registry, uint32_t *id_out);

/*
** Get read-only access to the extension's metadata.
*/
const struct GrammarExtensionMetadata *oracle_compat_get_metadata(void);

/*
** Clean up all resources held by the Oracle extension.
*/
void oracle_compat_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* ORACLE_COMPAT_H */
