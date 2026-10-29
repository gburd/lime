/*
** test_lalr_identity.c -- the generator's parse tables must not change.
**
** For each grammar in the golden file, build the tables two ways and
** hash them:
**
**   cli   run the lime binary (-q -l) and hash the table text of the
**         emitted parser: the YY* control #defines, yy_action ..
**         yy_default, yyFallback, yyRuleInfoLhs and yyRuleInfoNRhs.
**         This is the emitted-C byte-identity guarantee for the tables.
**   proc  lime_compile_grammar_in_process_ex(), then hash the snapshot's
**         tables and range constants.  This path keeps unit reductions
**         and packs its own action table, so it is a second, different
**         table set.
**
** Both hashes must equal the checked-in golden.  This is the gate for
** generator speed work (state construction, follow sets, action-table
** packing): those changes must leave every table bit-identical.  A
** golden is changed only for a deliberate, reviewed change of output,
** never to make a speedup pass.  LIME_LALR_IDENTITY_PRINT=1 prints the
** computed lines in golden-file format.
**
** usage: test_lalr_identity <lime> <limpar.c> <source-root> <golden>
*/
#define _POSIX_C_SOURCE 200809L
#include "lime_compiler.h"
#include "snapshot.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FNV_OFFSET 1469598103934665603ULL
static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const unsigned char *q = p;
    for (size_t i = 0; i < n; i++) h = (h ^ q[i]) * 1099511628211ULL;
    return h;
}
static uint64_t fnv_u32(uint64_t h, uint32_t v) { return fnv(h, &v, sizeof v); }

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    char *buf = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0) buf = malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf == NULL) return NULL;
    buf[n] = '\0';
    if (len) *len = (size_t)n;
    return buf;
}

/* Hash from the line holding `from` through the line holding `to`; with
** `only`, just the lines in that range that start with it.  Returns 0
** (and leaves *h alone) if either marker is missing. */
static int hash_region(uint64_t *h, const char *text, const char *from, const char *to,
                       const char *only) {
    const char *a = strstr(text, from);
    const char *b = a ? strstr(a, to) : NULL;
    if (b == NULL) return 0;
    while (a > text && a[-1] != '\n') a--;
    const char *e = strchr(b, '\n');
    e = e ? e + 1 : b + strlen(b);
    while (a < e) {
        const char *nl = memchr(a, '\n', (size_t)(e - a));
        const char *next = nl ? nl + 1 : e;
        if (only == NULL || strncmp(a, only, strlen(only)) == 0)
            *h = fnv(*h, a, (size_t)(next - a));
        a = next;
    }
    return 1;
}

/* cli: the emitted parser's tables.  Returns 0 on success. */
static int cli_hash(const char *lime, const char *tmpl, const char *grammar, uint64_t *out) {
    char dir[] = "/tmp/lime_identity_XXXXXX";
    if (mkdtemp(dir) == NULL) return -1;
    const char *base = strrchr(grammar, '/');
    base = base ? base + 1 : grammar;
    const char *dot = strrchr(base, '.');
    int nstem = dot ? (int)(dot - base) : (int)strlen(base);
    char cmd[16384], cpath[4096], hpath[4096];
    if (snprintf(cmd, sizeof cmd, "'%s' -q -l '-T%s' '-d%s' '%s' >/dev/null 2>&1", lime, tmpl, dir,
                 grammar) >= (int)sizeof cmd
        || snprintf(cpath, sizeof cpath, "%s/%.*s.c", dir, nstem, base) >= (int)sizeof cpath
        || snprintf(hpath, sizeof hpath, "%s/%.*s.h", dir, nstem, base) >= (int)sizeof hpath) {
        rmdir(dir);
        return -1;
    }
    /* Nonzero for grammars with unexpected conflicts; the .c is still written. */
    if (system(cmd) == -1) {
        rmdir(dir);
        return -1;
    }
    char *c = slurp(cpath, NULL);
    int rc = -1;
    if (c != NULL) {
        uint64_t h = FNV_OFFSET;
        /* Control block: only the YY constants, not %union / %extra_argument
        ** text, which comes from the grammar reader, not the tables. */
        int ok = hash_region(&h, c, "Begin control #defines", "End control #defines", "#define YY")
                 && hash_region(&h, c, "Begin parsing tables", "End of lemon-generated parsing tables",
                                NULL)
                 && hash_region(&h, c, "yyFallback[] = {", "};", NULL)
                 && hash_region(&h, c, "yyRuleInfoLhs[] = {", "};", NULL)
                 && hash_region(&h, c, "yyRuleInfoNRhs[] = {", "};", NULL);
        if (ok) {
            *out = h;
            rc = 0;
        }
        free(c);
    }
    unlink(cpath);
    unlink(hpath);
    rmdir(dir);
    return rc;
}

