/*
 * lsp_protocol.c -- JSON-RPC 2.0 framing + LSP method dispatch.
 *
 * The dispatch table is small enough (single-digit methods) that
 * a chain of strcmp wins on both clarity and code size compared
 * to a hand-rolled hash.  Method handlers are kept inline below
 * the dispatcher so the file reads top-down.
 *
 * Lifecycle invariants:
 *
 *   - We respond to the first `initialize` request with our
 *     ServerCapabilities object, then mark `initialized`.
 *   - Until `initialized`, every other request gets an LSP error
 *     (-32002 ServerNotInitialized).
 *   - A `shutdown` request flips a flag; subsequent requests get
 *     -32600 InvalidRequest.
 *   - `exit` ends the loop.  Exit code 0 if shutdown was first,
 *     1 otherwise (per the LSP spec).
 */

#include "lsp_protocol.h"

#include "lsp_diagnostics.h"
#include "lsp_documents.h"
#include "lsp_format.h"
#include "lsp_json.h"
#include "lsp_navigation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <string.h>
#  define strncasecmp _strnicmp
#else
#  include <strings.h>
#endif

/* ---- framing -------------------------------------------------------- */

void lsp_send_message(FILE *out, const json_value *msg) {
    char *body = json_serialize(msg);
    if (!body) return;
    size_t blen = strlen(body);
    fprintf(out, "Content-Length: %zu\r\n\r\n", blen);
    fwrite(body, 1, blen, out);
    fflush(out);
    free(body);
}

/* Read one HTTP-style header line into `buf`; returns 0 on EOF.
 * The trailing \r\n is stripped.  Lines >= cap-1 bytes are
 * truncated.
 */
static int read_header_line(FILE *in, char *buf, size_t cap) {
    size_t i = 0;
    int    seen_cr = 0;
    int    c;
    while ((c = fgetc(in)) != EOF) {
        if (c == '\r') { seen_cr = 1; continue; }
        if (c == '\n') {
            buf[i] = 0;
            return 1;
        }
        if (seen_cr) {
            /* lone CR not followed by LF -- treat as separator anyway */
            buf[i] = 0;
            ungetc(c, in);
            return 1;
        }
        if (i + 1 < cap) buf[i++] = (char)c;
    }
    return i > 0 ? 1 : 0;
}

json_value *lsp_read_message(FILE *in) {
    long long content_length = -1;
    char header[1024];
    while (read_header_line(in, header, sizeof(header))) {
        if (header[0] == 0) break;  /* blank line ends headers */
        const char *p = header;
        if (strncasecmp(p, "Content-Length:", 15) == 0) {
            p += 15;
            while (*p == ' ' || *p == '\t') p++;
            content_length = strtoll(p, NULL, 10);
        }
        /* every other header is ignored */
    }
    if (content_length < 0) return NULL;

    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) return NULL;
    size_t total = 0;
    while (total < (size_t)content_length) {
        size_t n = fread(body + total, 1, (size_t)content_length - total, in);
        if (n == 0) { free(body); return NULL; }
        total += n;
    }
    body[content_length] = 0;
    json_value *v = json_parse(body, (size_t)content_length);
    free(body);
    return v;
}

/* ---- response helpers ---------------------------------------------- */

static void send_response(FILE *out, const json_value *id, json_value *result) {
    json_value *m = json_make_object();
    json_object_set(m, "jsonrpc", json_make_string("2.0"));
    if (id) {
        /* clone id by serialize+parse; the value tree we hand to
         * send_response borrows from the request and we don't
         * want to free it twice */
        char *s = json_serialize(id);
        if (s) {
            json_value *clone = json_parse(s, strlen(s));
            free(s);
            if (clone) json_object_set(m, "id", clone);
            else       json_object_set(m, "id", json_make_null());
        } else {
            json_object_set(m, "id", json_make_null());
        }
    } else {
        json_object_set(m, "id", json_make_null());
    }
    json_object_set(m, "result", result ? result : json_make_null());
    lsp_send_message(out, m);
    json_free(m);
}

