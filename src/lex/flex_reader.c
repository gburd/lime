/*
** src/lex/flex_reader.c -- lex/flex (.l) front end.  See flex_reader.h.
**
** The reader follows flex 2.6.4's own scanner (flex/src/scan.l) and
** grammar (parse.y) closely, because "drop-in" means accepting what flex
** accepts and meaning what flex means:
**
**   - {name} references are expanded textually at the point of use, as
**     flex does (it unput()s "(" def ")"), so a definition is re-scanned
**     in the context it is used in.  A definition starting with ^ or
**     ending with $ is spliced without the parentheses.
**   - ^ is the beginning-of-line operator only as the first character of
**     a rule (it then anchors the whole rule); elsewhere it is literal.
**     $ is the end-of-line operator only when followed by a blank or the
**     end of the line (it anchors the whole rule); elsewhere literal.
**   - Repetition {n,m} binds to the preceding singleton, or to the whole
**     preceding series under %option posix-compat / -X.  "..." is one
**     singleton.  Repeated quantifiers nest: a*+ is (a*)+.
**   - Start-condition scopes nest by union, <*> means every condition,
**     an unprefixed <<EOF>> applies to every condition that does not yet
**     have one, and a second <<EOF>> for a condition is diagnosed.
**   - Actions end at the first newline at brace depth 0, skipping C
**     strings, character constants and comments the way flex does.
*/

#include "flex_reader.h"
#include "lex_ast.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
** Small helpers
** ============================================================ */

typedef struct {
    char *buf;
    size_t len, cap;
} SB;

static int sb_reserve(SB *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t nc = b->cap ? b->cap : 64;
    while (nc < b->len + extra + 1)
        nc *= 2;
    char *nb = realloc(b->buf, nc);
    if (!nb) return -1;
    b->buf = nb;
    b->cap = nc;
    return 0;
}
static void sb_putn(SB *b, const char *s, size_t n) {
    if (sb_reserve(b, n) < 0) return;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}
static void sb_puts(SB *b, const char *s) {
    sb_putn(b, s, strlen(s));
}
static void sb_putc(SB *b, char c) {
    sb_putn(b, &c, 1);
}
static char *sb_take(SB *b) {
    if (!b->buf) {
        char *e = malloc(1);
        if (e) e[0] = 0;
        return e;
    }
    char *r = b->buf;
    b->buf = NULL;
    b->len = b->cap = 0;
    return r;
}
static char *xstrndup(const char *s, size_t n) {
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}
static char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}

static int ci_eq(const char *a, size_t an, const char *b) {
    size_t bn = strlen(b);
    if (an != bn) return 0;
    for (size_t i = 0; i < an; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

static int is_blank(int c) {
    return c == ' ' || c == '\t';
}
static int is_name_start(int c) {
    return isalpha(c) || c == '_';
}
static int is_name_char(int c) {
    return isalnum(c) || c == '_' || c == '-';
}

/* ============================================================
** Reader state
** ============================================================ */

typedef struct Def {
    char *name;
    char *text;
    int line;
    struct Def *next;
} Def;

typedef struct {
    LimeFlexCode *v;
    int n, cap;
} CodeVec;

typedef struct {
    LimeFlexSpec *fs;
    const char *src;
    size_t len;
    size_t pos; /* start of the current line */
    int line;   /* line number of `pos` */
    Def *defs;
    int caseless;
    int posix;
    /* section 2 */
    unsigned char **scopes; /* scope stack: each an n_sc bitmap */
    int n_scopes, cap_scopes;
    char *pending_code; /* %{ %} block in the middle of section 2 */
    /* rules under construction */
    int cap_rules;
    char **rule_pat; /* Lime regex text per rule */
    int *rule_scoped; /* 1 if the rule had an explicit <SC> or scope */
} R;

static void rerr(R *r, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: ", r->fs->filename ? r->fs->filename : "<stdin>", line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    r->fs->error_count++;
}

static void rwarn(R *r, int line, const char *fmt, ...) {
    if (r->fs->opt.warn == 0) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: warning, ", r->fs->filename ? r->fs->filename : "<stdin>", line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* Current line as [s, s+n) without the newline or a trailing \r. */
static size_t line_end(const R *r, size_t pos) {
    const char *e = memchr(r->src + pos, '\n', r->len - pos);
    return e ? (size_t)(e - r->src) : r->len;
}
static void cur_line(const R *r, const char **s, size_t *n) {
    size_t e = line_end(r, r->pos);
    size_t m = e - r->pos;
    if (m > 0 && r->src[r->pos + m - 1] == '\r') m--;
    *s = r->src + r->pos;
    *n = m;
}
static void next_line(R *r) {
    size_t e = line_end(r, r->pos);
    r->pos = e < r->len ? e + 1 : r->len;
    r->line++;
}
static int at_eof(const R *r) {
    return r->pos >= r->len;
}
static int blank_line(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!is_blank((unsigned char)s[i])) return 0;
    return 1;
}
/* Rest of a line is blank or a C comment that ends on it. */
static int blank_or_comment(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && is_blank((unsigned char)s[i]))
        i++;
    if (i == n) return 1;
    if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
        for (size_t j = i + 2; j + 1 < n; j++)
            if (s[j] == '*' && s[j + 1] == '/') return blank_or_comment(s + j + 2, n - j - 2);
    }
    return 0;
}

static void code_add(CodeVec *cv, int line, char *text) {
    if (!text) return;
    if (cv->n == cv->cap) {
        int nc = cv->cap ? cv->cap * 2 : 8;
        LimeFlexCode *nv = realloc(cv->v, (size_t)nc * sizeof(*nv));
        if (!nv) {
            free(text);
            return;
        }
        cv->v = nv;
        cv->cap = nc;
    }
    cv->v[cv->n].line = line;
    cv->v[cv->n].text = text;
    cv->n++;
}

/* Copy whole lines from the current one up to and including the first
** line that starts with `%}` (which is not copied).  The `%{` line
** itself has already been consumed.  Returns the text, or NULL if the
** block is unterminated (diagnosed). */
static char *read_percent_block(R *r, int open_line) {
    SB b = { 0 };
    while (!at_eof(r)) {
        const char *s;
        size_t n;
        cur_line(r, &s, &n);
        if (n >= 2 && s[0] == '%' && s[1] == '}') {
            next_line(r);
            return sb_take(&b);
        }
        size_t e = line_end(r, r->pos);
        sb_putn(&b, r->src + r->pos, e - r->pos);
        sb_putc(&b, '\n');
        next_line(r);
    }
    rerr(r, open_line, "unterminated %%{ ... %%} block (premature EOF)");
    free(b.buf);
    return NULL;
}

/* ============================================================
** Start conditions
** ============================================================ */

static int sc_lookup(const LimeFlexSpec *fs, const char *s, size_t n) {
    for (int i = 0; i < fs->n_sc; i++)
        if (strlen(fs->sc_names[i]) == n && memcmp(fs->sc_names[i], s, n) == 0) return i;
    return -1;
}

static void sc_declare(R *r, const char *s, size_t n, int excl, int line) {
    LimeFlexSpec *fs = r->fs;
    if (sc_lookup(fs, s, n) >= 0) {
        rwarn(r, line, "start condition %.*s declared twice", (int)n, s);
        return;
    }
    char **nn = realloc(fs->sc_names, (size_t)(fs->n_sc + 1) * sizeof(*nn));
    if (!nn) return;
    fs->sc_names = nn;
    unsigned char *ne = realloc(fs->sc_excl, (size_t)(fs->n_sc + 1));
    if (!ne) return;
    fs->sc_excl = ne;
    fs->sc_names[fs->n_sc] = xstrndup(s, n);
    fs->sc_excl[fs->n_sc] = (unsigned char)excl;
    fs->n_sc++;
}

/* ============================================================
** %option
** ============================================================ */

typedef struct {
    const char *name;
    size_t off; /* offsetof into LimeFlexOptions, or (size_t)-1 */
    const char *reject_msg;
} OptDesc;

#define OPT(n, f) { n, offsetof(LimeFlexOptions, f), NULL }
#define OPT_IGN(n) { n, (size_t)-1, NULL }
#define OPT_NO(n, msg) { n, (size_t)-2, msg }

