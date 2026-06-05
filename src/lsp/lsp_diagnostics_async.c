/*
 * lsp_diagnostics_async.c -- background diagnostic worker.
 *
 * See lsp_diagnostics_async.h for design notes.  This file
 * implements the per-URI single-slot worker queue.
 *
 * The shape is intentionally simple:
 *
 *   - One worker thread, processes requests serially from a
 *     queue.  We don't run multiple lint passes in parallel
 *     because (a) the in-process compiler isn't yet thread-safe
 *     across concurrent calls and (b) for a single editor session
 *     the user types in only one document at a time, so
 *     parallelism wouldn't help.
 *
 *   - Per-URI generation counter.  Each request bumps the URI's
 *     latest generation.  When the worker dequeues a request, it
 *     captures the generation; after the lint completes, it
 *     compares against the URI's CURRENT latest -- if the URI's
 *     newer request superseded this one, drop the result.
 *
 *   - Out-stream lock guards the LSP framing (Content-Length +
 *     body); concurrent writes would corrupt the JSON-RPC stream.
 *     The server's main thread also holds this lock when it
 *     replies to requests.
 *
 * POSIX-only for now.  Windows stub via the meson build option
 * `lime_lsp_async`.
 */
#include "lsp_diagnostics_async.h"
#include "lsp_diagnostics.h"
#include "lsp_json.h"

#include <stdlib.h>

#if defined(_WIN32) && !defined(__MINGW32__)

/*
** Windows stub.  lime_threads.h doesn't shim pthread_create/cond/key,
** so the LSP server falls back to the synchronous diagnostic path on
** Windows.  When a Win32 thread shim lands (TODO: lsp_diagnostics_
** async_win32.c), this stub goes away.
*/

struct lsp_diagnostics_async {
    int unused;
};

lsp_diagnostics_async *lsp_diagnostics_async_create(FILE *out, const char *lime_bin) {
    (void)out; (void)lime_bin;
    return NULL;  /* publish_diagnostics falls through to sync path. */
}

void lsp_diagnostics_async_request(lsp_diagnostics_async *p,
                                    const char *uri,
                                    const char *text, size_t text_len) {
    (void)p; (void)uri; (void)text; (void)text_len;
}

void lsp_diagnostics_async_destroy(lsp_diagnostics_async *p) {
    (void)p;
}

#else /* POSIX */

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

/* Per-URI generation tracking.  Max 64 URIs concurrently open in
** an LSP session is plenty for editor workloads (typical: 1-3). */
#define ASYNC_MAX_URIS 64

typedef struct {
    char    *uri;          /* heap, NUL-terminated.  NULL when slot empty. */
    uint64_t latest_gen;   /* monotonic; bumped on every request. */
} uri_gen;

/* Per-request work item. */
typedef struct work_item {
    char     *uri;        /* heap */
    char     *text;       /* heap; NULL means "publish empty diagnostics" */
    size_t    text_len;
    uint64_t  gen;        /* request generation; matched against uri_gen.latest_gen */
    struct work_item *next;
} work_item;

struct lsp_diagnostics_async {
    pthread_t        worker;
    pthread_mutex_t  queue_mu;
    pthread_cond_t   queue_cv;
    work_item       *queue_head;
    work_item       *queue_tail;
    int              shutdown;     /* protected by queue_mu */

    pthread_mutex_t  uris_mu;
    uri_gen          uris[ASYNC_MAX_URIS];

    pthread_mutex_t  out_mu;
    FILE            *out;          /* borrowed */

    char            *lime_bin;     /* heap; copied in _create */
};

/* ------------------------------------------------------------------ */
/*  URI generation table -- allocates a slot lazily.                   */
/* ------------------------------------------------------------------ */

