/*
** src/emit_c_skin_bison.c -- bison-API-compatibility skin for the C
** output target.
**
** When `lime --target=c:bison` is given, this module emits two extra
** files alongside the standard `<grammar>.c` / `<grammar>.h`:
**
**     <grammar>_bison.h    -- bison-style public surface (yyparse,
**                              yylex, yylval, YYSTYPE, enum yytokentype)
**     <grammar>_bison.c    -- thin adapter that drives Lime's
**                              generated parser from yylex() and
**                              presents the bison-style API to callers
**
** The skin does not modify or replace the standard Lime output; both
** files are written next to the standard ones, and consumers can
** compile only the bison skin (plus the standard .c) into their
** binary if they want the bison surface.
**
** Design summary
** --------------
**
**   * Token codes: bison's enum starts at 258 for named tokens (256
**     == YYerror, 257 == YYUNDEF, 0..255 reserved for ASCII char
**     literals).  Lime's internal codes are 1..nterminal-1.  The
**     skin maps:
**
**         bison code 258 + (i - 1)  <->  lime code i        for i in [1, nterminal)
**         bison code 0              <->  lime code 0        (EOF)
**
**     The .c file embeds a translation table built at emit time.
**
**   * YYSTYPE: Lime exposes `<Name>TOKENTYPE` (the type of the
**     terminal slot in the value stack).  The skin picks YYSTYPE
**     in this priority order:
**
**       1. `%token_type {T}`     ->  typedef T YYSTYPE;
**       2. `%union { body }`     ->  typedef union { body } YYSTYPE;
**       3. (neither)             ->  typedef void * YYSTYPE;
**
**     The standard parser's print_stack_union() emits the same
**     YYSTYPE typedef under YYSTYPE_IS_DECLARED guards when %union
**     is set, so the bison adapter and the standard parser stack
**     agree on the type byte-for-byte.
**
**   * yydebug: bison-style global trace flag.  The skin emits
**     `int yydebug = 0;` at file scope; yyparse_extra() consults it
**     on entry and routes to Lime's `<Name>Trace(stderr, ">> ")`
**     when set.  Compiled out under -DNDEBUG (matching Lime's own
**     Trace gating).
**
**   * yyparse(): allocates a Lime parser, drives it from yylex() in
**     a loop, returns 0 on success / 1 on syntax error (detected by
**     the user's %syntax_error block calling yyerror) / 2 on
**     allocation failure.  When the grammar declares
**     %extra_argument, we additionally emit `yyparse_extra(<arg>)`
**     and route the strict bison-API yyparse() through a
**     weakly-referenced global of the same identifier.
**
**   * yyerror() and yylex() are EXTERN in the skin -- the caller
**     supplies them, exactly like bison.  The user's %syntax_error
**     block is expected to call yyerror() if the consumer wants
**     bison-style error reporting; the skin does not auto-inject
**     yyerror() into syntax_error, since the directive body is
**     copied verbatim into the generated parser.
**
** What is NOT supported (yet)
** --------------------------
**
**   * Tagged tokens (`%token<field> NAME`).  Bison's %union
**     pairs with `%token<field>` so the parser can pick the
**     correct union arm per token; Lime does not parse the angle-
**     bracketed tag.  Workaround: the user's yylex() writes
**     `yylval.<field> = ...` directly before returning, exactly
**     as in early-bison style.  See docs/SKINS.md.
**   * yychar / yynerrs  -- bison globals; not provided.  Lime's
**                          parser does not expose lookahead state
**                          via globals.
**   * %parse-param      -- bison's per-parser parameter list.  Use
**                          %extra_argument instead; the skin emits
**                          yyparse_extra().
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lime;
struct symbol;

/* Bridge functions (already declared in lime.c for the Rust skin).
** Reused here because they expose exactly what the bison emitter
** needs: terminal count, name lookup, %extra_argument / %token_type
** strings.  The few extras we need (number of terminals minus
** error+$ symbols, the parser's %name) we obtain via dedicated
** accessors below. */