static void send_error(FILE *out, const json_value *id, int code,
                       const char *message) {
    json_value *err = json_make_object();
    json_object_set(err, "code",    json_make_int(code));
    json_object_set(err, "message", json_make_string(message));
    json_value *m = json_make_object();
    json_object_set(m, "jsonrpc", json_make_string("2.0"));
    if (id) {
        char *s = json_serialize(id);
        if (s) {
            json_value *clone = json_parse(s, strlen(s));
            free(s);
            if (clone) json_object_set(m, "id", clone);
            else       json_object_set(m, "id", json_make_null());
        } else {
            json_object_set(m, "id", json_make_null());
        }
    } else {
        json_object_set(m, "id", json_make_null());
    }
    json_object_set(m, "error", err);
    lsp_send_message(out, m);
    json_free(m);
}

static void send_notification(FILE *out, const char *method,
                              json_value *params) {
    json_value *m = json_make_object();
    json_object_set(m, "jsonrpc", json_make_string("2.0"));
    json_object_set(m, "method",  json_make_string(method));
    json_object_set(m, "params",  params ? params : json_make_null());
    lsp_send_message(out, m);
    json_free(m);
}

/* ---- diagnostics push helper --------------------------------------- */

static void publish_diagnostics(lsp_server *s, const char *uri,
                                const char *text, size_t text_len) {
    json_value *diags = lsp_diagnostics_run(s->lime_bin, text, text_len);
    if (!diags) diags = json_make_array();
    json_value *params = json_make_object();
    json_object_set(params, "uri",         json_make_string(uri));
    json_object_set(params, "diagnostics", diags);
    send_notification(s->out, "textDocument/publishDiagnostics", params);
    /* send_notification takes ownership of `params` and frees it. */
}

/* ---- handlers ------------------------------------------------------- */

static json_value *server_capabilities(void) {
    json_value *caps = json_make_object();

    /* TextDocumentSyncOptions { openClose, change: Full }.
     * change=1 (Full) -- we do not support incremental. */
    json_value *sync = json_make_object();
    json_object_set(sync, "openClose", json_make_bool(1));
    json_object_set(sync, "change",    json_make_int(1));
    json_object_set(sync, "save",      json_make_bool(1));
    json_object_set(caps, "textDocumentSync", sync);

    json_object_set(caps, "definitionProvider",     json_make_bool(1));
    json_object_set(caps, "hoverProvider",          json_make_bool(1));
    json_object_set(caps, "documentSymbolProvider", json_make_bool(1));
    json_object_set(caps, "referencesProvider",     json_make_bool(1));

    /* CompletionOptions { triggerCharacters: ["%", " ", "."] }. */
    json_value *comp = json_make_object();
    json_value *trig = json_make_array();
    json_array_push(trig, json_make_string("%"));
    json_array_push(trig, json_make_string(" "));
    json_array_push(trig, json_make_string("."));
    json_object_set(comp, "triggerCharacters", trig);
    json_object_set(caps, "completionProvider", comp);

    /* RenameOptions { prepareProvider: true }. */
    json_value *ren = json_make_object();
    json_object_set(ren, "prepareProvider", json_make_bool(1));
    json_object_set(caps, "renameProvider", ren);

    /* SemanticTokensOptions { legend, full: true }.  The legend
     * mirrors lsp_semantic_tokens.c's emitted token-type /
     * modifier IDs so the editor can decode the int-array stream. */
    json_value *sem    = json_make_object();
    json_value *legend = json_make_object();
    json_value *types  = json_make_array();
    for (size_t i = 0; lsp_semantic_token_types[i]; i++)
        json_array_push(types,
                        json_make_string(lsp_semantic_token_types[i]));
    json_value *mods   = json_make_array();
    for (size_t i = 0; lsp_semantic_token_modifiers[i]; i++)
        json_array_push(mods,
                        json_make_string(lsp_semantic_token_modifiers[i]));
    json_object_set(legend, "tokenTypes",     types);
    json_object_set(legend, "tokenModifiers", mods);
    json_object_set(sem,    "legend",         legend);
    json_object_set(sem,    "full",           json_make_bool(1));
    json_object_set(caps,   "semanticTokensProvider", sem);

    /* SignatureHelpOptions { triggerCharacters: ["%", " "] }. */
    json_value *sig = json_make_object();
    json_value *sig_trig = json_make_array();
    json_array_push(sig_trig, json_make_string("%"));
    json_array_push(sig_trig, json_make_string(" "));
    json_object_set(sig, "triggerCharacters", sig_trig);
    json_object_set(caps, "signatureHelpProvider", sig);

    /* CodeLensOptions { resolveProvider: false }. */
    json_value *lens = json_make_object();
    json_object_set(lens, "resolveProvider", json_make_bool(0));
    json_object_set(caps, "codeLensProvider", lens);

    /* CodeActionOptions { codeActionKinds: ["source.format"] }. */
    json_value *act = json_make_object();
    json_value *kinds = json_make_array();
    json_array_push(kinds, json_make_string("source.format"));
    json_object_set(act, "codeActionKinds", kinds);
    json_object_set(caps, "codeActionProvider", act);

    /* DocumentFormattingOptions { } -- bool true is also accepted
     * but the object form is more explicit and matches our pattern
     * for other capabilities. */
    json_object_set(caps, "documentFormattingProvider", json_make_bool(1));

    return caps;
}