/* Caller holds uris_mu.  Returns slot index or -1 if table full. */
static int find_or_alloc_uri_slot(lsp_diagnostics_async *p, const char *uri) {
    int free_slot = -1;
    for (int i = 0; i < ASYNC_MAX_URIS; i++) {
        if (p->uris[i].uri && strcmp(p->uris[i].uri, uri) == 0) {
            return i;
        }
        if (!p->uris[i].uri && free_slot == -1) {
            free_slot = i;
        }
    }
    if (free_slot < 0) return -1;
    p->uris[free_slot].uri = strdup(uri);
    if (!p->uris[free_slot].uri) return -1;
    p->uris[free_slot].latest_gen = 0;
    return free_slot;
}

/* Bump the URI's generation counter and return the new value.
** Caller does NOT need to hold uris_mu; this function takes it. */
static uint64_t bump_uri_gen(lsp_diagnostics_async *p, const char *uri) {
    pthread_mutex_lock(&p->uris_mu);
    int idx = find_or_alloc_uri_slot(p, uri);
    uint64_t g = 0;
    if (idx >= 0) {
        p->uris[idx].latest_gen++;
        g = p->uris[idx].latest_gen;
    }
    pthread_mutex_unlock(&p->uris_mu);
    return g;
}

/* Read-only check: is `gen` the current latest for `uri`?  Used by
** the worker after lint completes to decide whether to publish. */
static int gen_is_current(lsp_diagnostics_async *p, const char *uri, uint64_t gen) {
    pthread_mutex_lock(&p->uris_mu);
    int current = 0;
    for (int i = 0; i < ASYNC_MAX_URIS; i++) {
        if (p->uris[i].uri && strcmp(p->uris[i].uri, uri) == 0) {
            current = (gen == p->uris[i].latest_gen);
            break;
        }
    }
    pthread_mutex_unlock(&p->uris_mu);
    return current;
}

/* ------------------------------------------------------------------ */
/*  LSP framing-aware send (must hold out_mu)                          */
/* ------------------------------------------------------------------ */

static void send_diagnostics_locked(lsp_diagnostics_async *p,
                                     const char *uri, json_value *diags) {
    /* Mirror lsp_send_message + send_notification's framing.  We
    ** can't call lsp_send_message directly because it doesn't take
    ** the mutex; the main thread also writes to `out` and we
    ** serialise here. */
    json_value *params = json_make_object();
    json_object_set(params, "uri", json_make_string(uri));
    json_object_set(params, "diagnostics", diags);

    json_value *msg = json_make_object();
    json_object_set(msg, "jsonrpc", json_make_string("2.0"));
    json_object_set(msg, "method", json_make_string("textDocument/publishDiagnostics"));
    json_object_set(msg, "params", params);

    char *body = json_serialize(msg);
    if (body) {
        size_t blen = strlen(body);
        fprintf(p->out, "Content-Length: %zu\r\n\r\n", blen);
        fwrite(body, 1, blen, p->out);
        fflush(p->out);
        free(body);
    }
    json_free(msg);
}

/* ------------------------------------------------------------------ */
/*  Worker thread                                                       */
/* ------------------------------------------------------------------ */