extern int   lime_emit_rust_get_nterminal(const struct lime *lemp);
extern const char *lime_emit_rust_get_name(const struct lime *lemp);
extern struct symbol *lime_emit_rust_symbol_at(const struct lime *lemp, int i);
extern const char     *lime_emit_rust_symbol_name(const struct symbol *sp);

/* Additional bridge declared at the bottom of lime.c (alongside the
** Rust bridge helpers). */
extern const char *lime_emit_c_skin_get_arg(const struct lime *lemp);
extern const char *lime_emit_c_skin_get_tokentype(const struct lime *lemp);
extern const char *lime_emit_c_skin_get_union(const struct lime *lemp);
extern const char *lime_emit_c_skin_symbol_union_field(const struct symbol *sp);
extern const char *lime_emit_c_skin_get_tokenprefix(const struct lime *lemp);
extern int         lime_emit_c_skin_get_first_token(const struct lime *lemp);
extern int         lime_emit_c_skin_has_locations(const struct lime *lemp);

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Uppercase a copy of `s` into `out` (size `n` bytes).  Non-alnum is
** replaced with '_'.  Used to derive the include guard from the
** parser %name. */
static void uppercase_id(char *out, size_t n, const char *s) {
    size_t i = 0;
    if (n == 0) return;
    for (; s && s[i] != 0 && i + 1 < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') {
            out[i] = (char)(c - 'a' + 'A');
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            out[i] = (char)c;
        } else {
            out[i] = '_';
        }
    }
    out[i] = 0;
}

/* Extract the trailing identifier of a %extra_argument declaration.
** Lime stores the full declaration text (`int *result`,
** `struct foo bar`, etc.).  The bare name is the last alnum/_ run.
** Returns 0 on failure (no identifier found). */
static int extract_arg_identifier(const char *decl, char *out, size_t n) {
    if (decl == NULL || n == 0) return 0;
    size_t end = strlen(decl);
    while (end > 0 && (decl[end-1] == ' ' || decl[end-1] == '\t')) end--;
    size_t start = end;
    while (start > 0) {
        unsigned char c = (unsigned char)decl[start-1];
        if (!(isalnum(c) || c == '_')) break;
        start--;
    }
    if (start == end) return 0;
    size_t len = end - start;
    if (len + 1 > n) len = n - 1;
    memcpy(out, decl + start, len);
    out[len] = 0;
    return 1;
}

/* Extract the type-qualifier portion of a %extra_argument decl by
** removing the trailing identifier (and any whitespace separating
** type from name).  Returns a heap-allocated copy on success; the
** caller must free().  Returns NULL on failure. */
static char *extract_arg_type(const char *decl) {
    if (decl == NULL) return NULL;
    size_t end = strlen(decl);
    while (end > 0 && (decl[end-1] == ' ' || decl[end-1] == '\t')) end--;
    size_t name_end = end;
    while (name_end > 0) {
        unsigned char c = (unsigned char)decl[name_end-1];
        if (!(isalnum(c) || c == '_')) break;
        name_end--;
    }
    /* name_end is now the boundary between type-text and identifier.
    ** Walk it back over whitespace separating the two. */
    size_t type_end = name_end;
    while (type_end > 0 && (decl[type_end-1] == ' ' || decl[type_end-1] == '\t')) {
        type_end--;
    }
    if (type_end == 0) return NULL;
    char *out = (char *)malloc(type_end + 1);
    if (out == NULL) return NULL;
    memcpy(out, decl, type_end);
    out[type_end] = 0;
    return out;
}

/* Effective YYSTYPE / per-symbol slot type for the bison skin.  The
** lookup order matches print_stack_union() in lime.c so the standard
** parser and the bison adapter agree byte-for-byte on the type that
** Lime's parse stack stores per terminal:
**
**     1. %token_type {T}        -> T            (user override wins)
**     2. %union { body }        -> YYSTYPE      (typedef'd from body)
**     3. neither                -> void *       (Lime default)
**
** Returns a pointer to a static string; callers must not free. */
static const char *effective_value_type(const struct lime *lemp) {
    const char *tt = lime_emit_c_skin_get_tokentype(lemp);
    if (tt && tt[0]) return tt;
    if (lime_emit_c_skin_get_union(lemp) != NULL) return "YYSTYPE";
    return "void *";
}