static void handle_initialize(lsp_server *s, const json_value *id,
                              const json_value *params) {
    (void)params;
    json_value *result = json_make_object();
    json_object_set(result, "capabilities", server_capabilities());
    json_value *info = json_make_object();
    json_object_set(info, "name",    json_make_string("lime-lsp"));
    json_object_set(info, "version",
                    json_make_string(
#ifdef LIME_VERSION_STRING
                        LIME_VERSION_STRING
#else
                        "0.0.0"
#endif
                    ));
    json_object_set(result, "serverInfo", info);
    send_response(s->out, id, result);
    s->initialized = 1;
}

static void handle_shutdown(lsp_server *s, const json_value *id) {
    s->shutdown_requested = 1;
    send_response(s->out, id, json_make_null());
}

static void handle_did_open(lsp_server *s, const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri  = json_string(json_get(td, "uri"));
    const char *text = json_string(json_get(td, "text"));
    size_t      tlen = json_string_len(json_get(td, "text"));
    long long   ver  = json_int(json_get(td, "version"));
    if (!uri || !text) return;
    lsp_documents_open(&s->docs, uri, ver, text, tlen);
    publish_diagnostics(s, uri, text, tlen);
}

static void handle_did_change(lsp_server *s, const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    long long   ver = json_int(json_get(td, "version"));
    const json_value *changes = json_get(params, "contentChanges");
    if (!uri || !changes) return;
    /* TextDocumentSyncKind.Full -- last change is full text. */
    size_t n = json_array_size(changes);
    if (n == 0) return;
    const json_value *ch = json_at(changes, n - 1);
    const char *text = json_string(json_get(ch, "text"));
    size_t      tlen = json_string_len(json_get(ch, "text"));
    if (!text) return;
    lsp_documents_set_text(&s->docs, uri, ver, text, tlen);
    publish_diagnostics(s, uri, text, tlen);
}

static void handle_did_save(lsp_server *s, const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    if (!uri) return;
    lsp_document *d = lsp_documents_get(&s->docs, uri);
    if (!d) return;
    publish_diagnostics(s, uri, d->text, d->text_len);
}

static void handle_did_close(lsp_server *s, const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    if (!uri) return;
    lsp_documents_close(&s->docs, uri);
    /* Push an empty diagnostics array so editors clear stale
     * markers. */
    json_value *p = json_make_object();
    json_object_set(p, "uri",         json_make_string(uri));
    json_object_set(p, "diagnostics", json_make_array());
    send_notification(s->out, "textDocument/publishDiagnostics", p);
    /* send_notification takes ownership of `p` and frees it. */
}

static void handle_definition(lsp_server *s, const json_value *id,
                              const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_null());
        return;
    }
    json_value *loc = lsp_navigation_definition(uri, d->text, d->text_len,
                                                line, ch);
    send_response(s->out, id, loc);
}

static void handle_hover(lsp_server *s, const json_value *id,
                         const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_null());
        return;
    }
    json_value *hov = lsp_navigation_hover(d->text, d->text_len, line, ch);
    send_response(s->out, id, hov);
}

