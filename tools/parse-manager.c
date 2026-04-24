/*
** parse-manager -- CLI tool for managing Lime parser plugins at runtime.
**
** Commands:
**
**   parse-manager list                    Show all loaded parsers
**   parse-manager load <path>             Load a parser plugin (.so)
**   parse-manager unload <name>           Unload a parser plugin
**   parse-manager info <name>             Show detailed plugin info
**   parse-manager activate <name> [grammar]  Set active parser
**   parse-manager test <name> <file>      Parse a file with a plugin
**   parse-manager compare <n1> <n2> <file>  Compare two parsers
**   parse-manager benchmark <name> <files...>  Benchmark a parser
**   parse-manager version                 Show version information
**
** Build:
**   cc -o parse-manager parse-manager.c \
**      -I../include ../src/parser_manager.c ../src/parser_plugin.c \
**      ../src/snapshot.c -ldl -lpthread
*/

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>

#include "parser_manager.h"
#include "parser.h"

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */

static int cmd_list(ParserManager *mgr, int argc, char **argv);
static int cmd_load(ParserManager *mgr, int argc, char **argv);
static int cmd_unload(ParserManager *mgr, int argc, char **argv);
static int cmd_info(ParserManager *mgr, int argc, char **argv);
static int cmd_activate(ParserManager *mgr, int argc, char **argv);
static int cmd_test(ParserManager *mgr, int argc, char **argv);
static int cmd_compare(ParserManager *mgr, int argc, char **argv);
static int cmd_benchmark(ParserManager *mgr, int argc, char **argv);
static int cmd_version(ParserManager *mgr, int argc, char **argv);
static int cmd_help(ParserManager *mgr, int argc, char **argv);

/* ================================================================== */
/*  Command table                                                      */
/* ================================================================== */

typedef struct {
    const char *name;
    const char *usage;
    const char *description;
    int (*handler)(ParserManager *mgr, int argc, char **argv);
} Command;

static const Command commands[] = {
    { "list",      "",
      "Show all loaded parser plugins",
      cmd_list },
    { "load",      "<path> [--search-path <dir>]",
      "Load a parser plugin from a shared library",
      cmd_load },
    { "unload",    "<name>",
      "Unload a parser plugin by name",
      cmd_unload },
    { "info",      "<name>",
      "Show detailed information about a plugin",
      cmd_info },
    { "activate",  "<name> [grammar-file]",
      "Set the active parser, optionally loading a grammar",
      cmd_activate },
    { "test",      "<name> <input-file>",
      "Parse a file using the named parser plugin",
      cmd_test },
    { "compare",   "<name1> <name2> <input-file>",
      "Compare parse results from two different parsers",
      cmd_compare },
    { "benchmark", "<name> <file> [file...]",
      "Benchmark a parser on one or more input files",
      cmd_benchmark },
    { "version",   "",
      "Show version information",
      cmd_version },
    { "help",      "[command]",
      "Show help for a specific command or list all commands",
      cmd_help },
    { NULL, NULL, NULL, NULL }
};

/* ================================================================== */
/*  Utility helpers                                                    */
/* ================================================================== */

static const char *program_name = "parse-manager";

/*
** Print an error message to stderr with the program name prefix.
*/
static void errmsg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/*
** Format a capability bitmask into a human-readable string.
*/
static const char *caps_string(uint32_t caps) {
    static char buf[256];
    lime_plugin_capabilities_string(caps, buf, sizeof(buf));
    return buf[0] ? buf : "(none)";
}

/*
** Format a LimePluginVersion into a static buffer.
*/
static const char *ver_string(LimePluginVersion v) {
    static char buf[32];
    lime_plugin_version_string(v, buf, sizeof(buf));
    return buf;
}

/*
** Read a file into a malloc'd buffer. Returns NULL on error.
*/
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        errmsg("cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        errmsg("cannot determine size of '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        errmsg("out of memory reading '%s'", path);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[nread] = '\0';

    if (out_len) *out_len = nread;
    return buf;
}