/* Forward-declare any struct / union / enum tags that appear in the
** %extra_argument declaration text, so prototypes referencing those
** tags do not introduce them in prototype scope (C11 6.2.1p4).  If
** the .h declared `int yyparse_extra(struct foo *x);` without a
** prior `struct foo;` at file scope, the tag is local to that
** prototype and is incompatible with the same struct elsewhere --
** which is exactly the gotcha that bites real users porting from
** bison.  We scan `decl` for the keywords struct/union/enum followed
** by an identifier and emit a forward declaration for each.
** Duplicates are harmless in C; we do not bother deduping. */
static void emit_extra_arg_forward_decls(FILE *out, const char *decl) {
    if (decl == NULL || decl[0] == 0) return;
    static const char *kw[] = { "struct", "union", "enum", NULL };
    for (int k = 0; kw[k] != NULL; k++) {
        const char *needle = kw[k];
        size_t nl = strlen(needle);
        const char *p = decl;
        while ((p = strstr(p, needle)) != NULL) {
            /* Token-boundary check: char before must be non-id. */
            if (p != decl) {
                unsigned char prev = (unsigned char)p[-1];
                if (isalnum(prev) || prev == '_') {
                    p += nl;
                    continue;
                }
            }
            const char *q = p + nl;
            while (*q == ' ' || *q == '\t') q++;
            const char *id_start = q;
            while (*q && (isalnum((unsigned char)*q) || *q == '_')) q++;
            if (q == id_start) {
                /* No identifier after the keyword (e.g. `struct {`).
                ** No forward decl is possible. */
                p += nl;
                continue;
            }
            fprintf(out, "%s ", needle);
            fwrite(id_start, 1, (size_t)(q - id_start), out);
            fprintf(out, ";\n");
            p = q;
        }
    }
}