static const OptDesc option_table[] = {
    OPT("reentrant", reentrant),
    OPT("bison-bridge", bison_bridge),
    OPT("bison-locations", bison_locations),
    OPT("yylineno", yylineno),
    OPT("yywrap", yywrap),
    OPT("default", nodefault), /* inverted below */
    OPT("caseless", caseless),
    OPT("case-insensitive", caseless),
    OPT("caseful", caseless),       /* inverted below */
    OPT("case-sensitive", caseless), /* inverted below */
    OPT("stack", stack),
    OPT("main", main_fn),
    OPT("posix-compat", posix_compat),
    OPT("debug", debug),
    OPT("never-interactive", never_interactive),
    OPT("always-interactive", always_interactive),
    OPT("interactive", always_interactive),
    OPT("batch", never_interactive),
    OPT("line", noline), /* inverted below */
    OPT("unput", unput),
    OPT("input", input),
    OPT("yy_push_state", yy_push_state),
    OPT("yy_pop_state", yy_pop_state),
    OPT("yy_top_state", yy_top_state),
    OPT("yyalloc", yyalloc),
    OPT("yyrealloc", yyrealloc),
    OPT("yyfree", yyfree),
    OPT("unistd", unistd),
    OPT("stdinit", stdinit),
    OPT("reject", reject),
    OPT("yymore", yymore),
    OPT("warn", warn),
    OPT("8bit", eightbit),
    OPT("7bit", eightbit), /* inverted below; 8-bit tables either way */
    OPT("backup", backup),
    OPT_IGN("pointer"),
    OPT_IGN("ecs"),
    OPT_IGN("meta-ecs"),
    OPT_IGN("align"),
    OPT_IGN("full"),
    OPT_IGN("fast"),
    OPT_IGN("read"),
    OPT_IGN("perf-report"),
    OPT_IGN("verbose"),
    OPT_IGN("stdout"),
    OPT_IGN("yy_scan_buffer"),
    OPT_IGN("yy_scan_bytes"),
    OPT_IGN("yy_scan_string"),
    OPT_IGN("yyget_debug"),
    OPT_IGN("yyset_debug"),
    OPT_IGN("yyget_extra"),
    OPT_IGN("yyset_extra"),
    OPT_IGN("yyget_leng"),
    OPT_IGN("yyget_text"),
    OPT_IGN("yyget_lineno"),
    OPT_IGN("yyset_lineno"),
    OPT_IGN("yyget_column"),
    OPT_IGN("yyset_column"),
    OPT_IGN("yyget_in"),
    OPT_IGN("yyset_in"),
    OPT_IGN("yyget_out"),
    OPT_IGN("yyset_out"),
    OPT_IGN("yyget_lval"),
    OPT_IGN("yyset_lval"),
    OPT_IGN("yyget_lloc"),
    OPT_IGN("yyset_lloc"),
    OPT_NO("array", "%option array (yytext as a char array) is not supported by lime; "
                    "yytext is always a char * (%pointer)"),
    OPT_NO("c++", "%option c++ (a C++ scanner class) is not supported by lime; "
                  "lime generates C scanners only"),
    OPT_NO("lex-compat", "%option lex-compat (AT&T lex compatibility mode) is not supported by lime"),
    OPT_NO("tables-verify", "%option tables-verify (serialized tables) is not supported by lime"),
    { NULL, 0, NULL },
};

static void set_bool_opt(LimeFlexOptions *o, const char *name, size_t off, int sense) {
    int *p = (int *)((char *)o + off);
    /* Options whose field stores the opposite sense. */
    if (!strcmp(name, "default") || !strcmp(name, "caseful") || !strcmp(name, "case-sensitive") ||
        !strcmp(name, "line") || !strcmp(name, "7bit"))
        sense = !sense;
    *p = sense;
}

static void parse_options(R *r, const char *s, size_t n, int line) {
    LimeFlexOptions *o = &r->fs->opt;
    size_t i = 0;
    while (i < n) {
        while (i < n && is_blank((unsigned char)s[i]))
            i++;
        if (i >= n) break;
        size_t ws = i;
        while (i < n && !is_blank((unsigned char)s[i]) && s[i] != '=')
            i++;
        const char *w = s + ws;
        size_t wn = i - ws;
        /* name=value */
        size_t j = i;
        while (j < n && is_blank((unsigned char)s[j]))
            j++;
        if (j < n && s[j] == '=') {
            j++;
            while (j < n && is_blank((unsigned char)s[j]))
                j++;
            const char *v;
            size_t vn;
            if (j < n && s[j] == '"') {
                size_t k = j + 1;
                while (k < n && s[k] != '"')
                    k++;
                if (k >= n) {
                    rerr(r, line, "unterminated quoted value in %%option %.*s", (int)wn, w);
                    return;
                }
                v = s + j + 1;
                vn = k - j - 1;
                i = k + 1;
            } else {
                size_t k = j;
                while (k < n && !is_blank((unsigned char)s[k]))
                    k++;
                v = s + j;
                vn = k - j;
                i = k;
            }
            char **dst = NULL;
            if (ci_eq(w, wn, "prefix"))
                dst = &o->prefix;
            else if (ci_eq(w, wn, "outfile"))
                dst = &o->outfile;
            else if (ci_eq(w, wn, "header-file") || ci_eq(w, wn, "header"))
                dst = &o->header_file;
            else if (ci_eq(w, wn, "extra-type"))
                dst = &o->extra_type;
            else if (ci_eq(w, wn, "yyclass")) {
                rerr(r, line, "%%option yyclass (C++ scanner class) is not supported by lime");
                continue;
            } else if (ci_eq(w, wn, "tables-file")) {
                rerr(r, line, "%%option tables-file (serialized tables) is not supported by lime");
                continue;
            } else {
                rerr(r, line, "unrecognized %%option: %.*s", (int)wn, w);
                continue;
            }
            if (dst == &o->prefix && (memchr(v, '[', vn) || memchr(v, ']', vn))) {
                rerr(r, line, "Prefix must not contain [ or ]");
                continue;
            }
            free(*dst);
            *dst = xstrndup(v, vn);
            continue;
        }
        /* Boolean option, with an optional "no" prefix. */
        int sense = 1;
        const OptDesc *d = NULL;
        for (int pass = 0; pass < 2 && !d; pass++) {
            const char *nm = w;
            size_t nl = wn;
            if (pass == 1) {
                if (wn < 3 || tolower((unsigned char)w[0]) != 'n' || tolower((unsigned char)w[1]) != 'o')
                    break;
                nm = w + 2;
                nl = wn - 2;
                sense = 0;
            }
            for (const OptDesc *t = option_table; t->name; t++)
                if (ci_eq(nm, nl, t->name)) {
                    d = t;
                    break;
                }
        }
        if (!d) {
            rerr(r, line, "unrecognized %%option: %.*s", (int)wn, w);
            continue;
        }
        if (d->off == (size_t)-2) {
            if (sense) rerr(r, line, "%s", d->reject_msg);
            continue;
        }
        if (d->off == (size_t)-1) continue;
        set_bool_opt(o, d->name, d->off, sense);
    }
}

/* ============================================================
** Section 1
** ============================================================ */

static void def_add(R *r, const char *name, size_t nn, const char *text, size_t tn, int line) {
    for (Def *d = r->defs; d; d = d->next)
        if (strlen(d->name) == nn && memcmp(d->name, name, nn) == 0) {
            /* flex: the later definition replaces the earlier one. */
            free(d->text);
            d->text = xstrndup(text, tn);
            return;
        }
    Def *d = calloc(1, sizeof(*d));
    if (!d) return;
    d->name = xstrndup(name, nn);
    d->text = xstrndup(text, tn);
    d->line = line;
    d->next = r->defs;
    r->defs = d;
}

static const Def *def_find(const R *r, const char *name, size_t nn) {
    for (const Def *d = r->defs; d; d = d->next)
        if (strlen(d->name) == nn && memcmp(d->name, name, nn) == 0) return d;
    return NULL;
}

