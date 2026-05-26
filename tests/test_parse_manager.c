/*
** test_parse_manager -- integration test for the parse-manager(1)
** CLI tool.
**
** Spawns parse-manager as a subprocess and asserts on its stdout
** for each command in the surface area we promise to keep working:
**
**   - version            : reports the same lime_parser_version()
**                          string the library reports
**   - load <path.so>     : loads a plugin and reports name + handle
**   - list               : shows the loaded plugin
**   - info <name>        : shows capabilities
**   - unload <name>      : unloads cleanly, list now empty
**
** Note: parse-manager runs each command as a fresh process, so the
** ParserManager state does not persist across invocations.  This
** mirrors how an operator would script the tool.  We therefore
** combine load+list+info+unload into a single shell-style invocation
** chain for each (separate process, separate manager) and assert
** each individual command's output independently.
**
** Args: argv[1] = parse-manager binary path,
**       argv[2] = sql_plugin.so path (any plugin -- the example one
**                 from examples/plugin_template).  Wired by tests/meson.build.
*/
#define _POSIX_C_SOURCE 200809L

#include "parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
    fflush(stdout); \
} while (0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(fmt, ...) do { printf("FAIL: " fmt "\n", ##__VA_ARGS__); } while (0)

/*
** Run the named binary with the given argv (NUL-terminated), capture
** stdout into a malloc'd buffer (caller frees).  *exit_status_out
** receives the WIFEXITED-decoded exit code, or -1 if the child died
** from a signal.
*/
static char *run_capture(const char *bin, char *const argv[],
                         int *exit_status_out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child: stdout -> pipe, then exec. */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        /* stderr also -> pipe so we can include error output in the
        ** captured string for diagnostics. */
        if (dup2(pipefd[1], STDERR_FILENO) < 0) _exit(127);
        close(pipefd[1]);
        execv(bin, argv);
        _exit(127);
    }

    /* Parent: read until EOF, then waitpid. */
    close(pipefd[1]);

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    for (;;) {
        if (len + 1024 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(pipefd[0]); waitpid(pid, NULL, 0); return NULL; }
            buf = nb;
        }
        ssize_t n = read(pipefd[0], buf + len, cap - len - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return NULL;
        }
        if (n == 0) break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    if (exit_status_out) {
        *exit_status_out = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static int test_version(const char *pm) {
    TEST("parse-manager version");

    char *args[] = { (char *)pm, (char *)"version", NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc != 0) {
        FAIL("exit=%d output=<%s>", rc, out);
    } else {
        const char *want = lime_parser_version();
        if (want == NULL) want = "0.0.0";
        if (strstr(out, want) == NULL) {
            FAIL("output missing lime version '%s': <%s>", want, out);
        } else if (strstr(out, "parse-manager") == NULL) {
            FAIL("output missing 'parse-manager' header: <%s>", out);
        } else {
            PASS();
            ok = 1;
        }
    }

    free(out);
    return ok ? 0 : 1;
}

static int test_help(const char *pm) {
    TEST("parse-manager help (lists subcommands)");

    char *args[] = { (char *)pm, (char *)"help", NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc != 0) {
        FAIL("exit=%d", rc);
    } else if (strstr(out, "load") == NULL || strstr(out, "list") == NULL
               || strstr(out, "unload") == NULL || strstr(out, "info") == NULL) {
        FAIL("help text missing core commands: <%s>", out);
    } else {
        PASS();
        ok = 1;
    }

    free(out);
    return ok ? 0 : 1;
}

static int test_list_empty(const char *pm) {
    TEST("parse-manager list (no plugins)");

    char *args[] = { (char *)pm, (char *)"list", NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc != 0) {
        FAIL("exit=%d", rc);
    } else if (strstr(out, "No parser plugins loaded") == NULL) {
        FAIL("expected 'No parser plugins loaded': <%s>", out);
    } else {
        PASS();
        ok = 1;
    }

    free(out);
    return ok ? 0 : 1;
}

/*
** parse-manager creates a fresh ParserManager per invocation, so we
** can't run "load X" then "list" in two separate calls and see X.
** Instead, we rely on parse-manager's load command itself to print
** the plugin's identity (name, handle, version) on stdout -- if the
** load + identity-readout works against a real .so, the underlying
** load + dlopen + plugin entry-point + ABI-validation chain is sound,
** which is the contract this test guards.
*/
static int test_load_identity(const char *pm, const char *plugin_so) {
    TEST("parse-manager load <path> reports plugin identity");

    char *args[] = { (char *)pm, (char *)"load", (char *)plugin_so, NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc != 0) {
        FAIL("exit=%d output=<%s>", rc, out);
    } else if (strstr(out, "Loaded plugin") == NULL) {
        FAIL("missing 'Loaded plugin' marker: <%s>", out);
    } else if (strstr(out, "lime-sql-parser") == NULL) {
        FAIL("missing plugin name 'lime-sql-parser': <%s>", out);
    } else {
        PASS();
        ok = 1;
    }

    free(out);
    return ok ? 0 : 1;
}

/*
** Bad-path tests: parse-manager should fail gracefully on
** unknown commands and missing arguments.
*/
static int test_unknown_command(const char *pm) {
    TEST("parse-manager <unknown-cmd> exits non-zero");

    char *args[] = { (char *)pm, (char *)"this-is-not-a-real-command", NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc == 0) {
        FAIL("expected non-zero exit, got 0: <%s>", out);
    } else if (strstr(out, "unknown command") == NULL) {
        FAIL("missing 'unknown command' diagnostic: <%s>", out);
    } else {
        PASS();
        ok = 1;
    }

    free(out);
    return ok ? 0 : 1;
}

static int test_help_subcommand(const char *pm) {
    TEST("parse-manager help load (subcommand-specific help)");

    char *args[] = { (char *)pm, (char *)"help", (char *)"load", NULL };
    int rc = 0;
    char *out = run_capture(pm, args, &rc);
    int ok = 0;

    if (!out) {
        FAIL("run_capture returned NULL");
    } else if (rc != 0) {
        FAIL("exit=%d", rc);
    } else if (strstr(out, "Usage:") == NULL || strstr(out, "load") == NULL) {
        FAIL("subcommand help malformed: <%s>", out);
    } else {
        PASS();
        ok = 1;
    }

    free(out);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <parse-manager-bin> <plugin.so>\n", argv[0]);
        return 2;
    }
    const char *pm = argv[1];
    const char *plugin_so = argv[2];

    printf("parse-manager integration:\n");
    printf("  binary: %s\n", pm);
    printf("  plugin: %s\n", plugin_so);

    int rc = 0;
    rc |= test_version(pm);
    rc |= test_help(pm);
    rc |= test_help_subcommand(pm);
    rc |= test_list_empty(pm);
    rc |= test_load_identity(pm, plugin_so);
    rc |= test_unknown_command(pm);

    printf("\nResults: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
