/*
** src/lex/emit_c_skin_flex.c -- flex-API-compatibility skin for the
** Lime lexer output.
**
** When `lime -X --target=c:flex` is given, this module emits two
** extra files alongside the standard `<basename>_lex.c` /
** `<basename>_lex.h`:
**
**     <basename>_flex.h    -- flex-style public surface (yytext,
**                              yyleng, yylineno, yyin, yyout,
**                              YY_BUFFER_STATE, yylex, yywrap,
**                              yy_scan_string, etc.)
**     <basename>_flex.c    -- thin pull-driven adapter that drives
**                              Lime's longest-match function from
**                              yylex() and presents the flex API to
**                              callers
**
** The skin does not modify or replace the standard Lime lexer
** output; both files are written next to the standard ones.  A
** consumer can compile `<basename>_flex.c` together with
** `<basename>_lex.c` to get a flex-API-compatible `int yylex(void)`
** over Lime's DFA tables.
**
** Design summary
** --------------
**
**   * Pull-driven yylex() built on Lime's `<Prefix>_match()`
**     longest-match primitive.  Each yylex() call:
**
**       1. Skips past the previous match
**       2. Calls <Prefix>_match(current_state, ptr+pos, len-pos,
**                               &rule_id, &consumed)
**       3. Sets yytext / yyleng to the matched bytes
**       4. Updates yylineno by counting '\n' in the match
**       5. Advances pos by `consumed`
**       6. Returns rule_id + 1 (so 0 stays reserved for EOF)
**
**   * Buffers: a tiny `yy_buffer_state` tracks `bytes`, `len`,
**     `pos`, and an `owns` flag (whether the skin should free
**     `bytes` on yy_delete_buffer).  yy_scan_string copies; yy_-
**     scan_buffer adopts; yy_create_buffer slurps a FILE * into a
**     fresh allocation.
**
**   * Start-condition state: `yy_set_state(state)` writes a
**     module-static int that <Prefix>_match() consults on the next
**     call.  The flex `BEGIN(state)` macro expands to
**     `yy_set_state(state)`.  State constants come from the
**     standard `<basename>_lex.h` (`<PREFIX>_STATE_*`).
**
**   * yywrap(): emitted as a weak default that returns 1 (no more
**     buffers; stop).  Consumers can override by linking a strong
**     definition.  yylex() consults yywrap() when the current
**     buffer is exhausted; if yywrap returns 0 and the user pushed
**     a new buffer via yy_switch_to_buffer, yylex resumes; if
**     yywrap returns 1, yylex returns 0 (EOF).
**
** What is NOT supported
** ---------------------
**
**   * `ECHO`, `REJECT`, `yymore`, `yyless` -- flex action-side
**     macros.  The skin does not honour Lime action bodies
**     (LEX_SKIP, LEX_EMIT, LEX_TRANSITION, LEX_TERMINATE,
**     LEX_ERROR_AT) because yylex() bypasses LexFeedBytes() and
**     calls the lower-level <Prefix>_match() directly.  Consumers
**     that need action-body semantics should drive Lime's standard
**     LexFeedBytes runtime instead.
**
**   * <<EOF>> rules.  Lime supports `%eof` rules but the flex
**     syntax differs.  Workaround: the consumer's yywrap() returns
**     non-zero to signal end-of-input.
**
**   * Multiple buffers / yy_push_state / yy_pop_state.  Basic
**     single-buffer support only for v0.9.3.  yy_switch_to_buffer
**     swaps; yy_scan_string allocates and switches in one step.
**
**   * %lexer_extra_argument grammars.  The flex skin refuses to
**     emit when the grammar declares one, because there is no
**     flex-API equivalent for the threaded parameter.
*/

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_emit.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CLI gate: lime.c sets this to 1 when --target=c:flex is given.
** Defined here as weak (gcc/clang) or selectany (MSVC) so the
** standalone single-file `cc -o lime lime.c` build can also define
** it without producing a duplicate-symbol error.  src/lex/lex_main.c
** consults this global to decide whether to invoke the skin emit
** after the standard lexer output is written. */
/* Defined in lime.c as the single source of truth.  Test
** programs that link liblime_lex_compiler.a directly (without
** lime.c) would need to provide their own definition. */
extern int g_lime_skin_flex_flag;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Copy `s` into `out` (size `n` bytes), uppercasing letters and
** replacing non-alnum with '_'.  Used to derive include guards and
** rule-name identifiers from the lexer base id / rule names. */
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

/* Effective name prefix for the lexer.  Mirrors lex_emit.c's
** eff_prefix(): NULL or empty falls back to "Lex". */
static const char *eff_prefix(const char *name_prefix) {
    if (name_prefix == NULL || name_prefix[0] == 0) return "Lex";
    return name_prefix;
}

/* ------------------------------------------------------------------ */
/*  Header emission: <base_id>_flex.h                                  */
/* ------------------------------------------------------------------ */

