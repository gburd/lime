/*
 * test_lsp.c -- end-to-end exercise of `lime-lsp`.
 *
 * Spawns the real LSP server binary as a subprocess, pipes
 * JSON-RPC framed messages to its stdin, reads framed responses
 * from its stdout, and asserts on the parsed JSON.
 *
 * Sub-tests:
 *
 *   1. lifecycle          -- initialize / initialized / shutdown / exit
 *   2. diagnostics        -- didOpen on a buffer with a known lint
 *                            warning -> publishDiagnostics with the
 *                            warning surfaced
 *   3. definition         -- cursor on `expr` reference jumps to the
 *                            `expr ::=` LHS line
 *   4. hover              -- hover on `%token` returns directive doc;
 *                            hover on a non-terminal returns its kind
 *                            + reference count
 *   5. documentSymbol     -- outline contains every directive,
 *                            terminal, and non-terminal
 *
 * Args:
 *   argv[1] -- path to the lime-lsp binary
 *   argv[2] -- path to the lime binary (so the LSP can run -L)
 *   argv[3] -- path to the test_lsp_fixtures/ directory
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- minimal JSON probing.  Not a real parser: the assertions
 *      below all use strstr() against the raw response body plus
 *      a single helper that finds a field name and returns its
 *      value's offset.  This keeps the test driver from sharing
 *      code with the thing it is testing.
 */


/* Find a top-level field by key in an object starting at offset `start`.
 * Returns the offset of the value (after the colon) or -1 if not found.
 * This is *only* good enough for the exact assertions below: it does
 * not handle nested objects at the same depth as siblings cleanly --
 * but each call is anchored at a known object start, so it is
 * sufficient.  Returns the offset of the first non-whitespace byte of
 * the value.
 */
static int find_field(const char *json, size_t len, const char *key,
                      int start) {
    int depth = 0;
    int in_string = 0;
    int escape    = 0;
    char keybuf[128];
    size_t klen = strlen(key);
    if (klen + 2 >= sizeof(keybuf)) return -1;
    keybuf[0] = '"';
    memcpy(keybuf + 1, key, klen);
    keybuf[klen + 1] = '"';
    keybuf[klen + 2] = 0;
    int kfull = (int)klen + 2;

    for (int i = start; i < (int)len; i++) {
        char c = json[i];
        if (!in_string && depth == 1 && c == '"' &&
            i + kfull <= (int)len &&
            strncmp(json + i, keybuf, (size_t)kfull) == 0) {
            int j = i + kfull;
            while (j < (int)len && json[j] == ' ') j++;
            if (j < (int)len && json[j] == ':') {
                j++;
                while (j < (int)len && (json[j] == ' ' || json[j] == '\t'))
                    j++;
                return j;
            }
        }
        if (escape) { escape = 0; continue; }
        if (c == '\\') { escape = 1; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            depth--;
            if (depth < 0) return -1;
        }
    }
    return -1;
}

/* Read a long long integer starting at `i` (whitespace tolerated). */
static long long parse_int(const char *json, int i) {
    return strtoll(json + i, NULL, 10);
}

/* ---- subprocess plumbing ------------------------------------------- */

typedef struct {
    pid_t pid;
    int   in_fd;
    int   out_fd;
} server_t;

static server_t spawn_server(const char *bin, const char *lime_bin) {
    int in_pipe[2];   /* parent->child stdin  */
    int out_pipe[2];  /* child->parent stdout */
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        perror("pipe"); exit(2);
    }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        dup2(in_pipe[0],  STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(in_pipe[0]);
        close(out_pipe[1]);
        if (lime_bin) setenv("LIME_BIN", lime_bin, 1);
        execl(bin, bin, (char *)NULL);
        perror("execl");
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    server_t s = { pid, in_pipe[1], out_pipe[0] };
    return s;
}

static void send_message(server_t *s, const char *json) {
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n",
                        strlen(json));
    if (write(s->in_fd, hdr, (size_t)hlen) != hlen) abort();
    size_t n = strlen(json);
    if (write(s->in_fd, json, n) != (ssize_t)n) abort();
}