/* Returns 0 at the `%%` that ends section 1, -1 at EOF. */
static int read_section1(R *r, CodeVec *top, CodeVec *defs) {
    while (!at_eof(r)) {
        const char *s;
        size_t n;
        cur_line(r, &s, &n);
        int line = r->line;
        if (n >= 2 && s[0] == '%' && s[1] == '%') {
            next_line(r);
            return 0;
        }
        if (blank_line(s, n)) {
            next_line(r);
            continue;
        }
        if (is_blank((unsigned char)s[0])) {
            /* Indented line: copied to the output verbatim. */
            code_add(defs, line, xstrndup(s, n));
            next_line(r);
            continue;
        }
        if (n >= 2 && s[0] == '/' && s[1] == '*') {
            /* Comment at column 0: copied to the output. */
            SB b = { 0 };
            int start = line;
            for (;;) {
                cur_line(r, &s, &n);
                sb_putn(&b, s, n);
                sb_putc(&b, '\n');
                int closed = 0;
                for (size_t j = (r->line == start ? 2 : 0); j + 1 < n; j++)
                    if (s[j] == '*' && s[j + 1] == '/') closed = 1;
                next_line(r);
                if (closed || at_eof(r)) break;
            }
            code_add(defs, start, sb_take(&b));
            continue;
        }
        if (s[0] == '%') {
            size_t k = 1;
            if (n >= 2 && s[1] == '{') {
                next_line(r);
                char *t = read_percent_block(r, line);
                code_add(defs, line + 1, t);
                continue;
            }
            while (k < n && (isalnum((unsigned char)s[k]) || s[k] == '_' || s[k] == '-'))
                k++;
            const char *w = s + 1;
            size_t wn = k - 1;
            if (ci_eq(w, wn, "top")) {
                /* %top{ ... } with naive brace matching, like flex. */
                size_t j = k;
                while (j < n && is_blank((unsigned char)s[j]))
                    j++;
                if (j >= n || s[j] != '{' || !blank_line(s + j + 1, n - j - 1)) {
                    rerr(r, line, "malformed '%%top' directive");
                    next_line(r);
                    continue;
                }
                next_line(r);
                SB b = { 0 };
                int depth = 1;
                int body = r->line;
                size_t p = r->pos;
                while (p < r->len && depth > 0) {
                    char c = r->src[p];
                    if (c == '{') depth++;
                    if (c == '}' && --depth == 0) break;
                    if (c == '\n') r->line++;
                    sb_putc(&b, c);
                    p++;
                }
                if (depth > 0) {
                    rerr(r, line, "Unmatched '{'");
                    free(b.buf);
                    r->pos = r->len;
                    return -1;
                }
                /* Skip the closing brace; the rest of its line is ignored. */
                r->pos = p + 1;
                {
                    size_t e = line_end(r, r->pos);
                    r->pos = e < r->len ? e + 1 : r->len;
                    r->line++;
                }
                code_add(top, body, sb_take(&b));
                continue;
            }
            if (ci_eq(w, wn, "option")) {
                parse_options(r, s + k, n - k, line);
                next_line(r);
                continue;
            }
            if (ci_eq(w, wn, "pointer")) {
                next_line(r);
                continue;
            }
            if (ci_eq(w, wn, "array")) {
                rerr(r, line, "%%array (yytext as a char array) is not supported by lime; "
                              "yytext is always a char * (%%pointer)");
                next_line(r);
                continue;
            }
            int c1 = wn > 0 ? tolower((unsigned char)w[0]) : 0;
            if (c1 == 's' || c1 == 'x') {
                /* %s / %x (or %start, %state, %x...): names follow. */
                size_t j = k;
                while (j < n) {
                    while (j < n && is_blank((unsigned char)s[j]))
                        j++;
                    if (j >= n) break;
                    size_t ns = j;
                    if (!is_name_start((unsigned char)s[j])) {
                        rerr(r, line, "bad start condition name in %%%c declaration", s[1]);
                        break;
                    }
                    while (j < n && is_name_char((unsigned char)s[j]))
                        j++;
                    sc_declare(r, s + ns, j - ns, c1 == 'x', line);
                }
                next_line(r);
                continue;
            }
            if (wn == 1 && strchr("aceknopr", c1)) {
                /* AT&T lex table-size directives (%e 1019, %p 2807 ...): ignored. */
                next_line(r);
                continue;
            }
            rerr(r, line, "unrecognized '%%' directive");
            next_line(r);
            continue;
        }
        if (is_name_start((unsigned char)s[0])) {
            size_t k = 0;
            while (k < n && is_name_char((unsigned char)s[k]))
                k++;
            size_t j = k;
            while (j < n && is_blank((unsigned char)s[j]))
                j++;
            size_t e = n;
            while (e > j && is_blank((unsigned char)s[e - 1]))
                e--;
            if (j >= e)
                rerr(r, line, "incomplete name definition");
            else
                def_add(r, s, k, s + j, e - j, line);
            next_line(r);
            continue;
        }
        if (s[0] == '#') {
            /* #line directives in section 1 are accepted and ignored. */
            size_t j = 1;
            while (j < n && is_blank((unsigned char)s[j]))
                j++;
            if (n - j >= 4 && memcmp(s + j, "line", 4) == 0) {
                next_line(r);
                continue;
            }
        }
        rerr(r, line, "unrecognized line in the definitions section");
        next_line(r);
    }
    rerr(r, r->line, "premature EOF: no %%%% line ends the definitions section");
    return -1;
}

/* ============================================================
** Pattern translation
** ============================================================
**
** The pattern is parsed from a private copy of the rest of the line
** that the parser may splice {name} expansions into.  Output is Lime
** regex text: every atom is a byte (alnum as itself, anything else as
** \xHH), a character class over byte values, or a parenthesised group,
** so the Lime regex parser never sees a flex-specific construct.
*/

typedef struct {
    unsigned char bits[32];
} Set;