static void emit_header(FILE *out, const char *base_id, const char *prefix,
                        const char *const *rule_names, int n_rules, int has_classes) {
    char guard[256];
    uppercase_id(guard, sizeof guard, base_id);
    size_t gl = strlen(guard);
    snprintf(guard + gl, sizeof guard - gl, "_FLEX_H");

    char PREFIX[128];
    uppercase_id(PREFIX, sizeof PREFIX, prefix);

    fprintf(out,
        "/* Generated by Lime %s skin (--target=c:flex) -- do not edit. */\n"
        "/* flex-API-compatible surface over the Lime lexer.            */\n"
        "/*                                                              */\n"
        "/* See docs/SKINS.md for the full surface, supported features, */\n"
        "/* and worked examples.                                         */\n"
        "\n"
        "#ifndef %s\n"
        "#define %s\n"
        "\n"
        "#include <stddef.h>\n"
        "#include <stdio.h>\n"
        "\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "\n",
        prefix, guard, guard);

    /* flex globals. */
    fprintf(out,
        "/* flex API surface.  yytext / yyleng point into the current   */\n"
        "/* buffer at the start of the most recent match; yylineno is   */\n"
        "/* incremented for each '\\n' consumed by a match.  yyin /     */\n"
        "/* yyout exist for source compatibility; the skin does not     */\n"
        "/* implicitly read yyin (use yy_create_buffer(yyin, 0)).       */\n"
        "extern char *yytext;\n"
        "extern int   yyleng;\n"
        "extern int   yylineno;\n"
        "extern FILE *yyin;\n"
        "extern FILE *yyout;\n"
        "\n");

    /* Buffer state. */
    fprintf(out,
        "/* Opaque buffer-state handle.  Created by yy_scan_string,     */\n"
        "/* yy_scan_buffer, or yy_create_buffer; destroyed with         */\n"
        "/* yy_delete_buffer; activated with yy_switch_to_buffer.       */\n"
        "typedef struct yy_buffer_state *YY_BUFFER_STATE;\n"
        "\n");

    /* Rule code enum: parallels the standard <PREFIX>_RULE_<NAME>
    ** values shifted by +1, so 0 stays reserved for EOF.  When the
    ** consumer pairs --target=c:bison,flex they can wire the
    ** matching parser-token enum (258 + i) on top, since rule names
    ** typically match parser token names by convention. */
    if (n_rules > 0) {
        fprintf(out,
            "/* Rule-code constants returned by yylex().  yylex() returns  */\n"
            "/* 0 at EOF and (rule_id + 1) for every successful match;     */\n"
            "/* the +1 keeps 0 reserved for EOF in the flex contract.      */\n"
            "/* Negative return values signal a no-match scan; the skin    */\n"
            "/* advances one byte and reports YY_FLEX_INVALID.             */\n"
            "enum {\n"
            "    YY_FLEX_EOF     = 0,\n"
            "    YY_FLEX_INVALID = -1");
        for (int i = 0; i < n_rules; i++) {
            char upper[128];
            uppercase_id(upper, sizeof upper, rule_names[i]);
            fprintf(out, ",\n    %s_FLEX_%s = %d", PREFIX, upper, i + 1);
        }
        fprintf(out, "\n};\n\n");
    }

    /* yylex / yywrap. */
    fprintf(out,
        "/* Standard flex entry point.  Tokenises one match against the */\n"
        "/* current buffer at the current position and returns:         */\n"
        "/*    0  -- end of buffer (yywrap() returned 1)                */\n"
        "/*   >0  -- (rule_id + 1) for the matched rule                 */\n"
        "/*   -1  -- no rule matched at the cursor; one byte was        */\n"
        "/*           skipped to avoid an infinite loop                 */\n"
        "int yylex(void);\n"
        "\n"
        "/* End-of-buffer hook.  Default implementation returns 1       */\n"
        "/* (no more input; stop).  Override by linking a strong        */\n"
        "/* definition that loads the next buffer with yy_switch_to_-   */\n"
        "/* buffer and returns 0 to keep tokenising.                    */\n"
        "int yywrap(void);\n"
        "\n");

    /* Buffer manipulation. */
    fprintf(out,
        "/* Create a buffer that scans the NUL-terminated string `str`. */\n"
        "/* The skin copies the string; the caller may free `str`       */\n"
        "/* immediately on return.  Switches to the new buffer.         */\n"
        "YY_BUFFER_STATE yy_scan_string(const char *str);\n"
        "\n"
        "/* Adopt a caller-owned buffer.  The buffer must be at least 2 */\n"
        "/* bytes long and end with two NUL sentinels (flex contract);  */\n"
        "/* `size` includes those two sentinels.  The skin does NOT     */\n"
        "/* free the buffer on yy_delete_buffer.  Switches.             */\n"
        "YY_BUFFER_STATE yy_scan_buffer(char *base, size_t size);\n"
        "\n"
        "/* Slurp the entire FILE * into a fresh allocation owned by    */\n"
        "/* the buffer state.  `size` is advisory and ignored (we read  */\n"
        "/* to EOF).  Does NOT switch -- call yy_switch_to_buffer.      */\n"
        "YY_BUFFER_STATE yy_create_buffer(FILE *file, int size);\n"
        "\n"
        "/* Free a buffer state.  If the buffer owns its bytes (created */\n"
        "/* by yy_scan_string or yy_create_buffer) the bytes are freed  */\n"
        "/* too; yy_scan_buffer-created states free only the wrapper.   */\n"
        "/* If `b` is the active buffer, the active pointer is cleared. */\n"
        "void yy_delete_buffer(YY_BUFFER_STATE b);\n"
        "\n"
        "/* Activate `b` as the buffer yylex() reads from.              */\n"
        "void yy_switch_to_buffer(YY_BUFFER_STATE b);\n"
        "\n");

    /* Start-condition state. */
    fprintf(out,
        "/* Set the lexer's start state for the next yylex() call.      */\n"
        "/* `state` is one of the %s_STATE_* constants emitted in       */\n"
        "/* %s_lex.h.  The flex BEGIN(state) macro expands to           */\n"
        "/* yy_set_state(state).                                         */\n"
        "void yy_set_state(int state);\n"
        "#ifndef BEGIN\n"
        "# define BEGIN(state) yy_set_state(state)\n"
        "#endif\n"
        "\n",
        PREFIX, base_id);

    /* Runtime classification (%keyword_class / %operator_class): only
    ** for a spec that declares a class, so other output is unchanged.
    ** The function type matches <P>ClassifyFn in <base_id>_lex.h. */
    if (has_classes) {
        fprintf(out,
            "/* Install the runtime classifier for %%keyword_class /           */\n"
            "/* %%operator_class rules (e.g. lime_lexicon_classify over a       */\n"
            "/* snapshot lexicon).  On a claim yylex() returns the classifier's */\n"
            "/* code with yytext/yyleng over the claimed prefix; fn == NULL     */\n"
            "/* restores the plain (rule_id + 1) return.                        */\n"
            "void yy_set_classifier(int (*fn)(const void *cls, int kind,\n"
            "                                 const char *text, size_t len,\n"
            "                                 size_t *out_len),\n"
            "                       const void *cls);\n"
            "\n");
    }

    fprintf(out,
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n"
        "\n"
        "#endif /* %s */\n",
        guard);
}

/* ------------------------------------------------------------------ */
/*  Source emission: <base_id>_flex.c                                  */
/* ------------------------------------------------------------------ */

