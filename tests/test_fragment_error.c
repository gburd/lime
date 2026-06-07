/*
** tests/test_fragment_error.c -- regression for the v1.4.0 fragment
** diagnostic (open-items.md item: formatter "Empty grammar" on
** %include-able fragments).
**
** Before: a %token-only fragment (e.g. pg/tokens.lime) compiled or
** formatted standalone produced the bare "Empty grammar." message,
** which reads like a parser bug rather than user error.
**
** After: when the input has declarations (Symbol_count() > 1) but no
** rules, lime emits a message that names the file and explains the
** input looks like a fragment meant to be %include-d.  A genuinely
** empty input still gets the bare "Empty grammar." message.
**
** Sub-tests:
**   1. token-only fragment, compile  -> "grammar fragment" message,
**      exit 1, does NOT say bare "Empty grammar."
**   2. token-only fragment, -F       -> same fragment message, exit 1
**   3. genuinely empty file, compile -> "Empty grammar." exit 1
**   4. normal grammar, compile       -> exit 0 (control)
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int g_fail = 0;

#define CHECK(cond, msg) do {                                         \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
} while (0)

/* Run `lime <args...>` capturing combined stdout+stderr into buf.
** Returns the process exit code (or -1 on spawn failure). */
static int run_lime(const char *lime, const char *arg1, const char *arg2,
                    char *buf, size_t buflen) {
    char cmd[2048];
    if (arg2)
        snprintf(cmd, sizeof(cmd), "%s %s %s 2>&1", lime, arg1, arg2);
    else
        snprintf(cmd, sizeof(cmd), "%s %s 2>&1", lime, arg1);
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    size_t n = fread(buf, 1, buflen - 1, p);
    buf[n] = '\0';
    int rc = pclose(p);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(content, f);
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-lime>\n", argv[0]);
        return 2;
    }
    const char *lime = argv[1];

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/test_fragment_error", tmpdir);
    mkdir(dir, 0755);

    char frag[512], empty[512], ok[512];
    snprintf(frag,  sizeof(frag),  "%s/frag.lime",  dir);
    snprintf(empty, sizeof(empty), "%s/empty.lime", dir);
    snprintf(ok,    sizeof(ok),    "%s/ok.lime",    dir);

    write_file(frag,
        "%token NUM PLUS MINUS.\n"
        "%token_type {int}\n");
    write_file(empty, "");
    write_file(ok,
        "%token NUM.\n"
        "%token_type {int}\n"
        "prog ::= expr.\n"
        "expr ::= NUM.\n");

    char buf[4096];
    int rc;

    /* 1. fragment compile */
    rc = run_lime(lime, frag, NULL, buf, sizeof(buf));
    CHECK(rc == 1, "fragment compile exits 1");
    CHECK(strstr(buf, "grammar fragment") != NULL,
          "fragment compile mentions 'grammar fragment'");
    CHECK(strstr(buf, "%include") != NULL,
          "fragment compile mentions %include");
    CHECK(strstr(buf, "Empty grammar.") == NULL,
          "fragment compile does NOT print bare 'Empty grammar.'");

    /* 2. fragment -F */
    rc = run_lime(lime, "-F", frag, buf, sizeof(buf));
    CHECK(rc == 1, "fragment -F exits 1");
    CHECK(strstr(buf, "grammar fragment") != NULL,
          "fragment -F mentions 'grammar fragment'");

    /* 3. genuinely empty -> bare message */
    rc = run_lime(lime, empty, NULL, buf, sizeof(buf));
    CHECK(rc == 1, "empty compile exits 1");
    CHECK(strstr(buf, "Empty grammar.") != NULL,
          "empty compile prints bare 'Empty grammar.'");
    CHECK(strstr(buf, "grammar fragment") == NULL,
          "empty compile does NOT use the fragment message");

    /* 4. normal grammar: -F formats successfully (exit 0, control).
    ** Use -F rather than full compile so the test needs no lempar.c
    ** template path -- the point is only to confirm a grammar WITH
    ** rules is not misclassified as a fragment. */
    rc = run_lime(lime, "-F", ok, buf, sizeof(buf));
    CHECK(rc == 0, "normal grammar -F succeeds (exit 0)");
    CHECK(strstr(buf, "grammar fragment") == NULL,
          "normal grammar not flagged as fragment");
    CHECK(strstr(buf, "Empty grammar.") == NULL,
          "normal grammar not flagged empty");

    if (g_fail) {
        fprintf(stderr, "FAIL: %d check(s) failed (artefacts in %s)\n", g_fail, dir);
        return 1;
    }
    fprintf(stdout, "OK: fragment_error (4 sub-tests)\n");
    return 0;
}