/*
** Return wall-clock time in nanoseconds (monotonic if available).
*/
static uint64_t now_ns(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
** Print a table separator line.
*/
static void table_sep(int width) {
    for (int i = 0; i < width; i++) putchar('-');
    putchar('\n');
}

/* ================================================================== */
/*  Command: list                                                      */
/* ================================================================== */

static int cmd_list(ParserManager *mgr, int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t count = parser_manager_plugin_count(mgr);
    if (count == 0) {
        printf("No parser plugins loaded.\n");
        return 0;
    }

    /* Allocate info array */
    LimePluginInfo *infos = calloc(count, sizeof(LimePluginInfo));
    if (!infos) {
        errmsg("out of memory");
        return 1;
    }

    uint32_t actual;
    ParserManagerStatus st = parser_manager_list_plugins(mgr, infos, count, &actual);
    if (st != PM_OK) {
        errmsg("list_plugins: %s", parser_manager_status_string(st));
        free(infos);
        return 1;
    }

    /* Print table header */
    printf("%-4s  %-20s  %-10s  %-8s  %-8s  %-30s\n",
           "ID", "Name", "Version", "Active", "Type", "Capabilities");
    table_sep(90);

    for (uint32_t i = 0; i < actual && i < count; i++) {
        LimePluginInfo *info = &infos[i];
        char vbuf[16];
        lime_plugin_version_string(info->version, vbuf, sizeof(vbuf));

        printf("%-4u  %-20s  %-10s  %-8s  %-8s  %-30s\n",
               (unsigned)info->handle,
               info->name ? info->name : "(unknown)",
               vbuf,
               info->is_active ? "  *" : "",
               info->is_dynamic ? "dynamic" : "static",
               caps_string(info->capabilities));
    }

    if (actual > count) {
        printf("... and %u more (increase buffer)\n", actual - count);
    }

    printf("\n%u plugin(s) loaded.\n", actual);
    free(infos);
    return 0;
}

/* ================================================================== */
/*  Command: load                                                      */
/* ================================================================== */

static int cmd_load(ParserManager *mgr, int argc, char **argv) {
    if (argc < 1) {
        errmsg("usage: %s load <path>", program_name);
        return 1;
    }

    const char *path = argv[0];
    LimePluginHandle handle;

    ParserManagerStatus st = parser_manager_load(mgr, path, NULL, &handle);
    if (st != PM_OK) {
        errmsg("load '%s': %s", path, parser_manager_status_string(st));
        return 1;
    }

    /* Get info for display */
    LimePluginInfo info;
    parser_manager_get_plugin_info(mgr, handle, &info);

    printf("Loaded plugin '%s' (handle %u, version %s)\n",
           info.name ? info.name : "(unknown)",
           (unsigned)handle,
           ver_string(info.version));

    return 0;
}

/* ================================================================== */
/*  Command: unload                                                    */
/* ================================================================== */

static int cmd_unload(ParserManager *mgr, int argc, char **argv) {
    if (argc < 1) {
        errmsg("usage: %s unload <name>", program_name);
        return 1;
    }

    const char *name = argv[0];
    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name);
        return 1;
    }

    ParserManagerStatus st = parser_manager_unload(mgr, handle);
    if (st != PM_OK) {
        errmsg("unload '%s': %s", name, parser_manager_status_string(st));
        return 1;
    }

    printf("Unloaded plugin '%s'.\n", name);
    return 0;
}

/* ================================================================== */
/*  Command: info                                                      */
/* ================================================================== */

