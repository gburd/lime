/*
** tests/test_lex_cli_argv.c -- lime_lex_cli_hook() must not write to the
** caller's argv[].
**
** On Linux the argv[] pointer array is followed directly by environ[], so a
** CLI front end that builds a LONGER argument vector in place overwrites
** environ[0] (usually PATH): that was Track D's yacc_personality() bug.
** This test lays argv out the same way (argv pointers, NULL, then an
** "environ" block in the same array), runs the lex personality with
** arguments that a rewrite would expand (lime --lex, and a lex argv[0] with
** flex flags), and checks every slot is unchanged afterwards.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int lime_lex_cli_hook(int argc, char **argv);

/* The lex library reads these lime.c globals; lime.c is not linked here. */
int g_lime_lex_safe_flag, g_lime_per_token_dfa_flag, g_lime_rustlex_memchr_flag;
int g_lime_rustlex_simd_flag, g_lime_rust_no_std, g_lime_skin_flex_flag;
int g_lime_skin_logos_flag, g_lime_rustlex_flag;

static int fails;

/* argv0 + args, NULL, then two fake environ entries, NULL. */
static void run(const char *what, const char *const *args, int nargs) {
    char *block[32];
    char *copy[32];
    int n = 0;
    for (int k = 0; k < nargs; k++) block[n++] = strdup(args[k]);
    block[n++] = NULL;
    int env0 = n;
    block[n++] = strdup("PATH=/canary/bin");
    block[n++] = strdup("HOME=/canary");
    block[n++] = NULL;
    memcpy(copy, block, (size_t)n * sizeof *block);
    char *texts[32];
    for (int k = 0; k < n; k++) texts[k] = block[k] ? strdup(block[k]) : NULL;

    int rc = lime_lex_cli_hook(nargs, block);

    int bad = 0;
    for (int k = 0; k < n; k++) {
        if (block[k] != copy[k]) bad = 1;
        else if (block[k] && strcmp(block[k], texts[k])) bad = 1;
    }
    if (rc < 0) {
        printf("FAIL %s: the lex front end did not take the run\n", what);
        fails++;
    } else if (bad || strcmp(block[env0], "PATH=/canary/bin")) {
        printf("FAIL %s: the hook wrote into argv[]/environ (rc=%d)\n", what, rc);
        fails++;
    } else {
        printf("ok   %s (rc=%d)\n", what, rc);
    }
    for (int k = 0; k < n; k++) {
        free(copy[k]);
        free(texts[k]);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s SCANNER.l\n", argv[0]);
        return 2;
    }
    const char *l = argv[1];
    {
        const char *a[] = { "lime", "--lex", "-t", "-L", "--noline", l };
        run("lime --lex -t", a, 6);
    }
    {
        const char *a[] = { "/some/dir/lime-lex", "-t", "-B", "-Pzz", l };
        run("lime-lex by path", a, 5);
    }
    {
        const char *a[] = { "flex", "-t", l };
        run("flex personality", a, 3);
    }
    {
        const char *a[] = { "lime", "-X", "--flex", "-d/nonexistent-dir-for-test", l };
        run("lime -X on a .l", a, 5);
    }
    printf("fails=%d\n", fails);
    return fails != 0;
}