static void set_add(Set *s, int c) {
    s->bits[(c & 255) >> 3] |= (unsigned char)(1u << (c & 7));
}
static int set_has(const Set *s, int c) {
    return (s->bits[(c & 255) >> 3] >> (c & 7)) & 1;
}
static void set_negate(Set *s) {
    for (int i = 0; i < 32; i++)
        s->bits[i] = (unsigned char)~s->bits[i];
}
static int has_case(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int reverse_case(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static void emit_byte(SB *b, int c) {
    if (isalnum(c) && c < 128) {
        sb_putc(b, (char)c);
    } else {
        char t[8];
        snprintf(t, sizeof t, "\\x%02x", c & 255);
        sb_puts(b, t);
    }
}

/* Emit a set as one atom. */
static void emit_set(SB *b, const Set *s) {
    int count = 0, only = -1;
    for (int c = 0; c < 256; c++)
        if (set_has(s, c)) {
            count++;
            only = c;
        }
    if (count == 1) {
        emit_byte(b, only);
        return;
    }
    if (count == 0) {
        sb_puts(b, "[^\\x00-\\xff]"); /* matches nothing */
        return;
    }
    sb_putc(b, '[');
    for (int c = 0; c < 256;) {
        if (!set_has(s, c)) {
            c++;
            continue;
        }
        int e = c;
        while (e + 1 < 256 && set_has(s, e + 1))
            e++;
        emit_byte(b, c);
        if (e > c) {
            sb_putc(b, '-');
            emit_byte(b, e);
        }
        c = e + 1;
    }
    sb_putc(b, ']');
}

typedef struct {
    R *r;
    int line;
    char *s; /* mutable pattern buffer (NUL-terminated) */
    size_t n, i;
    size_t exp_end; /* s[0..exp_end) came from {name} expansions */
    size_t growth;  /* bytes added by expansions so far */
    int n_expansions;
    int ci, dotall; /* scoped flags */
    int posix;
    int depth;      /* parenthesis depth */
    int err;
    int has_nl;     /* pattern can match '\n' */
    int at_start;   /* nothing consumed yet (for ^ handling) */
    int trail;      /* saw a trailing $ */
} PP;

static void pp_err(PP *p, const char *fmt, ...) {
    if (p->err) return;
    p->err = 1;
    va_list ap;
    va_start(ap, fmt);
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    rerr(p->r, p->line, "%s", msg);
}

static int pp_peek(const PP *p, size_t k) {
    return p->i + k < p->n ? (unsigned char)p->s[p->i + k] : -1;
}

/* Whitespace ends the pattern.  Inside an expansion it would split the
** definition between pattern and action, which flex mis-handles; we
** diagnose it instead. */
static int pp_at_end(PP *p) {
    int c = pp_peek(p, 0);
    if (c < 0) return 1;
    if (is_blank(c)) {
        if (p->i < p->exp_end) {
            pp_err(p, "a {definition} used in this rule contains unquoted whitespace "
                      "(flex would end the pattern there)");
            return 1;
        }
        return 1;
    }
    return 0;
}

/* Replace s[i, i+m) with `text`. */
static int pp_splice(PP *p, size_t m, const char *text) {
    size_t k = strlen(text);
    char *ns = malloc(p->n - m + k + 1);
    if (!ns) return -1;
    memcpy(ns, p->s, p->i);
    memcpy(ns + p->i, text, k);
    memcpy(ns + p->i + k, p->s + p->i + m, p->n - p->i - m + 1);
    free(p->s);
    p->s = ns;
    if (p->i < p->exp_end)
        p->exp_end = p->exp_end - m + k;
    else
        p->exp_end = p->i + k;
    p->n = p->n - m + k;
    p->growth += k - m; /* may wrap; only the difference is used */
    return 0;
}

/* Try to expand `{name}` at the cursor.  Returns 1 if expanded, 0 if the
** brace is not a definition reference. */
static int pp_try_expand(PP *p) {
    size_t j = p->i + 1;
    if (j >= p->n || !is_name_start((unsigned char)p->s[j])) return 0;
    size_t ns = j;
    while (j < p->n && is_name_char((unsigned char)p->s[j]))
        j++;
    if (j >= p->n || p->s[j] != '}') return 0;
    const Def *d = def_find(p->r, p->s + ns, j - ns);
    if (!d) {
        pp_err(p, "undefined definition {%.*s}", (int)(j - ns), p->s + ns);
        return 1;
    }
    if (++p->n_expansions > 10000) {
        pp_err(p, "definition {%s} expands recursively", d->name);
        return 1;
    }
    size_t tl = strlen(d->text);
    SB b = { 0 };
    if (d->text[0] == '^' || (tl > 0 && d->text[tl - 1] == '$')) {
        sb_puts(&b, d->text); /* flex: no parentheses for ^ / $ definitions */
    } else {
        sb_putc(&b, '(');
        sb_puts(&b, d->text);
        sb_putc(&b, ')');
    }
    char *t = sb_take(&b);
    if (t) pp_splice(p, j + 1 - p->i, t);
    free(t);
    return 1;
}

/* Read one escape sequence at the cursor (which is on the backslash),
** with flex's myesc() semantics. */
static int pp_escape(PP *p) {
    p->i++; /* backslash */
    int c = pp_peek(p, 0);
    if (c < 0) {
        pp_err(p, "trailing backslash in pattern");
        return 0;
    }
    p->i++;
    switch (c) {
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'a':
        return '\a';
    case 'v':
        return '\v';
    case 'x': {
        int v = 0, k = 0;
        while (k < 2 && isxdigit(pp_peek(p, 0))) {
            int d = pp_peek(p, 0);
            v = v * 16 + (isdigit(d) ? d - '0' : tolower(d) - 'a' + 10);
            p->i++;
            k++;
        }
        return v & 255;
    }
    default:
        if (c >= '0' && c <= '7') {
            int v = c - '0', k = 1;
            while (k < 3 && pp_peek(p, 0) >= '0' && pp_peek(p, 0) <= '7') {
                v = v * 8 + (pp_peek(p, 0) - '0');
                p->i++;
                k++;
            }
            return v & 255;
        }
        return c;
    }
}

static void add_char_ci(PP *p, Set *s, int c) {
    set_add(s, c);
    if (p->ci && has_case(c)) set_add(s, reverse_case(c));
}

static void posix_class(Set *s, const char *nm, size_t nl, int neg, int ci) {
    Set t;
    memset(&t, 0, sizeof t);
    for (int c = 0; c < 128; c++) {
        int in = 0;
        if (ci_eq(nm, nl, "alnum"))
            in = isalnum(c);
        else if (ci_eq(nm, nl, "alpha"))
            in = isalpha(c);
        else if (ci_eq(nm, nl, "blank"))
            in = (c == ' ' || c == '\t');
        else if (ci_eq(nm, nl, "cntrl"))
            in = iscntrl(c);
        else if (ci_eq(nm, nl, "digit"))
            in = isdigit(c);
        else if (ci_eq(nm, nl, "graph"))
            in = isgraph(c);
        else if (ci_eq(nm, nl, "lower"))
            in = islower(c) || (ci && isupper(c));
        else if (ci_eq(nm, nl, "print"))
            in = isprint(c);
        else if (ci_eq(nm, nl, "punct"))
            in = ispunct(c);
        else if (ci_eq(nm, nl, "space"))
            in = isspace(c);
        else if (ci_eq(nm, nl, "upper"))
            in = isupper(c) || (ci && islower(c));
        else if (ci_eq(nm, nl, "xdigit"))
            in = isxdigit(c);
        if (in) set_add(&t, c);
    }
    if (neg) set_negate(&t);
    for (int i = 0; i < 32; i++)
        s->bits[i] |= t.bits[i];
}

static int posix_class_known(const char *nm, size_t nl) {
    static const char *const k[] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph",
                                     "lower", "print", "punct", "space", "upper", "xdigit", NULL };
    for (int i = 0; k[i]; i++)
        if (ci_eq(nm, nl, k[i])) return 1;
    return 0;
}

/* Parse `[...]` at the cursor into *out. */
static int pp_ccl(PP *p, Set *out) {
    memset(out, 0, sizeof *out);
    p->i++; /* [ */
    int neg = 0;
    if (pp_peek(p, 0) == '^') {
        neg = 1;
        p->i++;
    }
    int first = 1;
    for (;;) {
        int c = pp_peek(p, 0);
        if (c < 0) {
            pp_err(p, "bad character class (missing ])");
            return -1;
        }
        if (c == ']' && !first) {
            p->i++;
            break;
        }
        if (c == '[' && pp_peek(p, 1) == ':') {
            size_t j = p->i + 2;
            int pneg = 0;
            if (j < p->n && p->s[j] == '^') {
                pneg = 1;
                j++;
            }
            size_t ns = j;
            while (j < p->n && isalpha((unsigned char)p->s[j]))
                j++;
            if (j + 1 < p->n && p->s[j] == ':' && p->s[j + 1] == ']') {
                if (!posix_class_known(p->s + ns, j - ns)) {
                    pp_err(p, "bad character class expression: [:%.*s:]", (int)(j - ns), p->s + ns);
                    return -1;
                }
                if (p->ci && pneg &&
                    (ci_eq(p->s + ns, j - ns, "lower") || ci_eq(p->s + ns, j - ns, "upper"))) {
                    rwarn(p->r, p->line, "[:^%.*s:] is ambiguous in case insensitive scanner",
                          (int)(j - ns), p->s + ns);
                } else {
                    posix_class(out, p->s + ns, j - ns, pneg, p->ci);
                }
                p->i = j + 2;
                first = 0;
                continue;
            }
        }
        first = 0;
        int lo;
        if (c == '\\')
            lo = pp_escape(p);
        else {
            lo = c;
            p->i++;
        }
        if (p->err) return -1;
        if (pp_peek(p, 0) == '-' && pp_peek(p, 1) >= 0 && pp_peek(p, 1) != ']') {
            p->i++;
            int hi;
            if (pp_peek(p, 0) == '\\')
                hi = pp_escape(p);
            else {
                hi = pp_peek(p, 0);
                p->i++;
            }
            if (lo > hi) {
                pp_err(p, "negative range in character class");
                return -1;
            }
            for (int k = lo; k <= hi; k++)
                set_add(out, k);
            if (p->ci && has_case(lo) && has_case(hi)) {
                int rl = reverse_case(lo), rh = reverse_case(hi);
                for (int k = rl; k <= rh; k++)
                    set_add(out, k);
            }
        } else {
            add_char_ci(p, out, lo);
        }
    }
    if (neg) set_negate(out);
    return 0;
}

static char *pp_re(PP *p);

/* A full character class, including the {-} and {+} set operators. */
static int pp_fullccl(PP *p, Set *out) {
    if (pp_ccl(p, out) < 0) return -1;
    for (;;) {
        int op = 0;
        if (pp_peek(p, 0) == '{' && (pp_peek(p, 1) == '-' || pp_peek(p, 1) == '+') &&
            pp_peek(p, 2) == '}' && pp_peek(p, 3) == '[')
            op = pp_peek(p, 1);
        if (!op) return 0;
        p->i += 3;
        Set rhs;
        if (pp_ccl(p, &rhs) < 0) return -1;
        for (int k = 0; k < 32; k++)
            out->bits[k] = op == '-' ? (unsigned char)(out->bits[k] & ~rhs.bits[k])
                                     : (unsigned char)(out->bits[k] | rhs.bits[k]);
    }
}

static void note_nl(PP *p, const Set *s) {
    if (set_has(s, '\n')) p->has_nl = 1;
}

/* One singleton without its quantifiers.  Returns heap text, or NULL at
** the end of the series (no singleton here) or on error. */
static char *pp_singleton(PP *p) {
    for (;;) {
        if (pp_at_end(p) || p->err) return NULL;
        int c = pp_peek(p, 0);
        if (c == '{') {
            if (isdigit(pp_peek(p, 1))) return NULL; /* repetition: caller */
            if (pp_try_expand(p)) {
                if (p->err) return NULL;
                continue; /* re-scan the spliced text */
            }
            pp_err(p, "unrecognized rule (stray '{' in a pattern)");
            return NULL;
        }
        break;
    }
    int c = pp_peek(p, 0);
    SB b = { 0 };
    Set s;
    memset(&s, 0, sizeof s);
    switch (c) {
    case '|':
    case ')':
    case '*':
    case '+':
    case '?':
        return NULL;
    case '}':
        pp_err(p, "unrecognized rule (stray '}' in a pattern)");
        return NULL;
    case '/':
        pp_err(p, "trailing context (the r/s operator) is not supported by lime");
        return NULL;
    case '$': {
        /* Only an EOL operator when it ends the rule. */
        int nx = pp_peek(p, 1);
        if (p->depth == 0 && (nx < 0 || is_blank(nx))) return NULL;
        p->i++;
        emit_byte(&b, '$');
        p->at_start = 0;
        return sb_take(&b);
    }
    case '.':
        p->i++;
        memset(&s, 0xff, sizeof s);
        if (!p->dotall) s.bits['\n' >> 3] &= (unsigned char)~(1u << ('\n' & 7));
        note_nl(p, &s);
        emit_set(&b, &s);
        p->at_start = 0;
        return sb_take(&b);
    case '[':
        if (pp_fullccl(p, &s) < 0) return NULL;
        note_nl(p, &s);
        emit_set(&b, &s);
        p->at_start = 0;
        return sb_take(&b);
    case '"': {
        p->i++;
        SB inner = { 0 };
        int count = 0;
        for (;;) {
            int d = pp_peek(p, 0);
            if (d < 0) {
                pp_err(p, "missing quote");
                free(inner.buf);
                return NULL;
            }
            if (d == '"') {
                p->i++;
                break;
            }
            int ch;
            if (d == '\\')
                ch = pp_escape(p);
            else {
                ch = d;
                p->i++;
            }
            Set one;
            memset(&one, 0, sizeof one);
            add_char_ci(p, &one, ch);
            note_nl(p, &one);
            emit_set(&inner, &one);
            count++;
        }
        if (count == 1) {
            p->at_start = 0;
            return sb_take(&inner);
        }
        sb_putc(&b, '(');
        if (inner.buf) sb_puts(&b, inner.buf);
        sb_putc(&b, ')');
        free(inner.buf);
        p->at_start = 0;
        return sb_take(&b);
    }
    case '(': {
        p->i++;
        int save_ci = p->ci, save_dot = p->dotall;
        if (pp_peek(p, 0) == '?') {
            if (p->posix) {
                pp_err(p, "(?...) groups are not allowed in posix-compat mode");
                return NULL;
            }
            p->i++;
            if (pp_peek(p, 0) == '#') {
                /* (?# comment ) */
                while (pp_peek(p, 0) >= 0 && pp_peek(p, 0) != ')')
                    p->i++;
                if (pp_peek(p, 0) != ')') {
                    pp_err(p, "unterminated (?# comment");
                    return NULL;
                }
                p->i++;
                return xstrdup("()");
            }
            int on = 1;
            for (;;) {
                int f = pp_peek(p, 0);
                if (f == ':') {
                    p->i++;
                    break;
                }
                if (f == '-')
                    on = 0;
                else if (f == 'i')
                    p->ci = on;
                else if (f == 's')
                    p->dotall = on;
                else if (f == 'x') {
                    pp_err(p, "the (?x: ...) skip-whitespace group is not supported by lime");
                    return NULL;
                } else {
                    pp_err(p, "bad (?...) group flag");
                    return NULL;
                }
                p->i++;
            }
        }
        p->depth++;
        p->at_start = 0;
        char *inner = pp_re(p);
        p->depth--;
        p->ci = save_ci;
        p->dotall = save_dot;
        if (!inner) return NULL;
        if (pp_peek(p, 0) != ')') {
            free(inner);
            pp_err(p, "unbalanced parenthesis");
            return NULL;
        }
        p->i++;
        sb_putc(&b, '(');
        sb_puts(&b, inner);
        sb_putc(&b, ')');
        free(inner);
        return sb_take(&b);
    }
    case '\\': {
        int ch = pp_escape(p);
        if (p->err) return NULL;
        Set one;
        memset(&one, 0, sizeof one);
        add_char_ci(p, &one, ch);
        note_nl(p, &one);
        emit_set(&b, &one);
        p->at_start = 0;
        return sb_take(&b);
    }
    default: {
        p->i++;
        Set one;
        memset(&one, 0, sizeof one);
        add_char_ci(p, &one, c);
        note_nl(p, &one);
        emit_set(&b, &one);
        p->at_start = 0;
        return sb_take(&b);
    }
    }
}

/* Parse {n}, {n,}, {n,m} at the cursor.  Returns 0 and the Lime
** quantifier text on success. */
static int pp_repeat(PP *p, char *q, size_t qn) {
    p->i++; /* { */
    long lo = 0, hi = -2;
    int got = 0;
    while (isdigit(pp_peek(p, 0))) {
        lo = lo * 10 + (pp_peek(p, 0) - '0');
        if (lo > 100000) lo = 100000;
        p->i++;
        got = 1;
    }
    if (!got) {
        pp_err(p, "bad character inside {}'s");
        return -1;
    }
    if (pp_peek(p, 0) == ',') {
        p->i++;
        if (isdigit(pp_peek(p, 0))) {
            hi = 0;
            while (isdigit(pp_peek(p, 0))) {
                hi = hi * 10 + (pp_peek(p, 0) - '0');
                if (hi > 100000) hi = 100000;
                p->i++;
            }
        } else {
            hi = -1;
        }
    }
    if (pp_peek(p, 0) != '}') {
        pp_err(p, pp_peek(p, 0) < 0 ? "missing }" : "bad character inside {}'s");
        return -1;
    }
    p->i++;
    if (hi == -2) { /* {n} */
        if (lo <= 0) {
            pp_err(p, "iteration value must be positive");
            return -1;
        }
        snprintf(q, qn, "{%ld}", lo);
    } else if (hi == -1) { /* {n,} */
        if (lo <= 0) {
            pp_err(p, "iteration value must be positive");
            return -1;
        }
        snprintf(q, qn, "{%ld,}", lo);
    } else {
        if (lo > hi || (lo == 0 && hi <= 0)) {
            pp_err(p, "bad iteration values");
            return -1;
        }
        snprintf(q, qn, "{%ld,%ld}", lo, hi);
    }
    return 0;
}

static char *wrap_q(char *atom, int quantified, const char *q) {
    SB b = { 0 };
    if (quantified) sb_putc(&b, '(');
    sb_puts(&b, atom);
    if (quantified) sb_putc(&b, ')');
    sb_puts(&b, q);
    free(atom);
    return sb_take(&b);
}

static char *pp_series(PP *p) {
    SB acc = { 0 };
    int any = 0;
    int series_quantified = 0;
    for (;;) {
        if (p->err) break;
        if (!pp_at_end(p) && pp_peek(p, 0) == '{' && isdigit(pp_peek(p, 1)) && p->posix) {
            /* posix-compat: {n,m} applies to the series so far. */
            if (!any) {
                pp_err(p, "repetition with nothing to repeat");
                break;
            }
            char q[48];
            if (pp_repeat(p, q, sizeof q) < 0) break;
            char *t = wrap_q(sb_take(&acc), 1, q);
            sb_puts(&acc, t);
            free(t);
            (void)series_quantified;
            continue;
        }
        char *atom = pp_singleton(p);
        if (!atom) break;
        int quantified = 0;
        for (;;) {
            if (pp_at_end(p)) break;
            int c = pp_peek(p, 0);
            if (c == '*' || c == '+' || c == '?') {
                char q[2] = { (char)c, 0 };
                p->i++;
                atom = wrap_q(atom, quantified, q);
                quantified = 1;
                continue;
            }
            if (c == '{' && isdigit(pp_peek(p, 1)) && !p->posix) {
                char q[48];
                if (pp_repeat(p, q, sizeof q) < 0) break;
                atom = wrap_q(atom, quantified, q);
                quantified = 1;
                continue;
            }
            break;
        }
        sb_puts(&acc, atom);
        free(atom);
        any = 1;
    }
    if (p->err) {
        free(acc.buf);
        return NULL;
    }
    if (!any) {
        free(acc.buf);
        return NULL;
    }
    return sb_take(&acc);
}

static char *pp_re(PP *p) {
    SB acc = { 0 };
    int n_alt = 0;
    for (;;) {
        char *s = pp_series(p);
        if (p->err) {
            free(s);
            free(acc.buf);
            return NULL;
        }
        if (!s) {
            pp_err(p, "unrecognized rule (empty regular expression)");
            free(acc.buf);
            return NULL;
        }
        if (n_alt++) sb_putc(&acc, '|');
        sb_puts(&acc, s);
        free(s);
        if (!pp_at_end(p) && pp_peek(p, 0) == '|') {
            p->i++;
            continue;
        }
        break;
    }
    return sb_take(&acc);
}

/* Translate the pattern at the start of `text` (NUL-terminated rest of
** the rule line).  On success returns the Lime regex and sets *consumed
** to the number of bytes of `text` used, *bol, *trail, *has_nl. */
static char *translate_pattern(R *r, int line, const char *text, size_t *consumed, int *bol,
                               int *trail, int *has_nl) {
    PP p;
    memset(&p, 0, sizeof p);
    p.r = r;
    p.line = line;
    p.s = xstrdup(text);
    if (!p.s) return NULL;
    p.n = strlen(p.s);
    p.ci = r->caseless;
    p.posix = r->posix;
    p.at_start = 1;
    *bol = 0;
    *trail = 0;
    /* A leading {name} whose definition starts with ^ makes that ^ the
    ** BOL operator, as in flex (it splices such a definition unwrapped). */
    if (pp_peek(&p, 0) == '{' && is_name_start(pp_peek(&p, 1))) {
        size_t j = 1;
        while (j < p.n && is_name_char((unsigned char)p.s[j]))
            j++;
        const Def *d = (j < p.n && p.s[j] == '}') ? def_find(r, p.s + 1, j - 1) : NULL;
        if (d && d->text[0] == '^') pp_try_expand(&p);
    }
    if (pp_peek(&p, 0) == '^') {
        *bol = 1;
        p.i++;
    }
    char *re = pp_re(&p);
    if (re && !p.err) {
        if (pp_peek(&p, 0) == '$' && (pp_peek(&p, 1) < 0 || is_blank(pp_peek(&p, 1)))) {
            p.i++;
            *trail = 1;
        } else if (pp_peek(&p, 0) == ')') {
            pp_err(&p, "unbalanced parenthesis");
        }
    }
    if (p.err) {
        free(re);
        free(p.s);
        return NULL;
    }
    if (p.i < p.exp_end) {
        free(re);
        free(p.s);
        pp_err(&p, "a {definition} used in this rule ends the pattern early");
        return NULL;
    }
    /* Map the cursor back to the original text: everything after
    ** exp_end is unmodified original text. */
    *consumed = strlen(text) - (p.n - p.i);
    *has_nl = p.has_nl;
    free(p.s);
    if (*trail) {
        SB b = { 0 };
        sb_putc(&b, '(');
        sb_puts(&b, re);
        sb_puts(&b, ")\\x0a");
        free(re);
        return sb_take(&b);
    }
    return re;
}

/* ============================================================
** Actions
** ============================================================ */

static int ident_is(const char *s, size_t n, const char *w) {
    return strlen(w) == n && memcmp(s, w, n) == 0;
}

/* Read an action starting at absolute offset `start` on line `line`.
** On return *end is the offset just past the newline that ends the
** action, *end_line the line number after it.  Sets *continued for a
** `|` action.  Returns the action text (NULL for an empty action). */
static char *read_action(R *r, size_t start, int line, size_t *end, int *end_line,
                         int *continued, int *uses_reject, int *uses_yymore) {
    const char *src = r->src;
    size_t i = start, n = r->len;
    *continued = 0;
    while (i < n && is_blank((unsigned char)src[i]))
        i++;
    if (i >= n || src[i] == '\n' || (src[i] == '\r' && i + 1 < n && src[i + 1] == '\n')) {
        size_t e = line_end(r, i);
        *end = e < n ? e + 1 : n;
        *end_line = line + 1;
        return NULL;
    }
    if (src[i] == '|') {
        size_t e = line_end(r, i);
        *end = e < n ? e + 1 : n;
        *end_line = line + 1;
        *continued = 1;
        return NULL;
    }
    size_t a = i;
    int cur = line;
    if (src[i] == '%' && i + 1 < n && src[i + 1] == '{') {
        /* %{ ... %} action */
        i += 2;
        size_t body = i;
        for (; i + 1 < n; i++) {
            if (src[i] == '\n') cur++;
            if (src[i] == '%' && src[i + 1] == '}') break;
        }
        if (i + 1 >= n) {
            rerr(r, line, "EOF encountered inside an action");
            *end = n;
            *end_line = cur;
            return NULL;
        }
        char *t = xstrndup(src + body, i - body);
        size_t e = line_end(r, i);
        *end = e < n ? e + 1 : n;
        *end_line = cur + 1;
        return t;
    }
    int brace = 0;
    while (i < n) {
        char c = src[i];
        if (c == '\n') {
            cur++;
            if (brace <= 0) break;
            i++;
            continue;
        }
        if (c == '{') {
            brace++;
            i++;
            continue;
        }
        if (c == '}') {
            brace--;
            i++;
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') cur++;
                i++;
            }
            if (i + 1 >= n) {
                rerr(r, line, "EOF encountered inside an action");
                i = n;
                break;
            }
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            /* String or character constant.  A complete 'x' / '\x' is
            ** skipped whole; otherwise scan to the closing quote.  A
            ** newline ends it (and the action, at depth 0), as in flex. */
            if (c == '\'' && i + 2 < n && src[i + 1] != '\\' && src[i + 1] != '\'' &&
                src[i + 1] != '\n' && src[i + 2] == '\'') {
                i += 3;
                continue;
            }
            if (c == '\'' && i + 3 < n && src[i + 1] == '\\' && src[i + 2] != '\n' &&
                src[i + 3] == '\'') {
                i += 4;
                continue;
            }
            i++;
            while (i < n && src[i] != c && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < n) {
                    /* \ followed by newlines (continuations) then a char */
                    i++;
                    while (i < n && src[i] == '\n') {
                        cur++;
                        i++;
                        if (i < n && src[i] == '\\') i++;
                    }
                    if (i < n && src[i] != '\n') i++;
                    continue;
                }
                i++;
            }
            if (i < n && src[i] == c) i++;
            continue;
        }
        if (is_name_start((unsigned char)c)) {
            size_t s0 = i;
            while (i < n && is_name_char((unsigned char)src[i]))
                i++;
            if (ident_is(src + s0, i - s0, "REJECT")) *uses_reject = 1;
            if (ident_is(src + s0, i - s0, "yymore")) *uses_yymore = 1;
            continue;
        }
        i++;
    }
    size_t e = i; /* at the terminating newline, or n */
    size_t te = e;
    if (te > a && src[te - 1] == '\r') te--;
    char *t = xstrndup(src + a, te - a);
    *end = e < n ? e + 1 : n;
    *end_line = cur + (e < n ? 1 : 0);
    return t;
}