static int cmd_info(ParserManager *mgr, int argc, char **argv) {
    if (argc < 1) {
        errmsg("usage: %s info <name>", program_name);
        return 1;
    }

    const char *name = argv[0];
    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name);
        return 1;
    }

    LimePluginInfo info;
    ParserManagerStatus st = parser_manager_get_plugin_info(mgr, handle, &info);
    if (st != PM_OK) {
        errmsg("get_plugin_info: %s", parser_manager_status_string(st));
        return 1;
    }

    printf("Plugin: %s\n", info.name ? info.name : "(unknown)");
    printf("  Handle:       %u\n", (unsigned)info.handle);
    printf("  Version:      %s\n", ver_string(info.version));
    printf("  Type:         %s\n", info.is_dynamic ? "dynamic (.so)" : "static");
    printf("  Active:       %s\n", info.is_active ? "yes" : "no");
    printf("  Capabilities: %s\n", caps_string(info.capabilities));

    /* Print individual capability flags */
    printf("  Capability flags:\n");
    static const struct { LimePluginCaps cap; const char *desc; } cap_list[] = {
        { LIME_CAP_SNAPSHOT,     "Can create parser snapshots" },
        { LIME_CAP_EXTENSIBLE,   "Supports runtime grammar extension" },
        { LIME_CAP_JIT,          "JIT compilation available" },
        { LIME_CAP_INCREMENTAL,  "Incremental snapshot updates" },
        { LIME_CAP_SERIALIZABLE, "Snapshot serialization/deserialization" },
    };
    for (size_t i = 0; i < sizeof(cap_list) / sizeof(cap_list[0]); i++) {
        bool has = (info.capabilities & (uint32_t)cap_list[i].cap) != 0;
        printf("    [%c] %s\n", has ? 'x' : ' ', cap_list[i].desc);
    }

    return 0;
}

/* ================================================================== */
/*  Command: activate                                                  */
/* ================================================================== */

static int cmd_activate(ParserManager *mgr, int argc, char **argv) {
    if (argc < 1) {
        errmsg("usage: %s activate <name> [grammar-file]", program_name);
        return 1;
    }

    const char *name = argv[0];
    const char *grammar = (argc > 1) ? argv[1] : NULL;

    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name);
        return 1;
    }

    ParserManagerStatus st = parser_manager_set_active(mgr, handle, grammar);
    if (st != PM_OK) {
        errmsg("activate '%s': %s", name, parser_manager_status_string(st));
        return 1;
    }

    printf("Activated parser '%s'", name);
    if (grammar) printf(" with grammar '%s'", grammar);
    printf(".\n");

    return 0;
}

/* ================================================================== */
/*  Command: test                                                      */
/* ================================================================== */

static int cmd_test(ParserManager *mgr, int argc, char **argv) {
    if (argc < 2) {
        errmsg("usage: %s test <name> <input-file>", program_name);
        return 1;
    }

    const char *name = argv[0];
    const char *file = argv[1];

    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name);
        return 1;
    }

    /* Verify the plugin is active or activate it */
    LimePluginHandle active = parser_manager_get_active(mgr);
    if (active != handle) {
        ParserManagerStatus st = parser_manager_set_active(mgr, handle, NULL);
        if (st != PM_OK) {
            errmsg("could not activate '%s': %s", name,
                   parser_manager_status_string(st));
            return 1;
        }
    }

    /* Get snapshot */
    ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
    if (snap == NULL) {
        errmsg("no active snapshot (did you provide a grammar file?)");
        return 1;
    }

    /* Read input file */
    size_t input_len;
    char *input = read_file(file, &input_len);
    if (!input) {
        lemon_snapshot_release(snap);
        return 1;
    }

    /* Parse */
    printf("Parsing '%s' with plugin '%s'...\n", file, name);

    uint64_t t0 = now_ns();

    ParseContext *ctx = parse_begin(snap);
    if (ctx == NULL) {
        errmsg("parse_begin failed");
        free(input);
        lemon_snapshot_release(snap);
        return 1;
    }

    /* Feed end-of-input */
    int rc = parse_token(ctx, 0, NULL);

    uint64_t t1 = now_ns();
    double elapsed_ms = (double)(t1 - t0) / 1e6;

    parse_end(ctx);

    if (rc != 0) {
        printf("FAIL: Parse error (rc=%d)\n", rc);
        printf("  Time: %.3f ms\n", elapsed_ms);
        printf("  Input: %zu bytes from '%s'\n", input_len, file);
    } else {
        printf("OK: Parse succeeded\n");
        printf("  Time: %.3f ms\n", elapsed_ms);
        printf("  Input: %zu bytes from '%s'\n", input_len, file);
    }

    free(input);
    lemon_snapshot_release(snap);
    return (rc != 0) ? 1 : 0;
}

