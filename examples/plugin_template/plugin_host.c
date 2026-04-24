/*
** Example Plugin Host Application
**
** Demonstrates how an application uses the ParserManager to:
**   1. Create a manager
**   2. Load plugins (both static and dynamic)
**   3. Set an active parser
**   4. Get snapshots and run parse sessions
**   5. Hot-swap to a new parser version
**   6. Clean up
**
** Build:
**
**   cc -o plugin_host plugin_host.c \
**      -I../../include -L../../builddir/src -llime_parser -lpthread -ldl
**
** Run:
**
**   ./plugin_host grammar.y
**   ./plugin_host --plugin ./sql_plugin.so grammar.y
*/

#include "parser_manager.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Forward declaration for the static plugin (from sql_plugin.c)      */
/* ------------------------------------------------------------------ */

extern ParserManagerStatus sql_plugin_register(ParserManager *mgr,
                                               void *user_data,
                                               LimePluginHandle *handle_out);

/* ------------------------------------------------------------------ */
/*  Helper: print plugin information                                   */
/* ------------------------------------------------------------------ */

static void print_plugin_info(const ParserManager *mgr, LimePluginHandle h) {
    LimePluginInfo info;
    ParserManagerStatus st = parser_manager_get_plugin_info(mgr, h, &info);
    if (st != PM_OK) {
        fprintf(stderr, "  Error getting plugin info: %s\n",
                parser_manager_status_string(st));
        return;
    }

    char vbuf[16];
    lime_plugin_version_string(info.version, vbuf, sizeof(vbuf));

    printf("  Plugin [%u]: %s v%s\n", info.handle, info.name, vbuf);
    printf("    Capabilities: 0x%x", info.capabilities);
    if (info.capabilities & LIME_CAP_SNAPSHOT)    printf(" SNAPSHOT");
    if (info.capabilities & LIME_CAP_EXTENSIBLE)  printf(" EXTENSIBLE");
    if (info.capabilities & LIME_CAP_JIT)         printf(" JIT");
    if (info.capabilities & LIME_CAP_INCREMENTAL) printf(" INCREMENTAL");
    if (info.capabilities & LIME_CAP_SERIALIZABLE) printf(" SERIALIZABLE");
    printf("\n");
    printf("    Active: %s  Dynamic: %s\n",
           info.is_active ? "yes" : "no",
           info.is_dynamic ? "yes" : "no");
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *plugin_path = NULL;
    const char *grammar_file = NULL;

    /* Simple argument parsing */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--plugin") == 0 && i + 1 < argc) {
            plugin_path = argv[++i];
        } else if (argv[i][0] != '-') {
            grammar_file = argv[i];
        }
    }

    if (grammar_file == NULL) {
        fprintf(stderr, "Usage: %s [--plugin path.so] grammar.y\n", argv[0]);
        return 1;
    }

    /* -------------------------------------------------------------- */
    /*  Step 1: Create the parser manager                              */
    /* -------------------------------------------------------------- */

    ParserManagerConfig config = {
        .max_plugins = 0,             /* unlimited */
        .validate_on_load = true,     /* validate snapshots */
        .auto_jit = false,            /* don't auto-JIT */
        .plugin_search_paths = NULL,  /* no search paths */
    };

    ParserManager *mgr = parser_manager_create(&config);
    if (mgr == NULL) {
        fprintf(stderr, "Failed to create parser manager\n");
        return 1;
    }

    printf("Parser manager created.\n");

    /* -------------------------------------------------------------- */
    /*  Step 2: Load a plugin                                          */
    /* -------------------------------------------------------------- */

    LimePluginHandle handle;
    ParserManagerStatus st;

    if (plugin_path != NULL) {
        /*
        ** Dynamic loading: load from a shared library.
        ** The manager calls dlopen(), finds lime_plugin_entry(),
        ** validates ABI, and calls init().
        */
        printf("Loading dynamic plugin: %s\n", plugin_path);
        st = parser_manager_load(mgr, plugin_path, NULL, &handle);
    } else {
        /*
        ** Static linking: register the compiled-in sql_plugin.
        */
        printf("Registering static plugin...\n");
        st = sql_plugin_register(mgr, NULL, &handle);
    }

    if (st != PM_OK) {
        fprintf(stderr, "Failed to load plugin: %s\n",
                parser_manager_status_string(st));
        parser_manager_destroy(mgr);
        return 1;
    }

    printf("Plugin loaded (handle=%u).\n", handle);
    print_plugin_info(mgr, handle);

    /* -------------------------------------------------------------- */
    /*  Step 3: Set the active parser and create a snapshot             */
    /* -------------------------------------------------------------- */

    printf("\nActivating plugin with grammar: %s\n", grammar_file);
    st = parser_manager_set_active(mgr, handle, grammar_file);
    if (st != PM_OK) {
        fprintf(stderr, "Failed to activate plugin: %s\n",
                parser_manager_status_string(st));
        parser_manager_destroy(mgr);
        return 1;
    }

    printf("Plugin activated.\n");

    /* -------------------------------------------------------------- */
    /*  Step 4: Use the parser                                         */
    /* -------------------------------------------------------------- */

    /*
    ** Get the active snapshot. This acquires a reference that we
    ** must release when done.
    */
    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        fprintf(stderr, "No active snapshot available.\n");
        parser_manager_destroy(mgr);
        return 1;
    }

    printf("Got snapshot (version ?).\n");

    /*
    ** Begin a parse session. The session pins the snapshot, so
    ** even if the active plugin changes, our parse is stable.
    */
    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) {
        fprintf(stderr, "Failed to begin parse session.\n");
        lemon_snapshot_release(snap);
        parser_manager_destroy(mgr);
        return 1;
    }

    /*
    ** Feed tokens to the parser.
    ** In a real application, you would use the Tokenizer to extract
    ** tokens from input and feed them one at a time:
    **
    **   Tokenizer *tok = tokenizer_create(table, sql, strlen(sql));
    **   Token t;
    **   while (tokenizer_next(tok, &t)) {
    **       parse_token(ctx, t.type, &t);
    **   }
    **   parse_token(ctx, 0, NULL);  // end of input
    **   tokenizer_destroy(tok);
    */
    printf("Parse session active (would feed tokens here).\n");

    /* End the session, releasing the snapshot reference */
    parse_end(ctx);
    lemon_snapshot_release(snap);
    printf("Parse session ended.\n");

    /* -------------------------------------------------------------- */
    /*  Step 5: List all loaded plugins                                 */
    /* -------------------------------------------------------------- */

    printf("\nLoaded plugins:\n");
    uint32_t count = parser_manager_plugin_count(mgr);
    LimePluginInfo *infos = calloc(count, sizeof(LimePluginInfo));
    if (infos != NULL) {
        uint32_t actual;
        parser_manager_list_plugins(mgr, infos, count, &actual);
        for (uint32_t i = 0; i < actual; i++) {
            char vbuf[16];
            lime_plugin_version_string(infos[i].version, vbuf, sizeof(vbuf));
            printf("  [%u] %s v%s %s\n",
                   infos[i].handle,
                   infos[i].name,
                   vbuf,
                   infos[i].is_active ? "(active)" : "");
        }
        free(infos);
    }

    /* -------------------------------------------------------------- */
    /*  Step 6: Demonstrate hot-swap (using the same plugin here)      */
    /* -------------------------------------------------------------- */

    printf("\nHot-swapping to same plugin with grammar: %s\n", grammar_file);
    st = parser_manager_hot_swap(mgr, handle, grammar_file);
    if (st != PM_OK) {
        fprintf(stderr, "Hot-swap failed: %s\n",
                parser_manager_status_string(st));
        /* Not fatal -- the old snapshot is still active */
    } else {
        printf("Hot-swap succeeded.\n");
    }

    /* -------------------------------------------------------------- */
    /*  Step 7: Clean up                                               */
    /* -------------------------------------------------------------- */

    printf("\nShutting down...\n");
    parser_manager_destroy(mgr);
    printf("Done.\n");

    return 0;
}