/* ============================================================
** Section 2
** ============================================================ */

static int add_rule(R *r, LimeFlexRule *fr, char *pat) {
    LimeFlexSpec *fs = r->fs;
    if (fs->n_rules == r->cap_rules) {
        int nc = r->cap_rules ? r->cap_rules * 2 : 32;
        LimeFlexRule *nr = realloc(fs->rules, (size_t)nc * sizeof(*nr));
        if (!nr) return -1;
        fs->rules = nr;
        char **np = realloc(r->rule_pat, (size_t)nc * sizeof(*np));
        if (!np) return -1;
        r->rule_pat = np;
        r->cap_rules = nc;
    }
    fs->rules[fs->n_rules] = *fr;
    r->rule_pat[fs->n_rules] = pat;
    fs->n_rules++;
    return 0;
}

static unsigned char *sc_set_new(const R *r) {
    return calloc((size_t)(r->fs->n_sc > 0 ? r->fs->n_sc : 1), 1);
}

/* Parse <a,b,*> at s[*i] (on the '<').  ORs names into set. */
static int parse_sc_list(R *r, const char *s, size_t n, size_t *i, unsigned char *set, int line) {
    size_t j = *i + 1;
    for (;;) {
        if (j >= n) {
            rerr(r, line, "bad <start condition>: missing >");
            return -1;
        }
        if (s[j] == '>') {
            j++;
            break;
        }
        if (s[j] == ',') {
            j++;
            continue;
        }
        if (s[j] == '*') {
            for (int k = 0; k < r->fs->n_sc; k++)
                set[k] = 1;
            j++;
            continue;
        }
        if (!is_name_start((unsigned char)s[j])) {
            rerr(r, line, "bad <start condition>: %c", s[j]);
            return -1;
        }
        size_t ns = j;
        while (j < n && is_name_char((unsigned char)s[j]))
            j++;
        int k = sc_lookup(r->fs, s + ns, j - ns);
        if (k < 0) {
            rerr(r, line, "undeclared start condition %.*s", (int)(j - ns), s + ns);
            return -1;
        }
        set[k] = 1;
    }
    *i = j;
    return 0;
}