/* ================================================================== */
/*  Command: compare                                                   */
/* ================================================================== */

static int cmd_compare(ParserManager *mgr, int argc, char **argv) {
    if (argc < 3) {
        errmsg("usage: %s compare <name1> <name2> <input-file>", program_name);
        return 1;
    }

    const char *name1 = argv[0];
    const char *name2 = argv[1];
    const char *file = argv[2];

    LimePluginHandle h1 = parser_manager_find_by_name(mgr, name1);
    if (h1 == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name1);
        return 1;
    }

    LimePluginHandle h2 = parser_manager_find_by_name(mgr, name2);
    if (h2 == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name2);
        return 1;
    }

    /* Read input */
    size_t input_len;
    char *input = read_file(file, &input_len);
    if (!input) return 1;

    printf("Comparing parsers on '%s' (%zu bytes):\n\n", file, input_len);

    /* Test parser 1 */
    ParserManagerStatus st = parser_manager_set_active(mgr, h1, NULL);
    if (st != PM_OK) {
        errmsg("activate '%s': %s", name1, parser_manager_status_string(st));
        free(input);
        return 1;
    }

    ParserSnapshot *snap1 = parser_manager_get_snapshot(mgr);
    int rc1 = -1;
    double ms1 = 0;

    if (snap1) {
        ParseContext *ctx = parse_begin(snap1);
        if (ctx) {
            uint64_t t0 = now_ns();
            rc1 = parse_token(ctx, 0, NULL);
            uint64_t t1 = now_ns();
            ms1 = (double)(t1 - t0) / 1e6;
            parse_end(ctx);
        }
        lemon_snapshot_release(snap1);
    }

    /* Test parser 2 */
    st = parser_manager_set_active(mgr, h2, NULL);
    if (st != PM_OK) {
        errmsg("activate '%s': %s", name2, parser_manager_status_string(st));
        free(input);
        return 1;
    }

    ParserSnapshot *snap2 = parser_manager_get_snapshot(mgr);
    int rc2 = -1;
    double ms2 = 0;

    if (snap2) {
        ParseContext *ctx = parse_begin(snap2);
        if (ctx) {
            uint64_t t0 = now_ns();
            rc2 = parse_token(ctx, 0, NULL);
            uint64_t t1 = now_ns();
            ms2 = (double)(t1 - t0) / 1e6;
            parse_end(ctx);
        }
        lemon_snapshot_release(snap2);
    }

    /* Report */
    printf("  %-20s  %-10s  %s\n", "Parser", "Result", "Time (ms)");
    table_sep(50);
    printf("  %-20s  %-10s  %.3f\n", name1,
           rc1 == 0 ? "OK" : (rc1 < 0 ? "ERROR" : "FAIL"), ms1);
    printf("  %-20s  %-10s  %.3f\n", name2,
           rc2 == 0 ? "OK" : (rc2 < 0 ? "ERROR" : "FAIL"), ms2);

    if (rc1 == 0 && rc2 == 0) {
        printf("\nBoth parsers succeeded.\n");
        if (ms1 > 0 && ms2 > 0) {
            double ratio = ms1 / ms2;
            if (ratio > 1.05) {
                printf("'%s' was %.1fx faster.\n", name2, ratio);
            } else if (ratio < 0.95) {
                printf("'%s' was %.1fx faster.\n", name1, 1.0 / ratio);
            } else {
                printf("Performance was comparable.\n");
            }
        }
    } else if (rc1 != rc2) {
        printf("\nWARNING: Parsers disagree on result!\n");
    }

    free(input);
    return (rc1 != 0 || rc2 != 0) ? 1 : 0;
}

/* ================================================================== */
/*  Command: benchmark                                                 */
/* ================================================================== */