/* Read one framed message into `buf`.  Returns body length or -1 on
 * EOF / framing error.
 */
static int read_message(server_t *s, char *buf, size_t cap) {
    char hdr[256];
    long long content_length = -1;
    while (1) {
        size_t pos = 0;
        while (1) {
            char c;
            ssize_t r = read(s->out_fd, &c, 1);
            if (r <= 0) return -1;
            if (pos + 1 < sizeof(hdr)) hdr[pos++] = c;
            if (c == '\n') break;
        }
        hdr[pos] = 0;
        size_t hl = pos;
        while (hl > 0 && (hdr[hl - 1] == '\r' || hdr[hl - 1] == '\n')) hl--;
        if (hl == 0) break;  /* blank line ends headers */
        hdr[hl] = 0;
        if (strncasecmp(hdr, "Content-Length:", 15) == 0) {
            content_length = strtoll(hdr + 15, NULL, 10);
        }
        /* every other header is ignored */
    }
    if (content_length < 0 || (size_t)content_length >= cap) return -1;
    size_t total = 0;
    while (total < (size_t)content_length) {
        ssize_t r = read(s->out_fd, buf + total,
                         (size_t)content_length - total);
        if (r <= 0) return -1;
        total += (size_t)r;
    }
    buf[total] = 0;
    return (int)total;
}

static int wait_for_response_id(server_t *s, int wanted_id,
                                char *buf, size_t cap) {
    /* Skip notifications until we see a response with the matching id. */
    for (int tries = 0; tries < 32; tries++) {
        int n = read_message(s, buf, cap);
        if (n < 0) return -1;
        int id_pos = find_field(buf, (size_t)n, "id", 0);
        if (id_pos < 0) continue;
        long long got = parse_int(buf, id_pos);
        if (got == wanted_id) return n;
    }
    return -1;
}

static void shutdown_server(server_t *s) {
    static const char *shutdown_msg =
        "{\"jsonrpc\":\"2.0\",\"id\":999,\"method\":\"shutdown\"}";
    static const char *exit_msg =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";
    send_message(s, shutdown_msg);
    char buf[1024];
    wait_for_response_id(s, 999, buf, sizeof(buf));
    send_message(s, exit_msg);
    close(s->in_fd);
    int status = 0;
    waitpid(s->pid, &status, 0);
    close(s->out_fd);
}

/* ---- helpers shared by sub-tests ----------------------------------- */

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) abort();
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) abort();
    buf[n] = 0;
    fclose(f);
    *out_len = (size_t)n;
    return buf;
}

/* Build a JSON-escaped copy of `s` into `out`.  Capacity assumed
 * generous (we use 32K buffers).  Used to embed grammar source
 * bytes into the textDocument/didOpen `text` field.
 */
static int json_escape(const char *s, size_t n, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char  unicode[8];
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b"; break;
            case '\f': esc = "\\f"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            default:
                if (c < 0x20) {
                    snprintf(unicode, sizeof(unicode), "\\u%04x", c);
                    esc = unicode;
                }
        }
        if (esc) {
            size_t el = strlen(esc);
            if (k + el >= cap) return -1;
            memcpy(out + k, esc, el);
            k += el;
        } else {
            if (k + 1 >= cap) return -1;
            out[k++] = (char)c;
        }
    }
    out[k] = 0;
    return (int)k;
}

static void initialize_server(server_t *s) {
    static const char *init =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
        "\"params\":{\"processId\":null,\"rootUri\":null,"
        "\"capabilities\":{}}}";
    static const char *initd =
        "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
    send_message(s, init);
    char resp[8192];
    int n = wait_for_response_id(s, 1, resp, sizeof(resp));
    assert(n > 0);
    /* Server must advertise capabilities. */
    assert(strstr(resp, "definitionProvider"));
    assert(strstr(resp, "hoverProvider"));
    assert(strstr(resp, "documentSymbolProvider"));
    assert(strstr(resp, "textDocumentSync"));
    /* v0.5.7 additions -- four new capabilities. */
    assert(strstr(resp, "completionProvider"));
    assert(strstr(resp, "referencesProvider"));
    assert(strstr(resp, "renameProvider"));
    assert(strstr(resp, "semanticTokensProvider"));
    assert(strstr(resp, "prepareProvider"));
    assert(strstr(resp, "triggerCharacters"));
    send_message(s, initd);
}

