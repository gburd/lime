/*
 * lsp_protocol.h -- JSON-RPC 2.0 over stdio per the LSP base
 * protocol.
 *
 * The LSP wire format is:
 *
 *   Content-Length: <N>\r\n
 *   [optional Content-Type header]\r\n
 *   \r\n
 *   <N bytes of UTF-8 JSON>
 *
 * `lsp_protocol_run` owns the read loop: it reads framed
 * messages from stdin, dispatches to handlers based on the
 * message's `method`, and writes framed responses + notifications
 * to stdout.
 *
 * The protocol layer is deliberately ignorant of grammar
 * concerns -- it knows about JSON-RPC, message lifecycle, and
 * the small set of LSP method names we support; everything else
 * is delegated to the diagnostics / navigation modules.
 */
#ifndef LIME_LSP_PROTOCOL_H
#define LIME_LSP_PROTOCOL_H

#include "lsp_documents.h"
#include "lsp_json.h"

#include <stdio.h>

typedef struct {
    lsp_documents docs;
    char         *lime_bin;     /* heap, NULL if not configured     */
    int           shutdown_requested;
    int           initialized;
    FILE         *in;           /* defaults to stdin                */
    FILE         *out;          /* defaults to stdout               */
    FILE         *log;          /* may be NULL; otherwise logged to */
} lsp_server;

void lsp_server_init(lsp_server *s);
void lsp_server_free(lsp_server *s);

/* Run the read/dispatch loop until exit notification or EOF.
 * Returns the suggested process exit code (0 on clean shutdown,
 * 1 if we received `exit` without a preceding `shutdown`).
 */
int  lsp_server_run(lsp_server *s);

/* Write a framed JSON-RPC message to `out` and flush. */
void lsp_send_message(FILE *out, const json_value *msg);

/* Read one framed JSON-RPC message from `in`.  Returns NULL on
 * EOF or framing error.  Caller frees with json_free.
 */
json_value *lsp_read_message(FILE *in);

#endif /* LIME_LSP_PROTOCOL_H */