static void handle_document_symbol(lsp_server *s, const json_value *id,
                                   const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_array());
        return;
    }
    json_value *syms = lsp_navigation_document_symbol(d->text, d->text_len);
    send_response(s->out, id, syms);
}

static void handle_completion(lsp_server *s, const json_value *id,
                              const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    char trigger = 0;
    const json_value *ctx = json_get(params, "context");
    if (ctx) {
        const char *trig = json_string(json_get(ctx, "triggerCharacter"));
        if (trig && trig[0]) trigger = trig[0];
    }
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_error(s->out, id, -32602, "Unknown textDocument URI");
        return;
    }
    json_value *r = lsp_navigation_completion(d->text, d->text_len,
                                              line, ch, trigger);
    send_response(s->out, id, r);
}

static void handle_references(lsp_server *s, const json_value *id,
                              const json_value *params) {
    const json_value *td  = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const json_value *ctx = json_get(params, "context");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    int include_decl = 1;
    if (ctx) {
        const json_value *iv = json_get(ctx, "includeDeclaration");
        if (iv) include_decl = json_bool(iv);
    }
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_error(s->out, id, -32602, "Unknown textDocument URI");
        return;
    }
    json_value *r = lsp_navigation_references(uri, d->text, d->text_len,
                                              line, ch, include_decl);
    send_response(s->out, id, r);
}

static void handle_prepare_rename(lsp_server *s, const json_value *id,
                                  const json_value *params) {
    const json_value *td  = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_error(s->out, id, -32602, "Unknown textDocument URI");
        return;
    }
    char err[256];
    json_value *r = lsp_navigation_prepare_rename(d->text, d->text_len,
                                                  line, ch, err, sizeof(err));
    if (!r) {
        /* LSP: prepareRename returning null tells the editor the
         * position isn't renameable; the editor surfaces this as a
         * non-modal status message rather than an error popup. */
        send_response(s->out, id, json_make_null());
        return;
    }
    send_response(s->out, id, r);
}

static void handle_rename(lsp_server *s, const json_value *id,
                          const json_value *params) {
    const json_value *td  = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri      = json_string(json_get(td, "uri"));
    const char *new_name = json_string(json_get(params, "newName"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_error(s->out, id, -32602, "Unknown textDocument URI");
        return;
    }
    if (!new_name) {
        send_error(s->out, id, -32602, "Missing newName parameter");
        return;
    }
    char err[256];
    json_value *r = lsp_navigation_rename(uri, d->text, d->text_len,
                                          line, ch, new_name,
                                          err, sizeof(err));
    if (!r) {
        send_error(s->out, id, -32602, err[0] ? err : "rename failed");
        return;
    }
    send_response(s->out, id, r);
}

static void handle_semantic_tokens(lsp_server *s, const json_value *id,
                                   const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        /* Spec: returning null is allowed; editors clear highlights. */
        send_response(s->out, id, json_make_null());
        return;
    }
    json_value *r = lsp_navigation_semantic_tokens(d->text, d->text_len);
    send_response(s->out, id, r);
}

static void handle_signature_help(lsp_server *s, const json_value *id,
                                  const json_value *params) {
    const json_value *td  = json_get(params, "textDocument");
    const json_value *pos = json_get(params, "position");
    const char *uri = json_string(json_get(td, "uri"));
    long long line = json_int(json_get(pos, "line"));
    long long ch   = json_int(json_get(pos, "character"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_null());
        return;
    }
    json_value *r = lsp_navigation_signature_help(d->text, d->text_len,
                                                  line, ch);
    send_response(s->out, id, r);
}

static void handle_code_lens(lsp_server *s, const json_value *id,
                             const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_array());
        return;
    }
    json_value *r = lsp_navigation_code_lens(d->text, d->text_len);
    send_response(s->out, id, r);
}

static void handle_code_action(lsp_server *s, const json_value *id,
                               const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const json_value *range = json_get(params, "range");
    const json_value *start = range ? json_get(range, "start") : NULL;
    long long line = start ? json_int(json_get(start, "line"))      : 0;
    long long ch   = start ? json_int(json_get(start, "character")) : 0;
    const char *uri = json_string(json_get(td, "uri"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_array());
        return;
    }
    json_value *r = lsp_navigation_code_actions(d->text, d->text_len,
                                                line, ch);
    send_response(s->out, id, r);
}