static void emit_header(FILE *out, const struct lime *lemp,
                        const char *base_id, const char *parser_name) {
    char guard[256];
    uppercase_id(guard, sizeof guard, base_id);
    /* Append _BISON_H to the guard. */
    size_t gl = strlen(guard);
    snprintf(guard + gl, sizeof guard - gl, "_BISON_H");

    int nterminal = lime_emit_rust_get_nterminal(lemp);
    const char *tokenprefix = lime_emit_c_skin_get_tokenprefix(lemp);
    if (tokenprefix == NULL) tokenprefix = "";
    const char *tokentype = lime_emit_c_skin_get_tokentype(lemp);
    const char *union_body = lime_emit_c_skin_get_union(lemp);
    const char *value_type = effective_value_type(lemp);
    int has_locs = lime_emit_c_skin_has_locations(lemp);

    fprintf(out,
        "/* Generated by Lime %s skin (--target=c:bison) -- do not edit. */\n"
        "/* bison-API-compatible surface over the Lime parser. */\n"
        "\n"
        "#ifndef %s\n"
        "#define %s\n"
        "\n"
        "#include <stddef.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n",
        parser_name ? parser_name : "Parse",
        guard, guard);

    /* enum yytokentype: bison contract -- 0 == YYEOF, 256 == YYerror,
    ** 257 == YYUNDEF, then named tokens at 258, 259, ...  The Lime
    ** internal codes (1..nterminal-1) map to the bison codes by
    ** adding 257 (so Lime 1 -> bison 258).  Skip Lime's $ pseudo at
    ** index 0 -- it's the EOF sentinel. */
    fprintf(out,
        "/* bison-style token codes.  Named tokens start at 258 to leave\n"
        "** room for the 0..255 ASCII range (bison's char-literal tokens)\n"
        "** plus YYerror (256) and YYUNDEF (257).  See docs/SKINS.md. */\n"
        "#ifndef YYTOKENTYPE\n"
        "# define YYTOKENTYPE\n"
        "enum yytokentype {\n"
        "    YYEMPTY = -2,\n"
        "    YYEOF = 0,\n"
        "    YYerror = 256,\n"
        "    YYUNDEF = 257");
    /* Emit one enum constant per terminal symbol (skip index 0 = $).
    ** v0.9.3: tagged tokens (%token<field> NAME) get a trailing
    ** /yylval.<field>/ block-comment in the emitted output so the
    ** user's yylex() and reduce-action code have the union arm
    ** documented at the point of use.  Untagged tokens emit no
    ** comment.  See docs/SKINS.md "Tagged tokens". */
    int any_tagged = 0;
    for (int i = 1; i < nterminal; i++) {
        struct symbol *sp = lime_emit_rust_symbol_at(lemp, i);
        const char *nm = lime_emit_rust_symbol_name(sp);
        const char *uf = lime_emit_c_skin_symbol_union_field(sp);
        fprintf(out, ",\n    %s%s = %d", tokenprefix, nm, 257 + i);
        if (uf && uf[0]) {
            fprintf(out, "  /* yylval.%s */", uf);
            any_tagged = 1;
        }
    }
    fprintf(out,
        "\n};\n"
        "typedef enum yytokentype yytoken_kind_t;\n"
        "#endif /* !YYTOKENTYPE */\n"
        "\n");
    if (any_tagged) {
        fprintf(out,
            "/* Tagged tokens (above): each `yylval.<field>` comment\n"
            "** marks the YYSTYPE union arm carrying the token's\n"
            "** semantic value.  The user's yylex() should write the\n"
            "** named arm before returning the token code; the user's\n"
            "** reduce action accesses `K.<field>` to read it back.\n"
            "** See docs/SKINS.md \"Tagged tokens\". */\n\n");
    }

    /* YYSTYPE typedef.  Three cases:
    **   1. %union { body } -- emit `typedef union { body } YYSTYPE;`,
    **      mirroring bison's %union directive byte-for-byte.  The
    **      standard Lime parser's print_stack_union emits the same
    **      typedef under YYSTYPE_IS_DECLARED guards so callers can
    **      use either header without ODR drift.
    **   2. %token_type {T} -- emit `typedef T YYSTYPE;`.  Lime's
    **      terminal slot type is T; YYSTYPE is just an alias.
    **   3. neither -- emit `typedef void * YYSTYPE;` (Lime default).
    ** All three forms gate on YYSTYPE_IS_DECLARED so a project-wide
    ** YYSTYPE predeclaration in a shared header takes precedence. */
    fprintf(out,
        "/* Semantic value type.  Lime's parser stack stores this for\n"
        "** every terminal; the bison skin exposes it as YYSTYPE so the\n"
        "** user's yylex() can write yylval.<field> = ... in the bison\n"
        "** idiom.  When the grammar uses %%union, the union body is\n"
        "** emitted verbatim; %%token_type sets a plain type alias;\n"
        "** otherwise YYSTYPE defaults to `void *`. */\n"
        "#ifndef YYSTYPE_IS_DECLARED\n");
    if (union_body && union_body[0]) {
        fprintf(out, "typedef union {%s} YYSTYPE;\n", union_body);
    } else {
        fprintf(out, "typedef %s YYSTYPE;\n",
                tokentype ? tokentype : "void *");
    }
    fprintf(out,
        "# define YYSTYPE_IS_DECLARED 1\n"
        "# define YYSTYPE_IS_TRIVIAL 1\n"
        "#endif\n"
        "extern YYSTYPE yylval;\n"
        "\n");
    (void)value_type;

    /* YYLTYPE: emitted only when %location_type / has_locations. */
    if (has_locs) {
        fprintf(out,
            "/* Location-tracking surface.  Generated because the Lime\n"
            "** grammar enabled %%location_type / location tracking. */\n"
            "#ifndef YYLTYPE_IS_DECLARED\n"
            "typedef struct YYLTYPE {\n"
            "    int first_line;\n"
            "    int first_column;\n"
            "    int last_line;\n"
            "    int last_column;\n"
            "} YYLTYPE;\n"
            "# define YYLTYPE_IS_DECLARED 1\n"
            "#endif\n"
            "extern YYLTYPE yylloc;\n"
            "\n");
    }

    /* User-supplied callbacks. */
    /* Forward-declare any struct/union/enum tags from %extra_argument
    ** at file scope so subsequent prototypes refer to the same tag
    ** (avoiding C11 6.2.1p4 prototype-scope tag introduction). */
    {
        const char *arg_pre = lime_emit_c_skin_get_arg(lemp);
        if (arg_pre && arg_pre[0]) {
            fprintf(out,
                "/* Forward decls for struct/union/enum tags referenced\n"
                "** by %%extra_argument so prototypes do not introduce\n"
                "** them in prototype scope (C11 6.2.1p4). */\n");
            emit_extra_arg_forward_decls(out, arg_pre);
            fprintf(out, "\n");
        }
    }
    fprintf(out,
        "/* The caller supplies these, exactly as in a bison-generated\n"
        "** parser.  yyerror() is invoked from the user's %%syntax_error\n"
        "** block (the skin does not auto-bridge the two; the body of\n"
        "** %%syntax_error is copied verbatim into the parser, so write\n"
        "** `yyerror(\"syntax error\");` there if you want bison-style\n"
        "** error reporting). */\n"
        "extern int  yylex(void);\n"
        "extern void yyerror(const char *msg);\n"
        "\n");

    /* yyparse / yyparse_extra. */
    const char *arg = lime_emit_c_skin_get_arg(lemp);
    fprintf(out,
        "/* bison-style entry point.  Drives Lime's parser from yylex()\n"
        "** until EOF.  Returns:\n"
        "**     0 -- successful parse\n"
        "**     1 -- syntax error (yyerror was invoked)\n"
        "**     2 -- memory exhaustion (parser allocation failed)\n"
        "*/\n"
        "int yyparse(void);\n");
    if (arg && arg[0]) {
        fprintf(out,
            "\n"
            "/* Variant that accepts the Lime grammar's %%extra_argument\n"
            "** directly, mirroring bison's %%parse-param mechanism. */\n"
            "int yyparse_extra(%s);\n",
            arg);
    }

    /* yydebug global.  Bison contract: int yydebug; setting it
    ** non-zero before yyparse() enables a stderr trace.  The skin
    ** wires this to Lime's <Name>Trace() inside yyparse_extra(). */
    fprintf(out,
        "\n"
        "/* bison-style runtime trace flag.  Set non-zero before\n"
        "** calling yyparse() to enable a parse-step trace on stderr;\n"
        "** zero (the default) disables it.  The skin maps this to\n"
        "** Lime's <Name>Trace() API, which is itself compiled out\n"
        "** when the standard Lime parser is built with -DNDEBUG.\n"
        "** See docs/SKINS.md `Debug tracing'. */\n"
        "extern int yydebug;\n");

    fprintf(out,
        "\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n"
        "\n"
        "#endif /* %s */\n",
        guard);
}