static void *worker_main(void *arg) {
    lsp_diagnostics_async *p = (lsp_diagnostics_async *)arg;

    for (;;) {
        /* Dequeue. */
        pthread_mutex_lock(&p->queue_mu);
        while (!p->shutdown && p->queue_head == NULL) {
            pthread_cond_wait(&p->queue_cv, &p->queue_mu);
        }
        if (p->shutdown && p->queue_head == NULL) {
            pthread_mutex_unlock(&p->queue_mu);
            break;
        }
        work_item *w = p->queue_head;
        p->queue_head = w->next;
        if (p->queue_head == NULL) p->queue_tail = NULL;
        pthread_mutex_unlock(&p->queue_mu);

        /* Run lint (may take ~2s on a large grammar).  No locks
        ** held here; main thread is free to enqueue new work. */
        json_value *diags = NULL;
        if (w->text == NULL) {
            /* didClose-style: publish empty diagnostics. */
            diags = json_make_array();
        } else {
            diags = lsp_diagnostics_run(p->lime_bin, w->text, w->text_len);
            if (!diags) diags = json_make_array();
        }

        /* Stale-check + publish. */
        if (gen_is_current(p, w->uri, w->gen)) {
            pthread_mutex_lock(&p->out_mu);
            send_diagnostics_locked(p, w->uri, diags);
            pthread_mutex_unlock(&p->out_mu);
            /* send_diagnostics_locked transferred ownership of diags
            ** into the JSON tree it built; that tree was freed via
            ** json_free(msg).  Don't free `diags` again. */
        } else {
            /* Stale -- a newer request for this URI superseded us.
            ** Drop the result without publishing. */
            json_free(diags);
        }

        free(w->uri);
        free(w->text);
        free(w);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

lsp_diagnostics_async *lsp_diagnostics_async_create(FILE *out, const char *lime_bin) {
    lsp_diagnostics_async *p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->out = out;
    p->lime_bin = lime_bin ? strdup(lime_bin) : NULL;

    if (pthread_mutex_init(&p->queue_mu, NULL) != 0) goto fail;
    if (pthread_cond_init(&p->queue_cv, NULL) != 0)  goto fail_qmu;
    if (pthread_mutex_init(&p->uris_mu, NULL) != 0)  goto fail_qcv;
    if (pthread_mutex_init(&p->out_mu, NULL) != 0)   goto fail_umu;

    if (pthread_create(&p->worker, NULL, worker_main, p) != 0) goto fail_omu;

    return p;

fail_omu: pthread_mutex_destroy(&p->out_mu);
fail_umu: pthread_mutex_destroy(&p->uris_mu);
fail_qcv: pthread_cond_destroy(&p->queue_cv);
fail_qmu: pthread_mutex_destroy(&p->queue_mu);
fail:
    free(p->lime_bin);
    free(p);
    return NULL;
}

void lsp_diagnostics_async_request(lsp_diagnostics_async *p,
                                    const char *uri,
                                    const char *text, size_t text_len) {
    if (!p || !uri) return;

    /* Bump generation -- any in-flight worker for this URI now has
    ** a stale generation and its result will be dropped. */
    uint64_t gen = bump_uri_gen(p, uri);

    work_item *w = calloc(1, sizeof(*w));
    if (!w) return;
    w->uri = strdup(uri);
    if (!w->uri) { free(w); return; }
    w->gen = gen;
    if (text && text_len > 0) {
        w->text = malloc(text_len + 1);
        if (!w->text) { free(w->uri); free(w); return; }
        memcpy(w->text, text, text_len);
        w->text[text_len] = '\0';
        w->text_len = text_len;
    }
    /* w->text == NULL here means "publish empty diagnostics" which
    ** is what didClose wants. */

    pthread_mutex_lock(&p->queue_mu);
    if (p->queue_tail) p->queue_tail->next = w; else p->queue_head = w;
    p->queue_tail = w;
    pthread_cond_signal(&p->queue_cv);
    pthread_mutex_unlock(&p->queue_mu);
}

void lsp_diagnostics_async_destroy(lsp_diagnostics_async *p) {
    if (!p) return;

    /* Tell the worker to drain and exit. */
    pthread_mutex_lock(&p->queue_mu);
    p->shutdown = 1;
    pthread_cond_signal(&p->queue_cv);
    pthread_mutex_unlock(&p->queue_mu);

    pthread_join(p->worker, NULL);

    /* Drain any remaining queued items. */
    while (p->queue_head) {
        work_item *w = p->queue_head;
        p->queue_head = w->next;
        free(w->uri);
        free(w->text);
        free(w);
    }

    /* Free URI slots. */
    for (int i = 0; i < ASYNC_MAX_URIS; i++) {
        free(p->uris[i].uri);
    }

    pthread_mutex_destroy(&p->queue_mu);
    pthread_cond_destroy(&p->queue_cv);
    pthread_mutex_destroy(&p->uris_mu);
    pthread_mutex_destroy(&p->out_mu);
    free(p->lime_bin);
    free(p);
}

#endif /* POSIX */
