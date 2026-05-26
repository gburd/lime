/*
 * lsp_main.c -- entry point for `lime-lsp`, the Lime grammar
 * Language Server.
 *
 * The binary is dependency-free aside from libc.  It does NOT
 * link the lime parser library; diagnostics are produced by
 * shelling out to the `lime` binary, which keeps the LSP a thin
 * shim and lets it follow lime upgrades without recompilation.
 *
 * Configuration:
 *
 *   --lime PATH           path to the lime binary (default: $LIME_BIN, then "lime")
 *   --log  PATH           append protocol-level trace to PATH
 *   --version             print version + exit
 *   --help                print usage + exit
 *
 * Editor recipes live in editors/lime-lsp-config.md.  The full
 * capability surface is in docs/LSP.md.
 */

#include "lsp_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LIME_VERSION_STRING
#define LIME_VERSION_STRING "0.0.0"
#endif

static void print_usage(FILE *out, const char *progname) {
    fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Lime grammar Language Server (speaks LSP over stdio).\n"
        "\n"
        "Options:\n"
        "  --lime PATH    Path to the lime binary used for diagnostics\n"
        "                 (default: $LIME_BIN, then `lime` on PATH).\n"
        "  --log  PATH    Append a protocol-level trace to PATH.\n"
        "  --version      Print version and exit.\n"
        "  --help         Print this message and exit.\n"
        "\n"
        "The server reads JSON-RPC 2.0 framed messages from stdin\n"
        "and writes responses + notifications to stdout.  Logs (if\n"
        "any) go to the file named by --log.\n",
        progname);
}

int main(int argc, char **argv) {
    const char *lime_bin = NULL;
    const char *log_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            printf("lime-lsp %s\n", LIME_VERSION_STRING);
            return 0;
        }
        if (strcmp(a, "--lime") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --lime requires an argument\n", argv[0]);
                return 2;
            }
            lime_bin = argv[++i];
            continue;
        }
        if (strncmp(a, "--lime=", 7) == 0) {
            lime_bin = a + 7;
            continue;
        }
        if (strcmp(a, "--log") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --log requires an argument\n", argv[0]);
                return 2;
            }
            log_path = argv[++i];
            continue;
        }
        if (strncmp(a, "--log=", 6) == 0) {
            log_path = a + 6;
            continue;
        }
        fprintf(stderr, "%s: unknown option `%s` (try --help)\n",
                argv[0], a);
        return 2;
    }

    if (!lime_bin) lime_bin = getenv("LIME_BIN");

    lsp_server s;
    lsp_server_init(&s);
    if (lime_bin && *lime_bin) {
        s.lime_bin = strdup(lime_bin);
        if (!s.lime_bin) {
            fprintf(stderr, "%s: out of memory\n", argv[0]);
            return 1;
        }
    }
    if (log_path) {
        s.log = fopen(log_path, "a");
        if (!s.log) {
            /* Soft-fail; logs aren't required for correctness. */
            fprintf(stderr, "%s: warning: could not open log file `%s`\n",
                    argv[0], log_path);
        }
    }

    int rc = lsp_server_run(&s);

    if (s.log) fclose(s.log);
    lsp_server_free(&s);
    return rc;
}