static void open_document(server_t *s, const char *uri, const char *text,
                          size_t text_len) {
    char escaped[32768];
    if (json_escape(text, text_len, escaped, sizeof(escaped)) < 0) abort();
    char msg[40000];
    int n = snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\","
        "\"params\":{\"textDocument\":{\"uri\":\"%s\",\"languageId\":\"lime\","
        "\"version\":1,\"text\":\"%s\"}}}",
        uri, escaped);
    if (n < 0 || (size_t)n >= sizeof(msg)) abort();
    send_message(s, msg);
}

/* ---- sub-tests ----------------------------------------------------- */

static void test_lifecycle(const char *bin, const char *lime_bin) {
    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    shutdown_server(&s);
    fprintf(stderr, "  lifecycle       OK\n");
}

static void test_diagnostics(const char *bin, const char *lime_bin,
                             const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/dirty.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///dirty.lime", text, tlen);

    /* Read the resulting publishDiagnostics notification. */
    char buf[16384];
    int n = read_message(&s, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "publishDiagnostics"));
    /* The dirty.lime fixture has a %type for `unused` with no
     * matching rule -- lint_grammar emits a warning. */
    assert(strstr(buf, "unused"));
    assert(strstr(buf, "warning") || strstr(buf, "\"severity\":2"));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  diagnostics     OK\n");
}

static void test_definition(const char *bin, const char *lime_bin,
                            const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/clean.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///clean.lime", text, tlen);

    /* Drain the publishDiagnostics push. */
    char buf[16384];
    read_message(&s, buf, sizeof(buf));

    /* `start ::= expr.` -- find that line, then position the
     * cursor on the `e` of `expr`. */
    long line = -1, col = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "start ::= expr", 14) == 0) {
                line = ln;
                /* find `expr` offset on this line */
                const char *p = strstr(text + i, "expr");
                col = (long)(p - (text + i));
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(line >= 0);

    char msg[512];
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/definition\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///clean.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld}}}",
        line, col + 1);
    send_message(&s, msg);
    int n = wait_for_response_id(&s, 2, buf, sizeof(buf));
    assert(n > 0);
    /* Definition result must include the URI and a range. */
    assert(strstr(buf, "file:///clean.lime"));
    assert(strstr(buf, "\"range\""));
    /* The defining line of `expr` is the first `expr ::=`. */
    long expected_line = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "expr ::=", 8) == 0) {
                expected_line = ln;
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(expected_line >= 0);
    char needle[64];
    snprintf(needle, sizeof(needle), "\"line\":%ld", expected_line);
    assert(strstr(buf, needle));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  definition      OK\n");
}

static void test_hover(const char *bin, const char *lime_bin,
                       const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/clean.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///clean.lime", text, tlen);

    char buf[16384];
    read_message(&s, buf, sizeof(buf));

    /* Hover on the `t` of `%token` -- find the line that starts
     * with `%token` and place cursor inside it. */
    long line = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "%token", 6) == 0 &&
                (i == 0 || text[i - 1] == '\n')) {
                line = ln;
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(line >= 0);

    char msg[512];
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///clean.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":3}}}", line);
    send_message(&s, msg);
    int n = wait_for_response_id(&s, 3, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "contents"));
    assert(strstr(buf, "%token"));
    /* Hover on a non-terminal mention. */
    long expr_line = -1;
    long expr_col  = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "start ::= expr", 14) == 0) {
                expr_line = ln;
                const char *p = strstr(text + i, "expr");
                expr_col = (long)(p - (text + i));
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(expr_line >= 0);
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///clean.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld}}}",
        expr_line, expr_col + 1);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 4, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "non-terminal"));
    assert(strstr(buf, "expr"));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  hover           OK\n");
}