/* proc: the in-process snapshot's tables.  Returns 0 on success. */
static int proc_hash(const char *grammar, uint64_t *out, uint32_t *nstate, uint32_t *nact) {
    size_t len = 0;
    char *text = slurp(grammar, &len);
    if (text == NULL) return -1;
    ParserSnapshot *s = NULL;
    char *err = NULL;
    int nconflict = 0;
    int rc = lime_compile_grammar_in_process_ex(text, len, &s, &err, &nconflict);
    free(text);
    free(err);
    if (rc != 0 || s == NULL) return -1;
    uint64_t h = FNV_OFFSET;
    const uint32_t scalars[] = {
        s->nstate, s->nrule, s->nsymbol, s->nterminal, s->action_count, s->lookahead_count,
        s->yy_max_shift, s->yy_min_shiftreduce, s->yy_max_shiftreduce, s->yy_error_action,
        s->yy_accept_action, s->yy_no_action, s->yy_min_reduce, s->yy_ntoken, s->yy_first_token,
        s->nfallback, (uint32_t)nconflict,
    };
    for (size_t i = 0; i < sizeof scalars / sizeof scalars[0]; i++) h = fnv_u32(h, scalars[i]);
    h = fnv(h, s->yy_action, s->action_count * sizeof s->yy_action[0]);
    h = fnv(h, s->yy_lookahead, s->lookahead_count * sizeof s->yy_lookahead[0]);
    h = fnv(h, s->yy_shift_ofst, s->nstate * sizeof s->yy_shift_ofst[0]);
    h = fnv(h, s->yy_reduce_ofst, s->nstate * sizeof s->yy_reduce_ofst[0]);
    h = fnv(h, s->yy_default, s->nstate * sizeof s->yy_default[0]);
    h = fnv(h, s->yy_rule_info_lhs, s->nrule * sizeof s->yy_rule_info_lhs[0]);
    h = fnv(h, s->yy_rule_info_nrhs, s->nrule * sizeof s->yy_rule_info_nrhs[0]);
    if (s->yy_fallback) h = fnv(h, s->yy_fallback, s->nfallback * sizeof s->yy_fallback[0]);
    *out = h;
    *nstate = s->nstate;
    *nact = s->action_count;
    snapshot_release(s);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <lime> <limpar.c> <source-root> <golden>\n", argv[0]);
        return 2;
    }
    setbuf(stdout, NULL);
    FILE *g = fopen(argv[4], "r");
    if (g == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[4]);
        return 2;
    }
    int print = getenv("LIME_LALR_IDENTITY_PRINT") != NULL;
    int ngrammar = 0, nfail = 0;
    char line[4096];
    while (fgets(line, sizeof line, g)) {
        char rel[2048], want_cli[64], want_proc[64];
        if (line[0] == '#' || sscanf(line, "%2047s %63s %63s", rel, want_cli, want_proc) != 3)
            continue;
        char path[4096];
        if (snprintf(path, sizeof path, "%s/%s", argv[3], rel) >= (int)sizeof path) continue;
        uint64_t hc = 0, hp = 0;
        uint32_t ns = 0, na = 0;
        char got_cli[64] = "-", got_proc[64] = "-";
        if (cli_hash(argv[1], argv[2], path, &hc) == 0)
            snprintf(got_cli, sizeof got_cli, "%016" PRIx64, hc);
        if (proc_hash(path, &hp, &ns, &na) == 0)
            snprintf(got_proc, sizeof got_proc, "%016" PRIx64, hp);
        int ok = strcmp(got_cli, want_cli) == 0 && strcmp(got_proc, want_proc) == 0;
        ngrammar++;
        if (!ok) nfail++;
        if (print)
            printf("%-48s %s %s\n", rel, got_cli, got_proc);
        else
            printf("  [%s] %-46s cli %s proc %s (%u states, %u actions)\n", ok ? "PASS" : "FAIL",
                   rel, got_cli, got_proc, ns, na);
        if (!ok && !print)
            printf("         expected   cli %s proc %s\n", want_cli, want_proc);
    }
    fclose(g);
    printf("=== Summary === %d grammars, %d differ from the golden tables\n", ngrammar, nfail);
    return ngrammar > 0 && nfail == 0 ? 0 : 1;
}
