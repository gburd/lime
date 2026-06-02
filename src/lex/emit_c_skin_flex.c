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

#include <ctype.h>
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
                        const char *const *rule_names, int n_rules) {
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

static void emit_source(FILE *out, const char *base_id, const char *prefix) {
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
        "        yytext = b->bytes + b->pos;\n"
        "        yyleng = (int)consumed;\n"
        "        for (size_t i = 0; i < consumed; i++) {\n"
        "            if (yytext[i] == '\\n') yylineno++;\n"
        "        }\n"
        "        b->pos += consumed;\n"
        "        return rule + 1;  /* +1 keeps 0 reserved for EOF */\n"
        "    }\n"
        "}\n",
        prefix);
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
    emit_header(fh, base_id, prefix, rule_names, n_rules);
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
    emit_source(fc, base_id, prefix);
    int cerr = ferror(fc);
    fclose(fc);
    if (cerr) {
        fprintf(stderr,
                "lime --target=c:flex: write error on %s\n", out_c_path);
        return 1;
    }
    return 0;
}
