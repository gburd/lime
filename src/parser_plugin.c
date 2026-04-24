/*
** Parser Plugin utilities.
**
** Provides helper functions for working with LimeParserPlugin structs:
** validation, capability checking, and diagnostic output.  These are
** used internally by the ParserManager and can also be used by plugin
** authors for self-testing.
**
** See include/parser_manager.h for type definitions.
*/
#include "parser_manager.h"

#include <stdio.h>
#include <string.h>

/* ================================================================== */
/*  Plugin validation                                                  */
/* ================================================================== */

/*
** Validate that a LimeParserPlugin struct has all required callbacks set
** and that its ABI version is compatible with the manager.
**
** On success returns PM_OK.  On failure returns an appropriate status
** code describing the first problem found.
*/
ParserManagerStatus lime_plugin_validate(const LimeParserPlugin *plugin) {
    if (plugin == NULL) return PM_ERR_INVALID_ARG;

    /* Required identity callbacks */
    if (plugin->get_name == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_version == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_abi_major == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_abi_minor == NULL) return PM_ERR_INVALID_ARG;
    if (plugin->get_capabilities == NULL) return PM_ERR_INVALID_ARG;

    /* Required lifecycle callback */
    if (plugin->destroy == NULL) return PM_ERR_INVALID_ARG;

    /* ABI compatibility check */
    uint16_t abi_major = plugin->get_abi_major();
    if (abi_major != LIME_PLUGIN_ABI_VERSION_MAJOR) {
        return PM_ERR_ABI_MISMATCH;
    }

    /* If the plugin advertises SNAPSHOT capability, create_snapshot is required */
    uint32_t caps = plugin->get_capabilities();
    if ((caps & LIME_CAP_SNAPSHOT) && plugin->create_snapshot == NULL) {
        return PM_ERR_CAPABILITY_MISSING;
    }

    /* If the plugin advertises SERIALIZABLE, both serialize and deserialize
    ** must be present */
    if (caps & LIME_CAP_SERIALIZABLE) {
        if (plugin->serialize_snapshot == NULL ||
            plugin->deserialize_snapshot == NULL) {
            return PM_ERR_CAPABILITY_MISSING;
        }
    }

    return PM_OK;
}

/* ================================================================== */
/*  Capability checking                                                */
/* ================================================================== */

/*
** Check if a plugin has a specific capability.
*/
bool lime_plugin_has_capability(const LimeParserPlugin *plugin,
                                LimePluginCaps cap) {
    if (plugin == NULL || plugin->get_capabilities == NULL) return false;
    return (plugin->get_capabilities() & (uint32_t)cap) != 0;
}

/*
** Check if a plugin has all of the specified capabilities (bitwise AND).
*/
bool lime_plugin_has_all_capabilities(const LimeParserPlugin *plugin,
                                      uint32_t required_caps) {
    if (plugin == NULL || plugin->get_capabilities == NULL) return false;
    uint32_t caps = plugin->get_capabilities();
    return (caps & required_caps) == required_caps;
}

/*
** Return a human-readable name for a capability flag.
** Returns "unknown" for unrecognized flags.
*/
const char *lime_plugin_capability_name(LimePluginCaps cap) {
    switch (cap) {
    case LIME_CAP_SNAPSHOT:     return "snapshot";
    case LIME_CAP_EXTENSIBLE:   return "extensible";
    case LIME_CAP_JIT:          return "jit";
    case LIME_CAP_INCREMENTAL:  return "incremental";
    case LIME_CAP_SERIALIZABLE: return "serializable";
    }
    return "unknown";
}

/*
** Format a capability bitmask as a comma-separated string of names.
** Writes into buf (which must be at least buflen bytes).
** Returns buf for convenience.
*/
char *lime_plugin_capabilities_string(uint32_t caps, char *buf, size_t buflen) {
    if (buf == NULL || buflen == 0) return buf;

    buf[0] = '\0';
    size_t pos = 0;
    bool first = true;

    static const struct {
        LimePluginCaps cap;
        const char *name;
    } cap_table[] = {
        { LIME_CAP_SNAPSHOT,     "snapshot" },
        { LIME_CAP_EXTENSIBLE,   "extensible" },
        { LIME_CAP_JIT,          "jit" },
        { LIME_CAP_INCREMENTAL,  "incremental" },
        { LIME_CAP_SERIALIZABLE, "serializable" },
    };

    for (size_t i = 0; i < sizeof(cap_table) / sizeof(cap_table[0]); i++) {
        if (caps & (uint32_t)cap_table[i].cap) {
            size_t nlen = strlen(cap_table[i].name);
            size_t needed = nlen + (first ? 0 : 2); /* ", " prefix */
            if (pos + needed >= buflen) break;

            if (!first) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            memcpy(buf + pos, cap_table[i].name, nlen);
            pos += nlen;
            first = false;
        }
    }

    buf[pos] = '\0';
    return buf;
}

/* ================================================================== */
/*  Plugin diagnostic dump                                             */
/* ================================================================== */

/*
** Print a summary of a plugin's identity and capabilities to the given
** FILE stream.  Useful for debugging and logging.
*/
void lime_plugin_dump(const LimeParserPlugin *plugin, FILE *out) {
    if (plugin == NULL || out == NULL) return;

    const char *name = plugin->get_name ? plugin->get_name() : "(null)";

    char vbuf[16];
    if (plugin->get_version) {
        LimePluginVersion v = plugin->get_version();
        lime_plugin_version_string(v, vbuf, sizeof(vbuf));
    } else {
        snprintf(vbuf, sizeof(vbuf), "?.?.?");
    }

    uint16_t abi_major = plugin->get_abi_major ? plugin->get_abi_major() : 0;
    uint16_t abi_minor = plugin->get_abi_minor ? plugin->get_abi_minor() : 0;

    char capbuf[128];
    uint32_t caps = plugin->get_capabilities ? plugin->get_capabilities() : 0;
    lime_plugin_capabilities_string(caps, capbuf, sizeof(capbuf));

    fprintf(out, "Plugin: %s\n", name);
    fprintf(out, "  Version: %s\n", vbuf);
    fprintf(out, "  ABI: %u.%u\n", (unsigned)abi_major, (unsigned)abi_minor);
    fprintf(out, "  Capabilities: %s\n", capbuf[0] ? capbuf : "(none)");
    fprintf(out, "  Callbacks:\n");
    fprintf(out, "    init:                  %s\n", plugin->init ? "yes" : "no");
    fprintf(out, "    destroy:               %s\n", plugin->destroy ? "yes" : "no");
    fprintf(out, "    create_snapshot:       %s\n", plugin->create_snapshot ? "yes" : "no");
    fprintf(out, "    validate_snapshot:     %s\n", plugin->validate_snapshot ? "yes" : "no");
    fprintf(out, "    serialize_snapshot:    %s\n", plugin->serialize_snapshot ? "yes" : "no");
    fprintf(out, "    deserialize_snapshot:  %s\n", plugin->deserialize_snapshot ? "yes" : "no");
}