static void read_section2(R *r, CodeVec *prolog) {
    int in_prolog = 1;
    while (!at_eof(r)) {
        const char *s;
        size_t n;
        cur_line(r, &s, &n);
        int line = r->line;
        if (n >= 2 && s[0] == '%' && s[1] == '%') {
            next_line(r);
            break;
        }
        if (in_prolog) {
            if (blank_line(s, n) || is_blank((unsigned char)s[0])) {
                if (!blank_line(s, n)) code_add(prolog, line, xstrndup(s, n));
                next_line(r);
                continue;
            }
            if (n >= 2 && s[0] == '%' && s[1] == '{') {
                next_line(r);
                code_add(prolog, line + 1, read_percent_block(r, line));
                continue;
            }
            in_prolog = 0;
        }
        if (blank_line(s, n)) {
            next_line(r);
            continue;
        }
        size_t i = 0;
        while (i < n && is_blank((unsigned char)s[i]))
            i++;
        if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
            if (i == 0) {
                rerr(r, line, "unrecognized rule (a comment in the rules section must be indented)");
                next_line(r);
                continue;
            }
            /* Indented comment: skipped (it may span lines). */
            size_t p = r->pos + i + 2;
            while (p + 1 < r->len && !(r->src[p] == '*' && r->src[p + 1] == '/')) {
                if (r->src[p] == '\n') r->line++;
                p++;
            }
            if (p + 1 >= r->len) {
                rerr(r, line, "EOF encountered inside a comment");
                r->pos = r->len;
                return;
            }
            size_t e = line_end(r, p);
            r->pos = e < r->len ? e + 1 : r->len;
            r->line++;
            continue;
        }
        if (i + 1 < n && s[i] == '%' && s[i + 1] == '{') {
            next_line(r);
            char *t = read_percent_block(r, line);
            if (t) {
                /* Placed before the next rule's case label, as flex does. */
                if (r->pending_code) {
                    SB b = { 0 };
                    sb_puts(&b, r->pending_code);
                    sb_puts(&b, t);
                    free(r->pending_code);
                    free(t);
                    t = sb_take(&b);
                }
                r->pending_code = t;
            }
            continue;
        }
        if (s[i] == '}' && r->n_scopes > 0 && blank_or_comment(s + i + 1, n - i - 1)) {
            free(r->scopes[--r->n_scopes]);
            next_line(r);
            continue;
        }

        /* A rule, an EOF rule, or a scope opener. */
        unsigned char *set = sc_set_new(r);
        if (!set) return;
        int scoped = 0;
        if (r->n_scopes > 0) {
            memcpy(set, r->scopes[r->n_scopes - 1], (size_t)r->fs->n_sc);
            scoped = 1;
        }
        if (s[i] == '<' && !(n - i >= 7 && memcmp(s + i, "<<EOF>>", 7) == 0)) {
            if (parse_sc_list(r, s, n, &i, set, line) < 0) {
                free(set);
                next_line(r);
                continue;
            }
            scoped = 1;
            if (i < n && s[i] == '{' && blank_or_comment(s + i + 1, n - i - 1)) {
                if (r->n_scopes == r->cap_scopes) {
                    int nc = r->cap_scopes ? r->cap_scopes * 2 : 8;
                    unsigned char **ns = realloc(r->scopes, (size_t)nc * sizeof(*ns));
                    if (!ns) {
                        free(set);
                        return;
                    }
                    r->scopes = ns;
                    r->cap_scopes = nc;
                }
                r->scopes[r->n_scopes++] = set;
                next_line(r);
                continue;
            }
        }
        LimeFlexRule fr;
        memset(&fr, 0, sizeof fr);
        fr.line = line;
        fr.sc = set;
        char *pat = NULL;
        size_t act_start;
        if (n - i >= 7 && memcmp(s + i, "<<EOF>>", 7) == 0) {
            fr.is_eof = 1;
            act_start = r->pos + i + 7;
        } else {
            /* NUL-terminated copy of the rest of the line. */
            char *rest = xstrndup(s + i, n - i);
            size_t used = 0;
            pat = translate_pattern(r, line, rest, &used, &fr.bol, &fr.trail, &fr.can_match_eol);
            free(rest);
            if (!pat) {
                free(set);
                next_line(r);
                continue;
            }
            act_start = r->pos + i + used;
            if (fr.bol) r->fs->bol_needed = 1;
            if (fr.trail) r->fs->has_trail = 1;
        }
        size_t end;
        int end_line, cont;
        fr.action_line = line;
        fr.action = read_action(r, act_start, line, &end, &end_line, &cont, &r->fs->uses_reject,
                                &r->fs->uses_yymore);
        fr.action_rule = cont ? -1 : r->fs->n_rules;
        fr.is_default = 0;
        /* A %{ %} block between rules: flex emits it just before the
        ** next rule's case label (so a #define in it still takes effect). */
        fr.pre_code = r->pending_code;
        r->pending_code = NULL;
        r->rule_scoped = realloc(r->rule_scoped, (size_t)(r->fs->n_rules + 1) * sizeof(int));
        if (r->rule_scoped) r->rule_scoped[r->fs->n_rules] = scoped;
        add_rule(r, &fr, pat);
        r->pos = end;
        r->line = end_line;
    }
    if (r->n_scopes > 0) rerr(r, r->line, "premature EOF: unclosed <start condition>{ scope");
}

