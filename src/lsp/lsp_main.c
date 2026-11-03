/*
 * lsp_main.c -- entry point for `lime-lsp`, the Lime grammar
 * Language Server.
 *
 * Diagnostics and formatting run in process through liblime_compiler
 * (lime_lint_grammar_*_in_process, lime_format_grammar_in_process); the
 * server never runs the `lime` binary.
 *
 * Configuration:
 *
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
        /* --lime PATH named the `lime` binary the server used to run.
        ** It runs none now; accept the option so existing editor configs
        ** keep working, and say it is ignored. */
        if (strcmp(a, "--lime") == 0 || strncmp(a, "--lime=", 7) == 0) {
            if (strcmp(a, "--lime") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "%s: --lime requires an argument\n", argv[0]);
                    return 2;
                }
                i++;
            }
            fprintf(stderr, "%s: note: --lime is ignored; lime-lsp runs no "
                    "lime binary\n", argv[0]);
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

    lsp_server s;
    lsp_server_init(&s);
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