static void emit_source(FILE *out, const char *base_id, const char *prefix, int has_classes) {
    fprintf(out,
        "/* Generated by Lime %s skin (--target=c:flex) -- do not edit. */\n"
        "/* flex-API-compatible adapter that drives Lime's lexer DFA   */\n"
        "/* via the standard <Prefix>_match() longest-match primitive. */\n"
        "/* Pair with %s_flex.h.                                        */\n"
        "\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#include \"%s_flex.h\"\n"
        "\n"
        "/* Forward decl of Lime's longest-match primitive.  We do NOT */\n"
        "/* #include <%s_lex.h> here because the skin's own enum       */\n"
        "/* (YY_FLEX_*) might collide with rule-id macros in some      */\n"
        "/* hand-rolled consumer headers; the prototype below is the   */\n"
        "/* only symbol the skin needs from the standard lexer.        */\n"
        "extern int %s_match(int state, const char *bytes, size_t n,\n"
        "                    int *out_rule, size_t *out_consumed);\n"
        "\n",
        prefix, base_id, base_id, base_id, prefix);
    if (has_classes) {
        fprintf(out,
            "/* Runtime classification: %s_classify is defined in %s_lex.c. */\n"
            "typedef int (*yy_classify_fn)(const void *cls, int kind,\n"
            "                              const char *text, size_t len,\n"
            "                              size_t *out_len);\n"
            "extern int %s_classify(yy_classify_fn fn, const void *cls, int rule,\n"
            "                       const char *text, size_t len, size_t *out_len);\n"
            "static yy_classify_fn yy_classifier     = NULL;\n"
            "static const void    *yy_classifier_cls = NULL;\n"
            "\n"
            "void yy_set_classifier(yy_classify_fn fn, const void *cls) {\n"
            "    yy_classifier     = fn;\n"
            "    yy_classifier_cls = cls;\n"
            "}\n"
            "\n",
            prefix, base_id, prefix);
    }

    /* Buffer state struct. */
    fprintf(out,
        "/* Opaque buffer-state struct.  `owns` flags whether the skin */\n"
        "/* must free `bytes` on yy_delete_buffer (yy_scan_string and  */\n"
        "/* yy_create_buffer set owns=1; yy_scan_buffer sets owns=0).  */\n"
        "struct yy_buffer_state {\n"
        "    char  *bytes;\n"
        "    size_t len;\n"
        "    size_t pos;\n"
        "    int    owns;\n"
        "};\n"
        "\n");

    /* Globals. */
    fprintf(out,
        "char *yytext   = NULL;\n"
        "int   yyleng   = 0;\n"
        "int   yylineno = 1;\n"
        "FILE *yyin     = NULL;\n"
        "FILE *yyout    = NULL;\n"
        "\n"
        "static YY_BUFFER_STATE yy_current_buffer = NULL;\n"
        "static int             yy_current_state  = 0;  /* INITIAL */\n"
        "\n");

    /* yywrap default.  Implemented as a weak symbol on gcc/clang so
    ** consumers can override by linking a strong definition; on
    ** other compilers we emit a plain definition (consumers can
    ** still override via their own object file at link time, in
    ** that case).  v0.9.3 keeps the override story simple. */
    fprintf(out,
        "/* Default yywrap: returns 1, signalling no more input.       */\n"
        "/* Override by defining YY_USER_YYWRAP before including this  */\n"
        "/* TU; consumer then provides their own strong yywrap().      */\n"
        "#ifndef YY_USER_YYWRAP\n"
        "# if defined(__GNUC__) || defined(__clang__)\n"
        "__attribute__((weak))\n"
        "# endif\n"
        "int yywrap(void) { return 1; }\n"
        "#endif\n"
        "\n");

    /* yy_set_state. */
    fprintf(out,
        "void yy_set_state(int state) {\n"
        "    yy_current_state = state;\n"
        "}\n"
        "\n");

    /* yy_switch_to_buffer. */
    fprintf(out,
        "void yy_switch_to_buffer(YY_BUFFER_STATE b) {\n"
        "    yy_current_buffer = b;\n"
        "}\n"
        "\n");

    /* yy_scan_string. */
    fprintf(out,
        "YY_BUFFER_STATE yy_scan_string(const char *str) {\n"
        "    if (str == NULL) return NULL;\n"
        "    size_t len = strlen(str);\n"
        "    /* +2 trailing NULs match flex's two-byte EOB sentinel.   */\n"
        "    char *copy = (char *)malloc(len + 2);\n"
        "    if (copy == NULL) return NULL;\n"
        "    memcpy(copy, str, len);\n"
        "    copy[len]     = 0;\n"
        "    copy[len + 1] = 0;\n"
        "    YY_BUFFER_STATE b = (YY_BUFFER_STATE)calloc(1, sizeof(*b));\n"
        "    if (b == NULL) { free(copy); return NULL; }\n"
        "    b->bytes = copy;\n"
        "    b->len   = len;\n"
        "    b->pos   = 0;\n"
        "    b->owns  = 1;\n"
        "    yy_switch_to_buffer(b);\n"
        "    return b;\n"
        "}\n"
        "\n");

    /* yy_scan_buffer. */
    fprintf(out,
        "YY_BUFFER_STATE yy_scan_buffer(char *base, size_t size) {\n"
        "    if (base == NULL || size < 2) return NULL;\n"
        "    /* flex contract: the trailing two bytes are NUL          */\n"
        "    /* sentinels owned by the caller; we verify cheaply.      */\n"
        "    if (base[size - 1] != 0 || base[size - 2] != 0) return NULL;\n"
        "    YY_BUFFER_STATE b = (YY_BUFFER_STATE)calloc(1, sizeof(*b));\n"
        "    if (b == NULL) return NULL;\n"
        "    b->bytes = base;\n"
        "    b->len   = size - 2;\n"
        "    b->pos   = 0;\n"
        "    b->owns  = 0;\n"
        "    yy_switch_to_buffer(b);\n"
        "    return b;\n"
        "}\n"
        "\n");

    /* yy_create_buffer. */
    fprintf(out,
        "YY_BUFFER_STATE yy_create_buffer(FILE *file, int size) {\n"
        "    (void)size;  /* advisory; we read to EOF */\n"
        "    if (file == NULL) return NULL;\n"
        "    if (fseek(file, 0, SEEK_END) != 0) return NULL;\n"
        "    long sz = ftell(file);\n"
        "    if (sz < 0) return NULL;\n"
        "    rewind(file);\n"
        "    char *buf = (char *)malloc((size_t)sz + 2);\n"
        "    if (buf == NULL) return NULL;\n"
        "    size_t got = (sz > 0) ? fread(buf, 1, (size_t)sz, file) : 0;\n"
        "    if (got != (size_t)sz) { free(buf); return NULL; }\n"
        "    buf[sz]     = 0;\n"
        "    buf[sz + 1] = 0;\n"
        "    YY_BUFFER_STATE b = (YY_BUFFER_STATE)calloc(1, sizeof(*b));\n"
        "    if (b == NULL) { free(buf); return NULL; }\n"
        "    b->bytes = buf;\n"
        "    b->len   = (size_t)sz;\n"
        "    b->pos   = 0;\n"
        "    b->owns  = 1;\n"
        "    return b;  /* caller chooses when to switch */\n"
        "}\n"
        "\n");

    /* yy_delete_buffer. */
    fprintf(out,
        "void yy_delete_buffer(YY_BUFFER_STATE b) {\n"
        "    if (b == NULL) return;\n"
        "    if (b == yy_current_buffer) yy_current_buffer = NULL;\n"
        "    if (b->owns) free(b->bytes);\n"
        "    free(b);\n"
        "}\n"
        "\n");

    /* yylex. */
    fprintf(out,
        "int yylex(void) {\n"
        "    for (;;) {\n"
        "        if (yy_current_buffer == NULL) return YY_FLEX_EOF;\n"
        "        YY_BUFFER_STATE b = yy_current_buffer;\n"
        "        if (b->pos >= b->len) {\n"
        "            /* End of current buffer.  Ask user to load next.  */\n"
        "            int wrap = yywrap();\n"
        "            if (wrap == 0 && yy_current_buffer != NULL\n"
        "                && yy_current_buffer != b) {\n"
        "                /* User installed a fresh buffer; resume.      */\n"
        "                continue;\n"
        "            }\n"
        "            return YY_FLEX_EOF;\n"
        "        }\n"
        "        int rule = -1;\n"
        "        size_t consumed = 0;\n"
        "        int ok = %s_match(yy_current_state,\n"
        "                          b->bytes + b->pos,\n"
        "                          b->len   - b->pos,\n"
        "                          &rule, &consumed);\n"
        "        if (!ok || consumed == 0) {\n"
        "            /* No rule matched.  Skip one byte to avoid an    */\n"
        "            /* infinite loop and report YY_FLEX_INVALID; the  */\n"
        "            /* caller can decide whether to abort or recover. */\n"
        "            yytext = b->bytes + b->pos;\n"
        "            yyleng = 1;\n"
        "            if (b->bytes[b->pos] == '\\n') yylineno++;\n"
        "            b->pos++;\n"
        "            return YY_FLEX_INVALID;\n"
        "        }\n"
        "        yytext = b->bytes + b->pos;\n",
        prefix);
    if (has_classes) {
        /* A class rule's match is offered to the classifier first; a
        ** claim covers its first n bytes, the rest is scanned next. */
        fprintf(out,
        "        if (yy_classifier != NULL) {\n"
        "            size_t n = 0;\n"
        "            int code = %s_classify(yy_classifier, yy_classifier_cls, rule,\n"
        "                                   yytext, consumed, &n);\n"
        "            if (code >= 0) {\n"
        "                yyleng = (int)n;\n"
        "                for (size_t i = 0; i < n; i++) {\n"
        "                    if (yytext[i] == '\\n') yylineno++;\n"
        "                }\n"
        "                b->pos += n;\n"
        "                return code;\n"
        "            }\n"
        "        }\n",
        prefix);
    }
    fprintf(out,
        "        yyleng = (int)consumed;\n"
        "        for (size_t i = 0; i < consumed; i++) {\n"
        "            if (yytext[i] == '\\n') yylineno++;\n"
        "        }\n"
        "        b->pos += consumed;\n"
        "        return rule + 1;  /* +1 keeps 0 reserved for EOF */\n"
        "    }\n"
        "}\n");
}