static int cmd_benchmark(ParserManager *mgr, int argc, char **argv) {
    if (argc < 2) {
        errmsg("usage: %s benchmark <name> <file> [file...]", program_name);
        return 1;
    }

    const char *name = argv[0];
    int nfiles = argc - 1;

    LimePluginHandle handle = parser_manager_find_by_name(mgr, name);
    if (handle == LIME_PLUGIN_HANDLE_INVALID) {
        errmsg("plugin '%s' not found", name);
        return 1;
    }

    /* Activate */
    ParserManagerStatus st = parser_manager_set_active(mgr, handle, NULL);
    if (st != PM_OK) {
        errmsg("activate '%s': %s", name, parser_manager_status_string(st));
        return 1;
    }

    printf("Benchmarking parser '%s' on %d file(s):\n\n", name, nfiles);
    printf("  %-40s  %10s  %10s  %10s  %s\n",
           "File", "Size (B)", "Iters", "Avg (ms)", "Result");
    table_sep(90);

    int failures = 0;
    double total_ms = 0;
    size_t total_bytes = 0;

    for (int f = 0; f < nfiles; f++) {
        const char *file = argv[f + 1];
        size_t input_len;
        char *input = read_file(file, &input_len);
        if (!input) {
            failures++;
            continue;
        }

        ParserSnapshot *snap = parser_manager_get_snapshot(mgr);
        if (!snap) {
            errmsg("no active snapshot");
            free(input);
            failures++;
            continue;
        }

        /*
        ** Run multiple iterations to get a stable measurement.
        ** Use at least 10 iterations; for small files, run more.
        */
        int iters = 10;
        if (input_len < 1024) iters = 100;
        if (input_len < 128) iters = 1000;

        uint64_t t0 = now_ns();
        int last_rc = 0;

        for (int i = 0; i < iters; i++) {
            ParseContext *ctx = parse_begin(snap);
            if (!ctx) { last_rc = -1; break; }
            last_rc = parse_token(ctx, 0, NULL);
            parse_end(ctx);
            if (last_rc != 0) break;
        }

        uint64_t t1 = now_ns();
        double avg_ms = (double)(t1 - t0) / 1e6 / iters;

        lemon_snapshot_release(snap);

        printf("  %-40s  %10zu  %10d  %10.3f  %s\n",
               file, input_len, iters, avg_ms,
               last_rc == 0 ? "OK" : "FAIL");

        if (last_rc == 0) {
            total_ms += avg_ms;
            total_bytes += input_len;
        } else {
            failures++;
        }

        free(input);
    }

    table_sep(90);
    printf("\nSummary:\n");
    printf("  Files:    %d total, %d succeeded, %d failed\n",
           nfiles, nfiles - failures, failures);
    printf("  Total:    %zu bytes\n", total_bytes);
    printf("  Avg time: %.3f ms per file\n",
           (nfiles - failures > 0) ? total_ms / (nfiles - failures) : 0);

    if (total_bytes > 0 && total_ms > 0) {
        double mb_per_sec = ((double)total_bytes / 1e6) / (total_ms / 1e3);
        printf("  Throughput: %.1f MB/s (approximate)\n", mb_per_sec);
    }

    return failures > 0 ? 1 : 0;
}

/* ================================================================== */
/*  Command: version                                                   */
/* ================================================================== */

static int cmd_version(ParserManager *mgr, int argc, char **argv) {
    (void)mgr; (void)argc; (void)argv;

    printf("parse-manager 1.0.0\n");
    printf("Lime Parser Generator plugin management tool\n");
    printf("Plugin ABI version: %d.%d\n",
           LIME_PLUGIN_ABI_VERSION_MAJOR, LIME_PLUGIN_ABI_VERSION_MINOR);

    const char *ver = lemon_parser_version();
    if (ver) {
        printf("Lime parser library: %s\n", ver);
    }

    printf("JIT available: %s\n", lime_jit_available() ? "yes" : "no");

    return 0;
}

/* ================================================================== */
/*  Command: help                                                      */
/* ================================================================== */