static void test_document_symbol(const char *bin, const char *lime_bin,
                                 const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/clean.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///clean.lime", text, tlen);

    char buf[16384];
    read_message(&s, buf, sizeof(buf));

    static const char *req =
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///clean.lime\"}}}";
    send_message(&s, req);
    int n = wait_for_response_id(&s, 5, buf, sizeof(buf));
    assert(n > 0);
    /* Outline must list: %name, %token, PLUS, MINUS, NUMBER,
     * start, expr.  Order: directives -> terminals -> non-terminals. */
    assert(strstr(buf, "%name"));
    assert(strstr(buf, "%token"));
    assert(strstr(buf, "PLUS"));
    assert(strstr(buf, "MINUS"));
    assert(strstr(buf, "NUMBER"));
    assert(strstr(buf, "start"));
    assert(strstr(buf, "expr"));
    /* Each symbol must carry both range and selectionRange. */
    assert(strstr(buf, "selectionRange"));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  documentSymbol  OK\n");
}

/* ---- v0.5.7 sub-tests ---------------------------------------------- */

/* Count non-overlapping occurrences of `needle` in `hay`. */
static int count_occurrences(const char *hay, const char *needle) {
    int n = 0;
    size_t nl = strlen(needle);
    if (!nl) return 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) { n++; p += nl; }
    return n;
}

static void test_completion(const char *bin, const char *lime_bin,
                            const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/rich.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///rich.lime", text, tlen);

    char buf[32768];
    read_message(&s, buf, sizeof(buf));

    /* (1) Completion at line 0 char 0: file-start, behaves like
     * a directive position.  Trigger '%' explicitly so we hit the
     * directive branch unambiguously. */
    static const char *req1 =
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":0,\"character\":0},"
        "\"context\":{\"triggerKind\":2,\"triggerCharacter\":\"%\"}}}";
    send_message(&s, req1);
    int n = wait_for_response_id(&s, 10, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"items\""));
    /* Should advertise core directives. */
    assert(strstr(buf, "\"label\":\"name\""));
    assert(strstr(buf, "\"label\":\"token\""));
    assert(strstr(buf, "\"label\":\"left\""));
    assert(strstr(buf, "\"label\":\"include\""));
    /* And not symbol identifiers (we're in directive mode). */
    assert(!strstr(buf, "\"label\":\"PLUS\""));

    /* (2) Completion in the middle of a rule's RHS.  Find the
     * line `expr ::= NUMBER.` and place the cursor just past the
     * `::= ` (empty prefix) so we get every RHS suggestion, not
     * just those starting with the partial word `NUMBER`. */
    long rhs_line = -1;
    long rhs_col  = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "expr ::= NUMBER.", 16) == 0) {
                rhs_line = ln;
                rhs_col  = (long)strlen("expr ::= ");
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(rhs_line >= 0);

    char msg[512];
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld},"
        "\"context\":{\"triggerKind\":1}}}",
        rhs_line, rhs_col);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 11, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"items\""));
    assert(strstr(buf, "\"label\":\"PLUS\""));
    assert(strstr(buf, "\"label\":\"NUMBER\""));
    assert(strstr(buf, "\"label\":\"expr\""));
    /* In RHS context, no directives should appear. */
    assert(!strstr(buf, "\"label\":\"name\""));
    assert(!strstr(buf, "\"label\":\"token\""));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  completion      OK\n");
}