/* ============================================================
** Build the LimeLexSpec
** ============================================================ */

static char *state_name(const LimeFlexSpec *fs, int sc, int bol) {
    SB b = { 0 };
    sb_puts(&b, sc == 0 ? "INITIAL" : fs->sc_names[sc]);
    if (bol) sb_puts(&b, "__bol");
    (void)fs;
    return sb_take(&b);
}

int lime_flex_state_index(const LimeFlexSpec *fs, int sc, int bol) {
    return (bol && fs->bol_needed) ? fs->n_sc + sc : sc;
}

static int build_spec(R *r) {
    LimeFlexSpec *fs = r->fs;
    LimeLexSpec *spec = fs->spec;
    int n_sc = fs->n_sc;

    /* Resolve `|` actions: each takes the next rule with its own action. */
    for (int k = fs->n_rules - 1, next = -1; k >= 0; k--) {
        if (fs->rules[k].action_rule < 0)
            fs->rules[k].action_rule = next;
        else
            next = k;
        if (fs->rules[k].action_rule < 0) {
            rerr(r, fs->rules[k].line, "unrecognized rule ('|' action on the last rule)");
            fs->rules[k].action_rule = k;
        }
    }

    /* Unscoped rules: INITIAL plus every inclusive condition.  EOF rules:
    ** an unscoped one applies to every condition without one yet. */
    fs->sc_eof_rule = malloc((size_t)n_sc * sizeof(int));
    if (!fs->sc_eof_rule) return -1;
    for (int k = 0; k < n_sc; k++)
        fs->sc_eof_rule[k] = -1;
    for (int k = 0; k < fs->n_rules; k++) {
        LimeFlexRule *fr = &fs->rules[k];
        int scoped = r->rule_scoped ? r->rule_scoped[k] : 0;
        if (fr->is_eof) {
            int any = 0;
            for (int c = 0; c < n_sc; c++) {
                if (scoped && !fr->sc[c]) continue;
                if (!scoped && fs->sc_eof_rule[c] >= 0) continue;
                if (fs->sc_eof_rule[c] >= 0) {
                    fprintf(stderr, "%s:%d: multiple <<EOF>> rules for start condition %s\n",
                            fs->filename ? fs->filename : "<stdin>", fr->line, fs->sc_names[c]);
                    fr->sc[c] = 0;
                    continue;
                }
                fs->sc_eof_rule[c] = k;
                fr->sc[c] = 1;
                any = 1;
            }
            if (!scoped && !any) rwarn(r, fr->line, "all start conditions already have <<EOF>> rules");
            continue;
        }
        if (!scoped)
            for (int c = 0; c < n_sc; c++)
                fr->sc[c] = (c == 0 || !fs->sc_excl[c]);
    }

    /* The default rule: any single byte, in every condition, last. */
    {
        LimeFlexRule d;
        memset(&d, 0, sizeof d);
        d.line = r->line;
        d.is_default = 1;
        d.can_match_eol = 1;
        d.action_rule = fs->n_rules;
        d.action_line = r->line;
        d.sc = sc_set_new(r);
        if (!d.sc) return -1;
        memset(d.sc, 1, (size_t)n_sc);
        r->rule_scoped = realloc(r->rule_scoped, (size_t)(fs->n_rules + 1) * sizeof(int));
        if (add_rule(r, &d, xstrdup("[\\x00-\\xff]")) < 0) return -1;
    }

    /* States: SC 1..n-1 (INITIAL is implicit), then the BOL variants. */
    LimeLexState **stail = &spec->states;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && !fs->bol_needed) break;
        for (int c = (pass == 0 ? 1 : 0); c < n_sc; c++) {
            LimeLexState *st = calloc(1, sizeof(*st));
            if (!st) return -1;
            st->name = state_name(fs, c, pass);
            st->exclusive = 1;
            st->line = 0;
            *stail = st;
            stail = &st->next;
        }
    }

    /* Rules, in file order, each qualified with its Lime states. */
    LimeLexRule **rtail = &spec->rules;
    for (int k = 0; k < fs->n_rules; k++) {
        LimeFlexRule *fr = &fs->rules[k];
        LimeLexRule *lr = calloc(1, sizeof(*lr));
        if (!lr) return -1;
        char nm[32];
        snprintf(nm, sizeof nm, "rule%d", k + 1);
        lr->name = xstrdup(nm);
        lr->line = fr->line;
        lr->is_eof = fr->is_eof;
        lr->pattern = r->rule_pat[k];
        r->rule_pat[k] = NULL;
        lr->states = calloc((size_t)(2 * n_sc), sizeof(char *));
        if (!lr->states) return -1;
        for (int c = 0; c < n_sc; c++) {
            if (!fr->sc[c]) continue;
            if (fr->is_eof) {
                lr->states[lr->n_states++] = state_name(fs, c, 0);
                continue;
            }
            if (!fr->bol) lr->states[lr->n_states++] = state_name(fs, c, 0);
            if (fs->bol_needed) lr->states[lr->n_states++] = state_name(fs, c, 1);
        }
        *rtail = lr;
        rtail = &lr->next;
    }
    fs->n_user_rules = fs->n_rules - 1;
    return 0;
}