/* ------------------------------------------------------------------ */
/*  Source emission: <basename>_bison.c                                */
/* ------------------------------------------------------------------ */

static void emit_source(FILE *out, const struct lime *lemp,
                        const char *base_id, const char *parser_name) {
    int nterminal = lime_emit_rust_get_nterminal(lemp);
    int first_token = lime_emit_c_skin_get_first_token(lemp);
    const char *arg = lime_emit_c_skin_get_arg(lemp);
    const char *value_type = effective_value_type(lemp);
    int has_locs = lime_emit_c_skin_has_locations(lemp);
    const char *name = parser_name ? parser_name : "Parse";

    fprintf(out,
        "/* Generated by Lime %s skin (--target=c:bison) -- do not edit. */\n"
        "/* bison-API-compatible adapter that drives the standard Lime\n"
        "** parser from yylex().  Pair with %s_bison.h. */\n"
        "\n"
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "#include <string.h>\n"
        "#include \"%s_bison.h\"\n"
        "\n",        name, base_id, base_id);

    /* Forward declarations of the standard Lime parser API.  We
    ** intentionally do NOT include <basename>.h: that header #defines
    ** every token name to its Lime-internal code (1..N), which
    ** would collide with the enum constants in <basename>_bison.h that
    ** assign the same names to bison codes (258..258+N). */
    /* Mirror the .h's forward decls of any struct/union/enum tags
    ** referenced in %extra_argument.  The .h declarations alone are
    ** not enough -- this TU includes the .h, but the .h's tag
    ** declarations live in prototype scope when the user has not
    ** pre-declared them.  Emitting again at file scope here makes
    ** the parameter type identical to the user's struct definition
    ** in their TU. */
    if (arg && arg[0]) {
        emit_extra_arg_forward_decls(out, arg);
    }
    fprintf(out,
        "/* Forward declarations of the standard Lime parser API.  We\n"
        "** intentionally do NOT include <%s.h>: that header #defines\n"
        "** every token name to its Lime-internal code (1..N), which\n"
        "** would collide with the enum constants in <%s_bison.h> that\n"
        "** assign the same names to bison codes (258..258+N). */\n"
        "extern void *%sAlloc(void *(*mallocProc)(size_t));\n",
        base_id, base_id, name);
    fprintf(out, "extern void  %sFree(void *p, void (*freeProc)(void *));\n", name);
    fprintf(out, "extern void  %s(void *yyp, int yymajor, %s yyminor",
            name, value_type);
    if (arg && arg[0]) {
        fprintf(out, ", %s", arg);
    }
    fprintf(out, ");\n");
    /* Lime's <Name>Trace() is the standard parser's runtime-trace
    ** entry, conditionally compiled under #ifndef NDEBUG (matching
    ** the limpar.c template).  Forward-declare it under the same
    ** guard so the bison skin's yydebug bridge links cleanly in
    ** debug builds and is silently a no-op in release builds. */
    fprintf(out,
        "#ifndef NDEBUG\n"
        "extern void %sTrace(FILE *TraceFILE, char *zTracePrompt);\n"
        "#endif\n"
        "\n",
        name);

    /* Globals.  YYSTYPE and YYLTYPE (when present) are visible in the
    ** header; instantiate them here so users do not have to. */
    fprintf(out,
        "/* The bison-style semantic-value global.  yylex() writes it\n"
        "** before returning; yyparse() reads it as the per-token value\n"
        "** to push onto Lime's parse stack. */\n"
        "YYSTYPE yylval;\n");
    /* yydebug: bison-compatible trace flag.  Definition lives here
    ** (file-scope, zero-initialised) so consumers can `extern int
    ** yydebug; yydebug = 1;` exactly as with bison.  The wiring into
    ** Lime's trace API happens inside yyparse_extra() below. */
    fprintf(out,
        "/* bison-style runtime trace flag.  yyparse_extra() inspects\n"
        "** this on entry and toggles Lime's <Name>Trace() accordingly.\n"
        "** Default 0 (tracing off).  In release builds (-DNDEBUG)\n"
        "** Lime omits the Trace() function and the wiring is a\n"
        "** silent no-op. */\n"
        "int yydebug = 0;\n");
    if (has_locs) {
        fprintf(out,
            "/* Location-tracking global, populated by yylex() in the\n"
            "** bison idiom and threaded through to Lime's location-aware\n"
            "** parse loop. */\n"
            "YYLTYPE yylloc;\n");
    }
    fprintf(out, "\n");

    /* Translation table: bison code -> Lime internal code.  Indexed
    ** by (bison_code - 258).  Size = nterminal - 1 (we skip Lime's $
    ** pseudo at index 0).  Each entry is the corresponding Lime
    ** code, ALREADY ADJUSTED for %first_token (Lime's runtime
    ** subtracts YYFIRSTTOKEN before indexing the action table; we
    ** pass through the externally-visible Lime code). */
    fprintf(out,
        "/* bison-code -> Lime-code translation.  Indexed by\n"
        "** (bison_code - 258).  The entry is the externally-visible\n"
        "** Lime token code; Lime's parser subtracts YYFIRSTTOKEN\n"
        "** internally if the grammar declared %%first_token. */\n"
        "static const int yy_bison_to_lime[] = {");
    for (int i = 1; i < nterminal; i++) {
        struct symbol *sp = lime_emit_rust_symbol_at(lemp, i);
        const char *nm = lime_emit_rust_symbol_name(sp);
        fprintf(out, "%s\n    %d /* %s */",
                (i == 1) ? "" : ",",
                i + first_token,
                nm);
    }
    fprintf(out, "\n};\n"
        "static const int YY_BISON_NTOKEN = %d;\n\n",
        nterminal - 1);

    /* Translate helper. */
    fprintf(out,
        "/* Translate a bison-style yylex() return value into the Lime\n"
        "** internal token code.  Returns -1 if the bison code is out\n"
        "** of range; the caller treats that as a syntax error. */\n"
        "static int yy_xlat_token(int yytok) {\n"
        "    if (yytok == 0) return 0;          /* YYEOF */\n"
        "    if (yytok < 258) return -1;        /* ASCII / YYerror / YYUNDEF */\n"
        "    int idx = yytok - 258;\n"
        "    if (idx >= YY_BISON_NTOKEN) return -1;\n"
        "    return yy_bison_to_lime[idx];\n"
        "}\n\n");

    /* yyparse_extra (when %extra_argument) and yyparse. */
    char arg_id[64] = {0};
    int have_arg_id = 0;
    if (arg && arg[0]) {
        have_arg_id = extract_arg_identifier(arg, arg_id, sizeof arg_id);
    }

    if (arg && arg[0] && have_arg_id) {
        fprintf(out,
            "/* Drives Lime's parser, threading the user's\n"
            "** %%extra_argument (`%s`) through every Lime call.  The\n"
            "** bison-strict yyparse() below routes through a default\n"
            "** value for that argument. */\n"
            "int yyparse_extra(%s) {\n"
            "    /* Bison-style runtime tracing: when yydebug is set,\n"
            "    ** route to Lime's <Name>Trace() with a default `>> '\n"
            "    ** prefix.  Toggle either way every call so flipping\n"
            "    ** yydebug between calls works as in bison.  Compiled\n"
            "    ** out under -DNDEBUG to match Lime's Trace gating. */\n"
            "#ifndef NDEBUG\n"
            "    if (yydebug) %sTrace(stderr, (char *)\">> \");\n"
            "    else         %sTrace((FILE *)0, (char *)0);\n"
            "#endif\n"
            "    void *parser = %sAlloc(malloc);\n"
            "    if (parser == NULL) return 2;\n"
            "    int errors = 0;\n"
            "    int yytok;\n"
            "    while ((yytok = yylex()) > 0) {\n"
            "        int lime = yy_xlat_token(yytok);\n"
            "        if (lime < 0) {\n"
            "            yyerror(\"syntax error: invalid token\");\n"
            "            errors = 1;\n"
            "            break;\n"
            "        }\n"
            "        %s(parser, lime, yylval, %s);\n"
            "    }\n"
            "    /* EOF sentinel -- runs %%accept / %%syntax_error / etc. */\n"
            "    %s(parser, 0, (YYSTYPE){0}, %s);\n"
            "    %sFree(parser, free);\n"
            "    return errors;\n"
            "}\n\n",
            arg, arg,
            name, name,
            name,
            name, arg_id, name, arg_id, name);

        /* Strict bison API yyparse(void): forwards to yyparse_extra
        ** with a zero-initialised `arg`.  We construct the zero
        ** through a union-typed local so this works for ANY type
        ** the user wrote in %extra_argument -- including pointer
        ** types, structs, and types whose name we cannot easily
        ** strip from the identifier without a full C parser.
        **
        ** Note for consumers with non-trivial %extra_argument types:
        ** call yyparse_extra() directly with your own value.  The
        ** zero-init shim below is a courtesy for the bison-strict
        ** "int yyparse(void)" signature only. */
        char *arg_type = extract_arg_type(arg);
        if (arg_type == NULL) {
            /* Fallback: emit a stub that loudly refuses to run.
            ** Almost never reached -- only when the %extra_argument
            ** text is so unusual we cannot find a type-prefix. */
            fprintf(out,
                "int yyparse(void) {\n"
                "    yyerror(\"yyparse(void): %%extra_argument has "
                "no extractable type; call yyparse_extra() directly\");\n"
                "    return 1;\n"
                "}\n");
        } else {
            fprintf(out,
                "/* Strict-bison entry point.  Forwards to yyparse_extra()\n"
                "** with a zero-initialised `%s`.  When the consumer\n"
                "** wants to thread their own value through the parser,\n"
                "** they should call yyparse_extra() directly instead. */\n"
                "int yyparse(void) {\n"
                "    union { unsigned char _b[sizeof(%s)]; %s v; } zero;\n"
                "    memset(&zero, 0, sizeof zero);\n"
                "    return yyparse_extra(zero.v);\n"
                "}\n",
                arg_id, arg_type, arg_type);
            free(arg_type);
        }
    } else {
        fprintf(out,
            "int yyparse(void) {\n"
            "    /* Bison-style runtime tracing: see yyparse_extra in\n"
            "    ** the %%extra_argument variant for the rationale. */\n"
            "#ifndef NDEBUG\n"
            "    if (yydebug) %sTrace(stderr, (char *)\">> \");\n"
            "    else         %sTrace((FILE *)0, (char *)0);\n"
            "#endif\n"
            "    void *parser = %sAlloc(malloc);\n"
            "    if (parser == NULL) return 2;\n"
            "    int errors = 0;\n"
            "    int yytok;\n"
            "    while ((yytok = yylex()) > 0) {\n"
            "        int lime = yy_xlat_token(yytok);\n"
            "        if (lime < 0) {\n"
            "            yyerror(\"syntax error: invalid token\");\n"
            "            errors = 1;\n"
            "            break;\n"
            "        }\n"
            "        %s(parser, lime, yylval);\n"
            "    }\n"
            "    /* EOF sentinel -- runs %%accept / %%syntax_error / etc. */\n"
            "    %s(parser, 0, (YYSTYPE){0});\n"
            "    %sFree(parser, free);\n"
            "    return errors;\n"
            "}\n",
            name, name, name, name, name, name);
    }

    /* Suppress unused warnings on the location global if locations
    ** are declared but Lime's parser does not yet thread them
    ** through.  (P0-NEW-2 plumbing is a future commit; for now the
    ** symbol is exported so callers can take its address but the
    ** skin does not write to it.) */
    if (has_locs) {
        fprintf(out, "\n/* yylloc currently exported but not threaded\n"
                    "** through Lime's parser; future commit will plumb\n"
                    "** it via ParseLoc(). */\n");
    }
}