/* ------------------------------------------------------------------ */
/*  Public entry point called from src/lex/lex_main.c                  */
/* ------------------------------------------------------------------ */

/* Emit the flex-API-compatibility skin.  `out_h_path` and
** `out_c_path` are the destination paths.  `base_id` is the
** input grammar's basename without extension; used in the
** include guard and the cross-file include.  `name_prefix` is
** the lexer's %name_prefix (or "Lex" if absent).  Returns 0 on
** success, non-zero on I/O / refusal (an error message has been
** written to stderr).
*/
int lime_emit_c_skin_flex(const LimeLexCompiled *compiled,
                          const LimeLexSpec *spec,
                          const char *name_prefix,
                          const char *const *rule_names,
                          int n_rules,
                          const char *out_h_path,
                          const char *out_c_path,
                          const char *base_id) {
    (void)compiled;
    if (spec == NULL || out_h_path == NULL || out_c_path == NULL
        || base_id == NULL) {
        fprintf(stderr,
                "lime --target=c:flex: internal error -- NULL argument\n");
        return 1;
    }

    /* Refuse %lexer_extra_argument grammars.  The flex API has no
    ** equivalent for the threaded parameter; emitting a stub that
    ** silently passes a zero would surprise users.  See
    ** docs/SKINS.md for the rationale. */
    if (spec->extra_argument != NULL && spec->extra_argument[0] != 0) {
        fprintf(stderr,
                "lime --target=c:flex: grammar uses %%lexer_extra_argument; "
                "the flex skin has no equivalent.  Drop the directive or "
                "use the standard Lime LexFeedBytes runtime instead.\n");
        return 1;
    }

    const char *prefix = eff_prefix(name_prefix);

    FILE *fh = fopen(out_h_path, "wb");
    if (fh == NULL) {
        fprintf(stderr,
                "lime --target=c:flex: cannot write %s\n", out_h_path);
        return 1;
    }
    const int has_classes = lime_lex_has_classes(spec);
    emit_header(fh, base_id, prefix, rule_names, n_rules, has_classes);
    int herr = ferror(fh);
    fclose(fh);
    if (herr) {
        fprintf(stderr,
                "lime --target=c:flex: write error on %s\n", out_h_path);
        return 1;
    }

    FILE *fc = fopen(out_c_path, "wb");
    if (fc == NULL) {
        fprintf(stderr,
                "lime --target=c:flex: cannot write %s\n", out_c_path);
        return 1;
    }
    emit_source(fc, base_id, prefix, has_classes);
    int cerr = ferror(fc);
    fclose(fc);
    if (cerr) {
        fprintf(stderr,
                "lime --target=c:flex: write error on %s\n", out_c_path);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Flex mode: a complete lex.yy.c from a .l source (Track E)         */
/* ------------------------------------------------------------------ */

#include "flex_reader.h"

/* The runtime skeleton: one string per line, with %%directives
** (%%if NAME / %%else / %%endif, and named insertion points). */
static const char *const flex_skel[] = {
#include "flex_skel.inc"
};

/* Symbols %option prefix renames (flex's list, minus the C++ ones). */
static const char *const flex_prefixed[] = {
    "_create_buffer", "_delete_buffer", "_scan_buffer", "_scan_string", "_scan_bytes",
    "_init_buffer", "_flush_buffer", "_load_buffer_state", "_switch_to_buffer",
    "push_buffer_state", "pop_buffer_state", "ensure_buffer_stack", "lex", "restart",
    "lex_init", "lex_init_extra", "lex_destroy", "get_debug", "set_debug", "get_extra",
    "set_extra", "get_in", "set_in", "get_out", "set_out", "get_leng", "get_text",
    "get_lineno", "set_lineno", "get_column", "set_column", "wrap", "get_lval", "set_lval",
    "get_lloc", "set_lloc", "alloc", "realloc", "free", "text", "leng", "in", "out",
    "_flex_debug", "lineno",
};

/* REJECT: one DFA per Lime state whose accepting states keep EVERY rule
** that matches there, not just the first.  accept_rule is a set id. */
typedef struct {
    LimeDfa *dfa;
    int *set_off;   /* set id -> first entry in set_rules; n_sets + 1 entries */
    int *set_rules; /* spec rule indices, ascending within each set */
    int n_sets;
} FeProduct;

typedef struct {
    LimeFlexSpec *fs;
    FILE *out;
    const char *out_name; /* for #line */
    int line;             /* current output line (1-based) */
    int noline;
    LimeLexCompiled *lc;
    const LimeDfa **dfas; /* per Lime state: lc's DFA, or the REJECT product's */
    FeProduct *prod;      /* per Lime state, REJECT scanners only; else NULL */
} FlexEmit;

static void fe_puts(FlexEmit *e, const char *s) {
    for (const char *p = s; *p; p++)
        if (*p == '\n') e->line++;
    fputs(s, e->out);
}

static void fe_printf(FlexEmit *e, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < sizeof buf) {
        fe_puts(e, buf);
        return;
    }
    char *big = malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    fe_puts(e, big);
    free(big);
}

/* #line pointing into the .l file, or back into the output. */
static void fe_line_src(FlexEmit *e, int line) {
    if (e->noline) return;
    fe_printf(e, "#line %d \"%s\"\n", line, e->fs->filename ? e->fs->filename : "<stdin>");
}
static void fe_line_out(FlexEmit *e) {
    if (e->noline) return;
    fe_printf(e, "#line %d \"%s\"\n", e->line + 2, e->out_name);
}

static void fe_code(FlexEmit *e, const LimeFlexCode *c, int n) {
    for (int k = 0; k < n; k++) {
        if (!c[k].text) continue;
        fe_line_src(e, c[k].line);
        fe_puts(e, c[k].text);
        size_t L = strlen(c[k].text);
        if (L == 0 || c[k].text[L - 1] != '\n') fe_puts(e, "\n");
    }
    if (n > 0) fe_line_out(e);
}

static int flex_opt(int v, int dflt) { return v < 0 ? dflt : v; }

/* Value of a %%if condition. */
static int fe_cond(FlexEmit *e, const char *name) {
    const LimeFlexSpec *fs = e->fs;
    const LimeFlexOptions *o = &fs->opt;
    int neg = name[0] == '!';
    if (neg) name++;
    int v = 0;
    if (!strcmp(name, "reentrant")) v = o->reentrant == 1;
    else if (!strcmp(name, "bridge")) v = o->bison_bridge == 1;
    else if (!strcmp(name, "locations")) v = o->bison_locations == 1;
    else if (!strcmp(name, "yylineno")) v = o->yylineno == 1;
    else if (!strcmp(name, "yymore")) v = fs->uses_yymore > 0;
    else if (!strcmp(name, "reject")) v = fs->uses_reject > 0;
    else if (!strcmp(name, "bol")) v = fs->bol_needed;
    else if (!strcmp(name, "trail")) v = fs->has_trail;
    else if (!strcmp(name, "stack")) v = o->stack == 1;
    else if (!strcmp(name, "debug")) v = o->debug == 1;
    else if (!strcmp(name, "unistd")) v = flex_opt(o->unistd, 1);
    else if (!strcmp(name, "yyalloc")) v = flex_opt(o->yyalloc, 1);
    else if (!strcmp(name, "yyrealloc")) v = flex_opt(o->yyrealloc, 1);
    else if (!strcmp(name, "yyfree")) v = flex_opt(o->yyfree, 1);
    else fprintf(stderr, "lime: internal error: unknown skeleton condition %s\n", name);
    return neg ? !v : v;
}

static void fe_prefix_defines(FlexEmit *e) {
    const char *P = e->fs->opt.prefix;
    if (!P || !strcmp(P, "yy")) return;
    size_t n = sizeof flex_prefixed / sizeof *flex_prefixed;
    if (e->fs->opt.reentrant == 1) n -= 6; /* the globals: they live in yyguts_t */
    for (size_t k = 0; k < n; k++)
        fe_printf(e, "#define yy%s %s%s\n", flex_prefixed[k], P, flex_prefixed[k]);
    fe_puts(e, "\n");
}

/* Interning of int vectors (DFA state tuples, accept sets) to dense ids. */
typedef struct {
    int *pool;
    size_t pool_n, pool_cap;
    size_t *off;
    int *len;
    int n, cap;
    int *tab; /* open addressing: id + 1, 0 = empty */
    int tab_cap;
} FeIntern;

static unsigned fe_hash(const int *v, int n) {
    unsigned h = 2166136261u;
    for (int k = 0; k < n; k++) h = (h ^ (unsigned)v[k]) * 16777619u;
    return h ^ (unsigned)n;
}

static int fe_intern_grow(FeIntern *in) {
    int cap = in->tab_cap ? in->tab_cap * 2 : 1024;
    int *tab = calloc((size_t)cap, sizeof(int));
    if (!tab) return -1;
    for (int id = 0; id < in->n; id++) {
        unsigned h = fe_hash(in->pool + in->off[id], in->len[id]) & (unsigned)(cap - 1);
        while (tab[h]) h = (h + 1) & (unsigned)(cap - 1);
        tab[h] = id + 1;
    }
    free(in->tab);
    in->tab = tab;
    in->tab_cap = cap;
    return 0;
}

/* The id of v[0..n), adding it if new.  -1 = out of memory. */
static int fe_intern(FeIntern *in, const int *v, int n) {
    if (2 * (in->n + 1) > in->tab_cap && fe_intern_grow(in) < 0) return -1;
    unsigned h = fe_hash(v, n) & (unsigned)(in->tab_cap - 1);
    for (; in->tab[h]; h = (h + 1) & (unsigned)(in->tab_cap - 1)) {
        int id = in->tab[h] - 1;
        if (in->len[id] == n && !memcmp(in->pool + in->off[id], v, (size_t)n * sizeof(int))) return id;
    }
    if (in->n == in->cap) {
        int cap = in->cap ? in->cap * 2 : 256;
        size_t *off = realloc(in->off, (size_t)cap * sizeof(size_t));
        if (!off) return -1;
        in->off = off;
        int *len = realloc(in->len, (size_t)cap * sizeof(int));
        if (!len) return -1;
        in->len = len;
        in->cap = cap;
    }
    if (in->pool_n + (size_t)n > in->pool_cap) {
        size_t cap = in->pool_cap ? in->pool_cap * 2 : 4096;
        while (cap < in->pool_n + (size_t)n) cap *= 2;
        int *pool = realloc(in->pool, cap * sizeof(int));
        if (!pool) return -1;
        in->pool = pool;
        in->pool_cap = cap;
    }
    if (n > 0) memcpy(in->pool + in->pool_n, v, (size_t)n * sizeof(int));
    in->off[in->n] = in->pool_n;
    in->len[in->n] = n;
    in->pool_n += (size_t)n;
    in->tab[h] = in->n + 1;
    return in->n++;
}

static void fe_intern_free(FeIntern *in) {
    free(in->pool);
    free(in->off);
    free(in->len);
    free(in->tab);
}

/* shortcut: the product is built over all 256 bytes per state; fine for
** REJECT scanners (rare, small), revisit if one needs >100k states. */
#define FE_PRODUCT_MAX 100000

/* The product of a Lime state's per-rule DFAs: a state is the tuple of
** per-rule states, and accepts the set of rules whose component accepts.
** Minimised with the set id as accept_rule.  0 ok, -1 OOM, 1 too big. */
static int fe_product(const LimeLexCompiledState *cs, FeProduct *out) {
    memset(out, 0, sizeof *out);
    int m = cs->n_per_rule_dfas, nc = 0, rc = -1;
    if (!cs->per_rule_dfas || m <= 0) return 0;
    int *comp = malloc((size_t)m * sizeof(int));
    int *cur = malloc((size_t)m * sizeof(int));
    int *nxt = malloc((size_t)m * sizeof(int));
    FeIntern T, S;
    memset(&T, 0, sizeof T);
    memset(&S, 0, sizeof S);
    LimeDfa raw;
    memset(&raw, 0, sizeof raw);
    if (!comp || !cur || !nxt) goto done;
    for (int i = 0; i < m; i++)
        if (cs->per_rule_dfas[i]) comp[nc++] = i;
    if (nc == 0) {
        rc = 0;
        goto done;
    }
    for (int j = 0; j < nc; j++) cur[j] = cs->per_rule_dfas[comp[j]]->start;
    if (fe_intern(&T, cur, nc) < 0) goto done;
    for (int q = 0; q < T.n; q++) {
        if (T.n > FE_PRODUCT_MAX) {
            rc = 1;
            goto done;
        }
        if (raw.n_states == raw.cap) {
            int cap = raw.cap ? raw.cap * 2 : 64;
            LimeDfaState *st = realloc(raw.states, (size_t)cap * sizeof *st);
            if (!st) goto done;
            raw.states = st;
            raw.cap = cap;
        }
        LimeDfaState *st = &raw.states[raw.n_states++];
        memset(st, 0, sizeof *st);
        st->id = q;
        memcpy(cur, T.pool + T.off[q], (size_t)nc * sizeof(int));
        int na = 0;
        for (int j = 0; j < nc; j++)
            if (cur[j] >= 0 && cs->per_rule_dfas[comp[j]]->states[cur[j]].is_accept)
                nxt[na++] = cs->rule_indices[comp[j]];
        if (na > 0) {
            int sid = fe_intern(&S, nxt, na);
            if (sid < 0) goto done;
            st = &raw.states[q];
            st->is_accept = 1;
            st->accept_rule = sid;
        }
        for (int b = 0; b < 256; b++) {
            int any = 0;
            for (int j = 0; j < nc; j++) {
                nxt[j] = cur[j] < 0 ? -1 : cs->per_rule_dfas[comp[j]]->states[cur[j]].trans[b];
                any |= nxt[j] >= 0;
            }
            int t = any ? fe_intern(&T, nxt, nc) : -1;
            if (any && t < 0) goto done;
            raw.states[q].trans[b] = t;
        }
    }
    raw.start = 0;
    out->dfa = lime_lex_dfa_minimize(&raw);
    if (!out->dfa) goto done;
    out->n_sets = S.n;
    out->set_off = malloc((size_t)(S.n + 1) * sizeof(int));
    out->set_rules = malloc((S.pool_n ? S.pool_n : 1) * sizeof(int));
    if (!out->set_off || !out->set_rules) goto done;
    for (int s = 0; s < S.n; s++) out->set_off[s] = (int)S.off[s];
    out->set_off[S.n] = (int)S.pool_n;
    if (S.pool_n) memcpy(out->set_rules, S.pool, S.pool_n * sizeof(int));
    rc = 0;
done:
    if (rc != 0) {
        lime_lex_dfa_free(out->dfa);
        free(out->set_off);
        free(out->set_rules);
        memset(out, 0, sizeof *out);
    }
    free(raw.states);
    fe_intern_free(&T);
    fe_intern_free(&S);
    free(comp);
    free(cur);
    free(nxt);
    return rc;
}

/* Equivalence classes over the 256 bytes, merged across every DFA. */
static int fe_classes(const FlexEmit *e, int ec[256]) {
    const LimeLexCompiled *lc = e->lc;
    /* Two bytes share a class iff every state of every DFA sends them to
    ** the same place.  Refine by hashing column signatures. */
    int n_ec = 0, rep[256];
    for (int b = 0; b < 256; b++) {
        ec[b] = -1;
        for (int k = 0; k < n_ec && ec[b] < 0; k++) {
            int c = rep[k];
            int same = (b == 0) == (c == 0); /* NUL stays alone: it doubles as end-of-buffer */
            for (int s = 0; same && s < lc->n_states; s++) {
                const LimeDfa *d = e->dfas[s];
                if (!d) continue;
                for (int q = 0; q < d->n_states && same; q++)
                    if (d->states[q].trans[b] != d->states[q].trans[c]) same = 0;
            }
            if (same) ec[b] = k;
        }
        if (ec[b] < 0) {
            rep[n_ec] = b;
            ec[b] = n_ec++;
        }
    }
    return n_ec;
}

static int fe_has_out(const LimeDfaState *st) {
    for (int b = 0; b < 256; b++)
        if (st->trans[b] >= 0) return 1;
    return 0;
}

/* Emit yy_ec, yy_nxt, yy_accept, yy_nul_trans, yy_start_tab and the
** per-rule tables.  All Lime states' DFAs go into one table, offset per
** state.  NUL (class 0) transitions are routed through yy_nul_trans so
** the scanner can tell a real NUL from the end of the buffer. */
static int fe_tables(FlexEmit *e) {
    const LimeFlexSpec *fs = e->fs;
    const LimeLexCompiled *lc = e->lc;
    int ec[256];
    int n_ec = fe_classes(e, ec);
    int *base = calloc((size_t)lc->n_states + 1, sizeof(int));
    if (!base) return -1;
    int total = 1; /* state 0 = jam (no transitions, not accepting) */
    for (int s = 0; s < lc->n_states; s++) {
        base[s] = total;
        if (e->dfas[s]) total += e->dfas[s]->n_states;
    }

    fe_printf(e, "#define YY_NUM_RULES %d\n", fs->n_rules);
    fe_printf(e, "#define YY_END_OF_BUFFER %d\n", fs->n_rules + 1);
    fe_printf(e, "#define YY_NEC %d\n", n_ec);
    fe_puts(e, "static const flex_uint8_t yy_ec[256] = {");
    for (int b = 0; b < 256; b++)
        fe_printf(e, "%s%d,", b % 16 ? " " : "\n\t", ec[b]);
    fe_puts(e, "\n};\n");

    /* Per class, the first byte (to read the class's column). */
    int rep[256];
    for (int c = 0; c < n_ec; c++) rep[c] = -1;
    for (int b = 255; b >= 0; b--) rep[ec[b]] = b;

    const char *ty = total < 32767 ? "flex_int16_t" : "flex_int32_t";
    fe_printf(e, "static const %s yy_nxt[%d] = {", ty, total * n_ec);
    int col = 0;
    for (int c = 0; c < n_ec; c++)
        fe_printf(e, "%s-1,", col++ % 16 ? " " : "\n\t");
    for (int s = 0; s < lc->n_states; s++) {
        const LimeDfa *d = e->dfas[s];
        if (!d) continue;
        for (int q = 0; q < d->n_states; q++) {
            for (int c = 0; c < n_ec; c++) {
                int t = d->states[q].trans[rep[c]];
                /* -2 = look at yy_nul_trans (NUL byte or end of buffer). */
                int v = c == ec[0] ? -2 : (t < 0 ? -1 : base[s] + t);
                fe_printf(e, "%s%d,", col++ % 16 ? " " : "\n\t", v);
            }
        }
    }
    fe_puts(e, "\n};\n");

    /* yy_nul_trans: the real NUL transition; yy_accept: rule + 1, negated
    ** when the state has no way out. */
    fe_printf(e, "static const %s yy_nul_trans[%d] = {\n\t-1,", ty, total);
    col = 1;
    for (int s = 0; s < lc->n_states; s++) {
        const LimeDfa *d = e->dfas[s];
        if (!d) continue;
        for (int q = 0; q < d->n_states; q++) {
            int t = d->states[q].trans[0];
            fe_printf(e, "%s%d,", col++ % 16 ? " " : "\n\t", t < 0 ? -1 : base[s] + t);
        }
    }
    fe_puts(e, "\n};\n");
    fe_printf(e, "static const %s yy_accept[%d] = {\n\t0,", fs->n_rules < 32767 ? "flex_int16_t" : "flex_int32_t", total);
    col = 1;
    for (int s = 0; s < lc->n_states; s++) {
        const LimeLexCompiledState *cs = &lc->states[s];
        const LimeDfa *d = e->dfas[s];
        if (!d) continue;
        for (int q = 0; q < d->n_states; q++) {
            int v = 0;
            if (d->states[q].is_accept) {
                /* REJECT scanners: 1 = accepting, the rules are in yy_acclist. */
                v = e->prod ? 1 : cs->rule_indices[d->states[q].accept_rule] + 1;
                if (!fe_has_out(&d->states[q])) v = -v;
            }
            fe_printf(e, "%s%d,", col++ % 16 ? " " : "\n\t", v);
        }
    }
    fe_puts(e, "\n};\n");
    if (e->prod) {
        /* State g accepts the rules yy_acclist[yy_acc_off[g] .. yy_acc_off[g + 1]),
        ** lowest rule first: REJECT moves to the next one. */
        const char *rty = fs->n_rules < 32767 ? "flex_int16_t" : "flex_int32_t";
        int n_acc = 0;
        fe_printf(e, "static const flex_int32_t yy_acc_off[%d] = {\n\t0, 0,", total + 1);
        col = 2;
        for (int s = 0; s < lc->n_states; s++) {
            const LimeDfa *d = e->dfas[s];
            if (!d) continue;
            const FeProduct *p = &e->prod[s];
            for (int q = 0; q < d->n_states; q++) {
                if (d->states[q].is_accept) {
                    int sid = d->states[q].accept_rule;
                    n_acc += p->set_off[sid + 1] - p->set_off[sid];
                }
                fe_printf(e, "%s%d,", col++ % 16 ? " " : "\n\t", n_acc);
            }
        }
        fe_puts(e, "\n};\n");
        fe_printf(e, "static const %s yy_acclist[%d] = {", rty, n_acc ? n_acc : 1);
        col = 0;
        for (int s = 0; s < lc->n_states; s++) {
            const LimeDfa *d = e->dfas[s];
            if (!d) continue;
            const FeProduct *p = &e->prod[s];
            for (int q = 0; q < d->n_states; q++) {
                if (!d->states[q].is_accept) continue;
                int sid = d->states[q].accept_rule;
                for (int k = p->set_off[sid]; k < p->set_off[sid + 1]; k++)
                    fe_printf(e, "%s%d,", col++ % 16 ? " " : "\n\t", p->set_rules[k] + 1);
            }
        }
        if (!n_acc) fe_puts(e, "0,");
        fe_puts(e, "\n};\n");
    }

    /* yy_start_tab[2 * sc + bol]: flex's yy_start is 1 + 2 * sc, and the
    ** scanner adds YY_START_BOL (0 or 1). */
    fe_printf(e, "static const %s yy_start_tab[%d] = {", ty, 2 * fs->n_sc);
    for (int c = 0; c < fs->n_sc; c++) {
        for (int bol = 0; bol < 2; bol++) {
            int st = lime_flex_state_index(fs, c, bol);
            const LimeDfa *d = st < lc->n_states ? e->dfas[st] : NULL;
            fe_printf(e, "%s%d,", (2 * c + bol) % 16 ? " " : "\n\t", d ? base[st] + d->start : 0);
        }
    }
    fe_puts(e, "\n};\n");
    if (fs->bol_needed)
        fe_puts(e, "#define YY_START_BOL (YY_CURRENT_BUFFER_LVALUE->yy_at_bol)\n");
    else
        fe_puts(e, "#define YY_START_BOL 0\n");

    /* Per rule (index = yy_act): trailing context and yylineno. */
    if (fs->has_trail) {
        fe_printf(e, "static const flex_int32_t yy_tc[%d] = {\n\t0,", fs->n_rules + 1);
        for (int k = 0; k < fs->n_rules; k++) {
            const LimeFlexRule *fr = &fs->rules[k];
            int v = fr->trail ? fr->trail : (fr->head ? -fr->head : 0);
            fe_printf(e, "%s%d,", (k + 1) % 16 ? " " : "\n\t", v);
        }
        fe_puts(e, "\n};\n");
    }
    if (fs->opt.yylineno == 1) {
        fe_printf(e, "static const flex_int32_t yy_rule_can_match_eol[%d] = {\n\t0,", fs->n_rules + 1);
        for (int k = 0; k < fs->n_rules; k++)
            fe_printf(e, "%s%d,", (k + 1) % 16 ? " " : "\n\t", fs->rules[k].can_match_eol);
        fe_puts(e, "\n};\n");
    }
    if (fs->opt.debug == 1) {
        fe_printf(e, "static const flex_int32_t yy_rule_linenum[%d] = {\n\t0,", fs->n_rules + 1);
        for (int k = 0; k < fs->n_rules; k++)
            fe_printf(e, "%s%d,", (k + 1) % 16 ? " " : "\n\t", fs->rules[k].line);
        fe_puts(e, "\n};\n");
    }
    free(base);
    return 0;
}

/* The switch body: one case per rule, <<EOF>> cases, the default rule. */
static void fe_actions(FlexEmit *e) {
    const LimeFlexSpec *fs = e->fs;
    int nodefault = fs->opt.nodefault == 1;
    for (int k = 0; k < fs->n_rules; k++) {
        const LimeFlexRule *fr = &fs->rules[k];
        if (fr->is_eof) continue;
        if (fr->pre_code) {
            fe_puts(e, fr->pre_code);
            if (fr->pre_code[0] && fr->pre_code[strlen(fr->pre_code) - 1] != '\n') fe_puts(e, "\n");
        }
        fe_printf(e, "case %d:\n", k + 1);
        if (fr->is_default) {
            fe_puts(e, "YY_RULE_SETUP\n");
            if (nodefault)
                fe_puts(e, "\tYY_FATAL_ERROR(\"flex scanner jammed\");\n");
            else
                fe_puts(e, "\tECHO;\n");
            fe_puts(e, "\tYY_BREAK\n");
            continue;
        }
        if (fr->action_rule != k) {
            /* A `|` rule shares the next real action; flex gives it its own
            ** case so its trailing context (if any) is undone first. */
            continue;
        }
        fe_puts(e, "YY_RULE_SETUP\n");
        fe_line_src(e, fr->action_line);
        fe_puts(e, fr->action ? fr->action : "");
        fe_puts(e, "\n");
        fe_line_out(e);
        fe_puts(e, "\tYY_BREAK\n");
    }
    /* <<EOF>>: the conditions sharing a rule share its case. */
    for (int k = -1; k < fs->n_rules; k++) {
        if (k >= 0 && !fs->rules[k].is_eof) continue;
        int any = 0;
        for (int c = 0; c < fs->n_sc; c++) {
            if ((fs->sc_eof_rule ? fs->sc_eof_rule[c] : -1) != k) continue;
            fe_printf(e, "case YY_STATE_EOF(%s):\n", fs->sc_names[c]);
            any = 1;
        }
        if (!any) continue;
        if (k < 0) {
            fe_puts(e, "\tyyterminate();\n");
            continue;
        }
        const LimeFlexRule *fr = &fs->rules[k];
        fe_line_src(e, fr->action_line);
        fe_puts(e, fr->action ? fr->action : "");
        fe_puts(e, "\n");
        fe_line_out(e);
        fe_puts(e, "\tYY_BREAK\n");
    }
}

static void fe_sc_defines(FlexEmit *e) {
    const LimeFlexSpec *fs = e->fs;
    for (int c = 0; c < fs->n_sc; c++)
        fe_printf(e, "#define %s %d\n", fs->sc_names[c], c);
    fe_puts(e, "\n");
}

static void fe_yy_decl(FlexEmit *e) {
    const LimeFlexOptions *o = &e->fs->opt;
    const char *args;
    if (o->bison_locations == 1)
        args = o->reentrant == 1 ? "(YYSTYPE *yylval_param, YYLTYPE *yylloc_param, yyscan_t yyscanner)"
                                 : "(YYSTYPE *yylval_param, YYLTYPE *yylloc_param)";
    else if (o->bison_bridge == 1)
        args = o->reentrant == 1 ? "(YYSTYPE *yylval_param, yyscan_t yyscanner)" : "(YYSTYPE *yylval_param)";
    else
        args = o->reentrant == 1 ? "(yyscan_t yyscanner)" : "(void)";
    fe_printf(e, "#ifndef YY_DECL\n#define YY_DECL_IS_OURS 1\nextern int yylex %s;\n#define YY_DECL int yylex %s\n#endif\n\n",
              args, args);
}

static void fe_yywrap(FlexEmit *e) {
    const LimeFlexOptions *o = &e->fs->opt;
    if (o->yywrap == 0) {
        if (o->reentrant == 1)
            fe_printf(e, "#define %swrap(yyscanner) (/*CONSTCOND*/1)\n", e->fs->opt.prefix);
        else
            fe_printf(e, "#define %swrap() (/*CONSTCOND*/1)\n", e->fs->opt.prefix);
        fe_puts(e, "#define YY_SKIP_YYWRAP\n");
    }
}

static void fe_interactive_init(FlexEmit *e) {
    const LimeFlexOptions *o = &e->fs->opt;
    if (o->always_interactive == 1)
        fe_puts(e, "\tb->yy_is_interactive = 1;\n");
    else if (o->never_interactive == 1)
        fe_puts(e, "\tb->yy_is_interactive = 0;\n");
    else if (flex_opt(o->unistd, 1))
        fe_puts(e, "\tb->yy_is_interactive = file ? (isatty(fileno(file)) > 0) : 0;\n");
    else
        fe_puts(e, "\tb->yy_is_interactive = 0;\n");
}

static int fe_skeleton(FlexEmit *e) {
    const LimeFlexSpec *fs = e->fs;
    int depth = 0, off = 0;
    for (size_t k = 0; k < sizeof flex_skel / sizeof *flex_skel; k++) {
        const char *ln = flex_skel[k];
        if (ln[0] == '%' && ln[1] == '%') {
            const char *d = ln + 2;
            if (!strncmp(d, "if ", 3)) {
                depth++;
                if (!off && !fe_cond(e, d + 3)) off = depth;
            } else if (!strcmp(d, "else")) {
                if (off == depth) off = 0;
                else if (!off) off = depth;
            } else if (!strcmp(d, "endif")) {
                if (off == depth) off = 0;
                depth--;
            } else if (off) {
                continue;
            } else if (!strcmp(d, "top-code")) {
                fe_code(e, fs->top_code, fs->n_top_code);
            } else if (!strcmp(d, "prefix-defines")) {
                fe_prefix_defines(e);
            } else if (!strcmp(d, "yywrap-macro")) {
                fe_yywrap(e);
            } else if (!strcmp(d, "tables")) {
                if (fe_tables(e) < 0) return -1;
            } else if (!strcmp(d, "defs-code")) {
                fe_code(e, fs->defs_code, fs->n_defs_code);
            } else if (!strcmp(d, "sc-defines")) {
                fe_sc_defines(e);
            } else if (!strcmp(d, "yy-decl")) {
                fe_yy_decl(e);
            } else if (!strcmp(d, "prolog-code")) {
                fe_code(e, fs->prolog_code, fs->n_prolog_code);
            } else if (!strcmp(d, "actions")) {
                fe_actions(e);
            } else if (!strcmp(d, "interactive-init")) {
                fe_interactive_init(e);
            } else if (!strcmp(d, "user-code")) {
                if (fs->user_code.text) fe_code(e, &fs->user_code, 1);
                else if (fs->opt.main_fn == 1)
                    fe_puts(e, "\nint main(void)\n{\n\tyylex();\n\treturn 0;\n}\n");
            } else {
                fprintf(stderr, "lime: internal error: unknown skeleton directive %s\n", ln);
            }
            continue;
        }
        if (off) continue;
        fe_puts(e, ln);
        fe_puts(e, "\n");
    }
    if (depth != 0) {
        fprintf(stderr, "lime: internal error: unbalanced %%%%if in the flex skeleton\n");
        return -1;
    }
    return 0;
}

/* Compile fs->spec and write the scanner.  out_c NULL = flex's default
** name (lex.<prefix>.c, or %option outfile).  Returns 0, 1 (diagnosed
** error) or 2 (I/O). */
int lime_flex_compile_and_emit(LimeFlexSpec *fs, const char *out_c, const char *out_h,
                               int to_stdout) {
    (void)out_h;
    LimeLexCompiled *lc = lime_lex_compile(fs->spec);
    if (!lc) {
        fprintf(stderr, "lime: out of memory\n");
        return 2;
    }
    if (lc->error_count > 0) {
        lime_lex_compiled_free(lc);
        return 1;
    }
    if (!fs->opt.prefix) fs->opt.prefix = strdup("yy");
    char defname[256];
    if (!out_c) out_c = fs->opt.outfile;
    if (!out_c) {
        snprintf(defname, sizeof defname, "lex.%s.c", fs->opt.prefix);
        out_c = defname;
    }
    FlexEmit e;
    memset(&e, 0, sizeof e);
    e.fs = fs;
    e.lc = lc;
    e.noline = fs->opt.noline == 1;
    e.out_name = to_stdout ? "<stdout>" : out_c;
    e.out = to_stdout ? stdout : fopen(out_c, "wb");
    if (!e.out) {
        fprintf(stderr, "lime: cannot write %s\n", out_c);
        lime_lex_compiled_free(lc);
        return 2;
    }
    int rc = -1;
    e.dfas = calloc((size_t)lc->n_states + 1, sizeof *e.dfas);
    if (e.dfas) {
        rc = 0;
        for (int s = 0; s < lc->n_states; s++) e.dfas[s] = lc->states[s].dfa;
    }
    if (rc == 0 && fs->uses_reject > 0) {
        e.prod = calloc((size_t)lc->n_states + 1, sizeof *e.prod);
        if (!e.prod) rc = -1;
        for (int s = 0; rc == 0 && s < lc->n_states; s++) {
            int pr = fe_product(&lc->states[s], &e.prod[s]);
            if (pr < 0) rc = -1;
            else if (pr > 0) {
                fprintf(stderr, "%s: REJECT: the scanner needs more than %d DFA states per start condition\n",
                        fs->filename ? fs->filename : "<stdin>", FE_PRODUCT_MAX);
                rc = 1;
            } else e.dfas[s] = e.prod[s].dfa;
        }
    }
    if (rc == 0) rc = fe_skeleton(&e);
    int werr = ferror(e.out);
    if (!to_stdout) werr |= fclose(e.out);
    else werr |= fflush(stdout);
    if (e.prod) {
        for (int s = 0; s < lc->n_states; s++) {
            lime_lex_dfa_free(e.prod[s].dfa);
            free(e.prod[s].set_off);
            free(e.prod[s].set_rules);
        }
        free(e.prod);
    }
    free(e.dfas);
    lime_lex_compiled_free(lc);
    if (rc > 0) {
        if (!to_stdout) remove(out_c);
        return 1;
    }
    if (rc < 0) {
        fprintf(stderr, "lime: out of memory\n");
        return 2;
    }
    if (werr) {
        fprintf(stderr, "lime: write error on %s\n", e.out_name);
        return 2;
    }
    return 0;
}