/* ============================================================
** Public entry points
** ============================================================ */

int lime_flex_detect(const char *src, size_t len) {
    size_t i = 0;
    while (i < len) {
        if (i + 1 < len && src[i] == '%' && src[i + 1] == '%') {
            size_t j = i + 2;
            /* "%%" then end of line, blanks, or anything flex allows. */
            if (j >= len || src[j] == '\n' || src[j] == '\r' || is_blank((unsigned char)src[j]))
                return 1;
        }
        const char *e = memchr(src + i, '\n', len - i);
        if (!e) break;
        i = (size_t)(e - src) + 1;
    }
    return 0;
}

static void apply_cli(LimeFlexOptions *o, const LimeFlexCliOptions *cli) {
    if (!cli) return;
    if (cli->caseless) o->caseless = 1;
    if (cli->posix_compat) o->posix_compat = 1;
    if (cli->nodefault) o->nodefault = 1;
    if (cli->debug) o->debug = 1;
    if (cli->reentrant) o->reentrant = 1;
    if (cli->bison_bridge) o->bison_bridge = 1;
    if (cli->bison_locations) o->bison_locations = o->bison_bridge = 1;
    if (cli->yylineno) o->yylineno = 1;
    if (cli->noyywrap) o->yywrap = 0;
    if (cli->never_interactive) o->never_interactive = 1;
    if (cli->always_interactive) o->always_interactive = 1;
    if (cli->noline) o->noline = 1;
    if (cli->nounistd) o->unistd = 0;
    if (cli->prefix) {
        free(o->prefix);
        o->prefix = xstrdup(cli->prefix);
    }
}

LimeFlexSpec *lime_flex_read(const char *filename, const char *src, size_t len,
                             const LimeFlexCliOptions *cli) {
    LimeFlexSpec *fs = calloc(1, sizeof(*fs));
    if (!fs) return NULL;
    fs->filename = filename;
    fs->spec = lime_lex_spec_new(filename);
    if (!fs->spec) {
        free(fs);
        return NULL;
    }
    {
        LimeFlexOptions *o = &fs->opt; /* -1 = not given */
        o->reentrant = o->bison_bridge = o->bison_locations = o->yylineno = o->yywrap = -1;
        o->nodefault = o->caseless = o->stack = o->main_fn = o->posix_compat = o->debug = -1;
        o->never_interactive = o->always_interactive = o->noline = -1;
        o->unput = o->input = o->yy_push_state = o->yy_pop_state = o->yy_top_state = -1;
        o->yyalloc = o->yyrealloc = o->yyfree = o->unistd = o->stdinit = o->reject = -1;
        o->yymore = o->warn = o->eightbit = o->backup = -1;
    }
    sc_declare(&(R){ .fs = fs }, "INITIAL", 7, 0, 0);

    R r;
    memset(&r, 0, sizeof r);
    r.fs = fs;
    r.src = src;
    r.len = len;
    r.line = 1;
    CodeVec top = { 0 }, defs = { 0 }, prolog = { 0 };
    int rc = read_section1(&r, &top, &defs);
    apply_cli(&fs->opt, cli);
    if (fs->opt.bison_locations == 1) fs->opt.bison_bridge = 1;
    if (fs->opt.reject == 0) fs->uses_reject = -1; /* %option noreject */
    r.caseless = fs->opt.caseless == 1;
    r.posix = fs->opt.posix_compat == 1;
    if (rc == 0) {
        read_section2(&r, &prolog);
        if (!at_eof(&r)) {
            fs->user_code.line = r.line;
            fs->user_code.text = xstrndup(src + r.pos, len - r.pos);
        }
    }
    fs->top_code = top.v;
    fs->n_top_code = top.n;
    fs->defs_code = defs.v;
    fs->n_defs_code = defs.n;
    fs->prolog_code = prolog.v;
    fs->n_prolog_code = prolog.n;
    if (fs->opt.reject == 1) fs->uses_reject = 1;
    if (fs->uses_reject < 0) fs->uses_reject = 0;
    if (fs->opt.yymore == 1) fs->uses_yymore = 1;
    if (fs->opt.yymore == 0) fs->uses_yymore = 0;
    if (rc == 0 && fs->error_count == 0) build_spec(&r);
    fs->spec->error_count += fs->error_count;
    /* Reader scratch. */
    for (Def *d = r.defs; d;) {
        Def *nx = d->next;
        free(d->name);
        free(d->text);
        free(d);
        d = nx;
    }
    for (int k = 0; k < r.n_scopes; k++)
        free(r.scopes[k]);
    free(r.scopes);
    free(r.pending_code);
    if (r.rule_pat) {
        for (int k = 0; k < fs->n_rules; k++)
            free(r.rule_pat[k]);
        free(r.rule_pat);
    }
    free(r.rule_scoped);
    return fs;
}

static void free_code(LimeFlexCode *v, int n) {
    for (int i = 0; i < n; i++)
        free(v[i].text);
    free(v);
}

void lime_flex_spec_free(LimeFlexSpec *fs) {
    if (!fs) return;
    lime_lex_spec_free(fs->spec);
    for (int i = 0; i < fs->n_sc; i++)
        free(fs->sc_names[i]);
    free(fs->sc_names);
    free(fs->sc_excl);
    free(fs->sc_eof_rule);
    for (int i = 0; i < fs->n_rules; i++) {
        free(fs->rules[i].sc);
        free(fs->rules[i].action);
        free(fs->rules[i].pre_code);
    }
    free(fs->rules);
    free_code(fs->top_code, fs->n_top_code);
    free_code(fs->defs_code, fs->n_defs_code);
    free_code(fs->prolog_code, fs->n_prolog_code);
    free(fs->user_code.text);
    free(fs->opt.prefix);
    free(fs->opt.outfile);
    free(fs->opt.header_file);
    free(fs->opt.extra_type);
    free(fs);
}