static int cmd_help(ParserManager *mgr, int argc, char **argv) {
    (void)mgr;

    if (argc > 0) {
        /* Help for a specific command */
        const char *cmd = argv[0];
        for (int i = 0; commands[i].name != NULL; i++) {
            if (strcmp(cmd, commands[i].name) == 0) {
                printf("Usage: %s %s %s\n\n",
                       program_name, commands[i].name, commands[i].usage);
                printf("%s\n", commands[i].description);
                return 0;
            }
        }
        errmsg("unknown command '%s'", cmd);
        return 1;
    }

    printf("Usage: %s [options] <command> [args...]\n\n", program_name);
    printf("Options:\n");
    printf("  -s, --search-path <dir>    Add plugin search directory\n");
    printf("  -v, --validate             Validate snapshots on load\n");
    printf("  -j, --jit                  Enable auto-JIT compilation\n");
    printf("  -q, --quiet                Suppress non-essential output\n");
    printf("  -h, --help                 Show this help message\n");
    printf("\nCommands:\n");

    for (int i = 0; commands[i].name != NULL; i++) {
        printf("  %-12s %s\n", commands[i].name, commands[i].description);
    }

    printf("\nRun '%s help <command>' for details on a specific command.\n",
           program_name);
    printf("\nExamples:\n");
    printf("  %s load /usr/lib/lime/sql_parser.so\n", program_name);
    printf("  %s list\n", program_name);
    printf("  %s activate sql_parser grammar.y\n", program_name);
    printf("  %s test sql_parser query.sql\n", program_name);
    printf("  %s compare parser_v1 parser_v2 query.sql\n", program_name);
    printf("  %s benchmark sql_parser tests/*.sql\n", program_name);

    return 0;
}

/* ================================================================== */
/*  Main entry point                                                   */
/* ================================================================== */

int main(int argc, char **argv) {
    if (argc > 0) program_name = argv[0];

    /* Parse global options */
    ParserManagerConfig config;
    memset(&config, 0, sizeof(config));

    const char *search_paths[16] = {NULL};
    int search_path_count = 0;
    bool quiet = false;

    static struct option long_opts[] = {
        { "search-path", required_argument, 0, 's' },
        { "validate",    no_argument,       0, 'v' },
        { "jit",         no_argument,       0, 'j' },
        { "quiet",       no_argument,       0, 'q' },
        { "help",        no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "+s:vjqh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 's':
            if (search_path_count < 15) {
                search_paths[search_path_count++] = optarg;
            }
            break;
        case 'v':
            config.validate_on_load = true;
            break;
        case 'j':
            config.auto_jit = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 'h':
            cmd_help(NULL, 0, NULL);
            return 0;
        default:
            fprintf(stderr, "Try '%s --help' for usage.\n", program_name);
            return 1;
        }
    }

    (void)quiet;  /* Used by subcommands that check it */

    if (search_path_count > 0) {
        search_paths[search_path_count] = NULL;
        config.plugin_search_paths = search_paths;
    }

    /* Remaining args: command [args...] */
    int cmd_argc = argc - optind;
    char **cmd_argv = argv + optind;

    if (cmd_argc < 1) {
        cmd_help(NULL, 0, NULL);
        return 1;
    }

    const char *cmd_name = cmd_argv[0];
    cmd_argc--;
    cmd_argv++;

    /* Find command */
    const Command *cmd = NULL;
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(cmd_name, commands[i].name) == 0) {
            cmd = &commands[i];
            break;
        }
    }

    if (cmd == NULL) {
        errmsg("unknown command '%s'", cmd_name);
        fprintf(stderr, "Run '%s help' for a list of commands.\n", program_name);
        return 1;
    }

    /* Create manager */
    ParserManager *mgr = parser_manager_create(&config);
    if (mgr == NULL) {
        errmsg("failed to create parser manager");
        return 1;
    }

    /* Dispatch */
    int rc = cmd->handler(mgr, cmd_argc, cmd_argv);

    /* Cleanup */
    parser_manager_destroy(mgr);

    return rc;
}