/* ------------------------------------------------------------------ */
/*  Public entry point called from lime.c                              */
/* ------------------------------------------------------------------ */

/* Emit the bison-API-compatibility skin.  `out_h_path` and
** `out_c_path` are the destination paths (already prefixed with
** -d/--output-dir as appropriate by the caller).  `base_id` is the
** input grammar's basename without extension -- used in the include
** guard and the #include of the bison header.  Returns 0 on success,
** non-zero on I/O failure (an error message has already been
** written to stderr). */
int lime_emit_c_skin_bison(struct lime *lemp,
                           const char *out_h_path,
                           const char *out_c_path,
                           const char *base_id) {
    const char *parser_name = lime_emit_rust_get_name(lemp);

    FILE *fh = fopen(out_h_path, "wb");
    if (fh == NULL) {
        fprintf(stderr, "lime --target=c:bison: cannot write %s\n", out_h_path);
        return 1;
    }
    emit_header(fh, lemp, base_id, parser_name);
    if (ferror(fh)) {
        fprintf(stderr, "lime --target=c:bison: write error on %s\n", out_h_path);
        fclose(fh);
        return 1;
    }
    fclose(fh);

    FILE *fc = fopen(out_c_path, "wb");
    if (fc == NULL) {
        fprintf(stderr, "lime --target=c:bison: cannot write %s\n", out_c_path);
        return 1;
    }
    emit_source(fc, lemp, base_id, parser_name);
    if (ferror(fc)) {
        fprintf(stderr, "lime --target=c:bison: write error on %s\n", out_c_path);
        fclose(fc);
        return 1;
    }
    fclose(fc);
    return 0;
}