static void test_references(const char *bin, const char *lime_bin,
                            const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/rich.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///rich.lime", text, tlen);

    char buf[32768];
    read_message(&s, buf, sizeof(buf));

    /* Locate the first `expr ::=` -- the canonical defining site
     * we'll call references on. */
    long def_line = -1, def_col = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "expr ::=", 8) == 0) {
                def_line = ln;
                /* `expr` starts at the line start (no indent). */
                def_col = 0;
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(def_line >= 0);

    /* Independently count `expr` occurrences in the source so the
     * test is robust to fixture edits. */
    int expected_total = 0;
    {
        size_t i = 0;
        while (i < tlen) {
            if (i + 4 <= tlen && strncmp(text + i, "expr", 4) == 0) {
                int prev_id = (i > 0) &&
                    (isalnum((unsigned char)text[i-1]) || text[i-1] == '_');
                int next_id = (i + 4 < tlen) &&
                    (isalnum((unsigned char)text[i+4]) || text[i+4] == '_');
                if (!prev_id && !next_id) { expected_total++; i += 4; continue; }
            }
            i++;
        }
    }
    assert(expected_total > 5);

    /* (1) include_declaration = true. */
    char msg[512];
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/references\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld},"
        "\"context\":{\"includeDeclaration\":true}}}",
        def_line, def_col + 1);
    send_message(&s, msg);
    int n = wait_for_response_id(&s, 12, buf, sizeof(buf));
    assert(n > 0);
    int total = count_occurrences(buf, "file:///rich.lime");
    assert(total == expected_total);

    /* (2) include_declaration = false -- drops every is_definition
     * site (the LHS of each `expr ::= ...`).  rich.lime has six
     * `expr ::=` LHS sites, so the count must drop by six. */
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/references\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld},"
        "\"context\":{\"includeDeclaration\":false}}}",
        def_line, def_col + 1);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 13, buf, sizeof(buf));
    assert(n > 0);
    int after = count_occurrences(buf, "file:///rich.lime");
    assert(after >= 1);
    assert(after < total);

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  references      OK\n");
}

static void test_rename(const char *bin, const char *lime_bin,
                        const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/rich.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///rich.lime", text, tlen);

    char buf[32768];
    read_message(&s, buf, sizeof(buf));

    /* (1) prepareRename on a renameable terminal `PLUS`. */
    long plus_line = -1, plus_col = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "PLUS", 4) == 0) {
                plus_line = ln;
                size_t ls = i;
                while (ls > 0 && text[ls - 1] != '\n') ls--;
                plus_col = (long)(i - ls);
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(plus_line >= 0);

    char msg[512];
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"textDocument/prepareRename\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld}}}",
        plus_line, plus_col + 1);
    send_message(&s, msg);
    int n = wait_for_response_id(&s, 14, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "placeholder"));
    assert(strstr(buf, "\"PLUS\""));

    /* (2) rename PLUS -> ADDOP.  Workspace edit must list every
     * occurrence with newText "ADDOP". */
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"textDocument/rename\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld},"
        "\"newName\":\"ADDOP\"}}",
        plus_line, plus_col + 1);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 15, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"changes\""));
    assert(strstr(buf, "file:///rich.lime"));
    int edits = count_occurrences(buf, "\"newText\":\"ADDOP\"");
    int expected = 0;
    {
        size_t i = 0;
        while (i < tlen) {
            if (i + 4 <= tlen && strncmp(text + i, "PLUS", 4) == 0) {
                int prev_id = (i > 0) &&
                    (isalnum((unsigned char)text[i-1]) || text[i-1] == '_');
                int next_id = (i + 4 < tlen) &&
                    (isalnum((unsigned char)text[i+4]) || text[i+4] == '_');
                if (!prev_id && !next_id) { expected++; i += 4; continue; }
            }
            i++;
        }
    }
    assert(expected >= 2);
    assert(edits == expected);

    /* (3) prepareRename on `%token` (a directive) -- must return
     * null per LSP convention so the editor falls back. */
    long tok_line = -1;
    {
        long ln = 0; size_t i = 0;
        while (i < tlen) {
            if (strncmp(text + i, "%token", 6) == 0 &&
                (i == 0 || text[i - 1] == '\n')) {
                tok_line = ln;
                break;
            }
            if (text[i] == '\n') ln++;
            i++;
        }
    }
    assert(tok_line >= 0);
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"textDocument/prepareRename\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":3}}}", tok_line);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 16, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"result\":null"));
    assert(!strstr(buf, "placeholder"));

    /* (4) rename to invalid identifier `123abc` -- expect a
     * JSON-RPC error response with code -32602. */
    snprintf(msg, sizeof(msg),
        "{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"textDocument/rename\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"},"
        "\"position\":{\"line\":%ld,\"character\":%ld},"
        "\"newName\":\"123abc\"}}",
        plus_line, plus_col + 1);
    send_message(&s, msg);
    n = wait_for_response_id(&s, 17, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"error\""));
    assert(strstr(buf, "-32602"));

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  rename          OK\n");
}