static void handle_formatting(lsp_server *s, const json_value *id,
                              const json_value *params) {
    const json_value *td = json_get(params, "textDocument");
    const char *uri = json_string(json_get(td, "uri"));
    lsp_document *d = uri ? lsp_documents_get(&s->docs, uri) : NULL;
    if (!d) {
        send_response(s->out, id, json_make_array());
        return;
    }
    json_value *r = lsp_format_run(s->lime_bin, d->text, d->text_len);
    send_response(s->out, id, r);
}

/* ---- main dispatch -------------------------------------------------- */

void lsp_server_init(lsp_server *s) {
    memset(s, 0, sizeof(*s));
    lsp_documents_init(&s->docs);
    s->in  = stdin;
    s->out = stdout;
    s->log = NULL;
}

void lsp_server_free(lsp_server *s) {
    lsp_documents_free(&s->docs);
    free(s->lime_bin);
    s->lime_bin = NULL;
}

int lsp_server_run(lsp_server *s) {
    int got_exit = 0;
    while (!got_exit) {
        json_value *msg = lsp_read_message(s->in);
        if (!msg) break;
        const json_value *method_v = json_get(msg, "method");
        const json_value *id       = json_get(msg, "id");
        const json_value *params   = json_get(msg, "params");
        const char       *method   = json_string(method_v);
        if (!method) { json_free(msg); continue; }

        /* Notifications never carry an `id`; requests always do. */
        int is_request = (id != NULL);

        if (s->log) {
            fprintf(s->log, "<-- %s%s\n", method,
                    is_request ? " (request)" : " (notification)");
            fflush(s->log);
        }

        if (strcmp(method, "exit") == 0) {
            got_exit = 1;
            json_free(msg);
            break;
        }

        if (!s->initialized && strcmp(method, "initialize") != 0) {
            if (is_request) {
                send_error(s->out, id, -32002, "Server not initialized");
            }
            json_free(msg);
            continue;
        }

        if (s->shutdown_requested && is_request &&
            strcmp(method, "exit") != 0) {
            send_error(s->out, id, -32600,
                       "Request received after shutdown");
            json_free(msg);
            continue;
        }

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(s, id, params);
        } else if (strcmp(method, "initialized") == 0) {
            /* notification, no-op */
        } else if (strcmp(method, "shutdown") == 0) {
            handle_shutdown(s, id);
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(s, params);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(s, params);
        } else if (strcmp(method, "textDocument/didSave") == 0) {
            handle_did_save(s, params);
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            handle_did_close(s, params);
        } else if (strcmp(method, "textDocument/definition") == 0) {
            handle_definition(s, id, params);
        } else if (strcmp(method, "textDocument/hover") == 0) {
            handle_hover(s, id, params);
        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            handle_document_symbol(s, id, params);
        } else if (strcmp(method, "textDocument/completion") == 0) {
            handle_completion(s, id, params);
        } else if (strcmp(method, "textDocument/references") == 0) {
            handle_references(s, id, params);
        } else if (strcmp(method, "textDocument/prepareRename") == 0) {
            handle_prepare_rename(s, id, params);
        } else if (strcmp(method, "textDocument/rename") == 0) {
            handle_rename(s, id, params);
        } else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
            handle_semantic_tokens(s, id, params);
        } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
            handle_signature_help(s, id, params);
        } else if (strcmp(method, "textDocument/codeLens") == 0) {
            handle_code_lens(s, id, params);
        } else if (strcmp(method, "textDocument/codeAction") == 0) {
            handle_code_action(s, id, params);
        } else if (strcmp(method, "textDocument/formatting") == 0) {
            handle_formatting(s, id, params);
        } else if (is_request) {
            send_error(s->out, id, -32601, "Method not found");
        }
        /* else: unknown notification, silently dropped per LSP spec. */

        json_free(msg);
    }

    return s->shutdown_requested ? 0 : 1;
}