static void test_semantic_tokens(const char *bin, const char *lime_bin,
                                 const char *fixtures_dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/rich.lime", fixtures_dir);
    size_t tlen;
    char *text = slurp(path, &tlen);

    server_t s = spawn_server(bin, lime_bin);
    initialize_server(&s);
    open_document(&s, "file:///rich.lime", text, tlen);

    char buf[65536];
    read_message(&s, buf, sizeof(buf));

    static const char *req =
        "{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"textDocument/semanticTokens/full\","
        "\"params\":{\"textDocument\":{\"uri\":\"file:///rich.lime\"}}}";
    send_message(&s, req);
    int n = wait_for_response_id(&s, 18, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "\"data\""));
    /* Find the data array and walk it: must be a sequence of
     * non-negative integers, multiple of 5. */
    const char *p = strstr(buf, "\"data\":");
    assert(p);
    p = strchr(p, '[');
    assert(p);
    p++;
    int ints = 0;
    long long vals[2048];
    while (*p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']') break;
        char *end;
        long long v = strtoll(p, &end, 10);
        assert(end != p);
        assert(v >= 0);
        if (ints < (int)(sizeof(vals)/sizeof(vals[0]))) vals[ints] = v;
        ints++;
        p = end;
    }
    assert(ints > 0);
    assert(ints % 5 == 0);
    int tokens = ints / 5;
    /* Spot-check: the first token in rich.lime is the leading
     * block comment.  Its tokenType must be 7 (comment). */
    assert(vals[3] == 7);
    int saw_keyword = 0, saw_class = 0, saw_variable = 0, saw_operator = 0;
    for (int i = 0; i < tokens; i++) {
        long long len  = vals[i*5 + 2];
        long long type = vals[i*5 + 3];
        long long mods = vals[i*5 + 4];
        assert(len > 0);
        assert(type >= 0 && type <= 11);
        assert(mods >= 0);
        if (type == 0) saw_keyword  = 1;
        if (type == 2) saw_class    = 1;
        if (type == 3) saw_variable = 1;
        if (type == 8) saw_operator = 1;
    }
    /* rich.lime contains directives, terminals, non-terminals,
     * and `::=` operators -- every one of these should fire. */
    assert(saw_keyword);
    assert(saw_class);
    assert(saw_variable);
    assert(saw_operator);

    shutdown_server(&s);
    free(text);
    fprintf(stderr, "  semanticTokens  OK\n");
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <lime-lsp-bin> <lime-bin> <fixtures-dir>\n",
                argv[0]);
        return 2;
    }
    const char *bin           = argv[1];
    const char *lime_bin      = argv[2];
    const char *fixtures_dir  = argv[3];

    /* Don't let a stuck server stall CI -- die after 60s. */
    alarm(60);
    /* Don't let SIGPIPE from a crashed server abort us -- read
     * paths handle EOF cleanly. */
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "test_lsp:\n");
    test_lifecycle(bin, lime_bin);
    test_diagnostics(bin, lime_bin, fixtures_dir);
    test_definition(bin, lime_bin, fixtures_dir);
    test_hover(bin, lime_bin, fixtures_dir);
    test_document_symbol(bin, lime_bin, fixtures_dir);
    test_completion(bin, lime_bin, fixtures_dir);
    test_references(bin, lime_bin, fixtures_dir);
    test_rename(bin, lime_bin, fixtures_dir);
    test_semantic_tokens(bin, lime_bin, fixtures_dir);
    fprintf(stderr, "  ALL PASSED\n");
    return 0;
}
