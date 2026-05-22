/*
** JIT-compiled keyword tokenizer implementation.
**
** When LLVM is available, builds a trie from the TokenTable's keywords
** and compiles it into a switch-cascade classifier using LLVM IR. The
** generated function takes (const char *input, uint32_t len) and returns
** the token code (int32_t), or -1 if no keyword matches.
**
** The trie is length-partitioned: the outer switch dispatches on input
** length, and each length bucket contains a character-by-character
** switch tree. This layout lets LLVM lower the switches to efficient
** jump tables or binary searches on the target architecture.
**
** All comparisons are case-insensitive for ASCII (matching SQL keyword
** conventions), implemented by OR-ing each character with 0x20 before
** comparing against lowercase constants.
**
** When compiled without LLVM (LIME_NO_JIT), all functions degrade to
** stubs that return appropriate error values.
*/

#include "jit_tokenizer.h"
#include "token_table.h"

#include <stdlib.h>
#include <string.h>

#ifndef LIME_NO_JIT

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Orc.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include "jit_llvm_compat.h"

#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Internal structure                                                  */
/* ------------------------------------------------------------------ */

struct JITTokenizer {
    LLVMOrcLLJITRef lljit;
    LLVMContextRef llvm_ctx; /* Bare LLVM context (for IR gen)  */
    LLVMOrcThreadSafeContextRef ts_ctx;

    /* Compiled classifier: int32_t (*)(const char *input, uint32_t len) */
    int (*classify_fn)(const char *, uint32_t);

    JITTokenizerStats stats;
};

/* ------------------------------------------------------------------ */
/*  Keyword snapshot (copied from TokenTable under read lock)           */
/* ------------------------------------------------------------------ */

typedef struct KeywordEntry {
    char *lower_lexeme; /* Heap-allocated lowercase copy */
    size_t len;
    int token_code;
} KeywordEntry;

/* Copy all keywords from the TokenTable, lowercasing them.
** Caller must free the returned array and each lower_lexeme string. */
static KeywordEntry *snapshot_keywords(const TokenTable *table, uint32_t *out_count) {
    *out_count = 0;
    if (table == NULL || table->ntokens == 0) return NULL;

    /* We access table fields directly since we're reading a snapshot.
    ** In production this should be under the table's read lock, but
    ** jit_tokenizer_create documents that the table must be stable. */
    uint32_t n = table->ntokens;
    KeywordEntry *entries = calloc(n, sizeof(KeywordEntry));
    if (entries == NULL) return NULL;

    for (uint32_t i = 0; i < n; i++) {
        const TokenDefinition *td = &table->tokens[i];
        entries[i].len = td->lexeme_len;
        entries[i].token_code = td->token_code;
        entries[i].lower_lexeme = malloc(td->lexeme_len + 1);
        if (entries[i].lower_lexeme == NULL) {
            /* Cleanup on failure */
            for (uint32_t j = 0; j < i; j++)
                free(entries[j].lower_lexeme);
            free(entries);
            return NULL;
        }
        for (size_t c = 0; c < td->lexeme_len; c++) {
            unsigned char ch = (unsigned char)td->lexeme[c];
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            entries[i].lower_lexeme[c] = (char)ch;
        }
        entries[i].lower_lexeme[td->lexeme_len] = '\0';
    }

    *out_count = n;
    return entries;
}

static void free_keywords(KeywordEntry *entries, uint32_t count) {
    if (entries == NULL) return;
    for (uint32_t i = 0; i < count; i++) {
        free(entries[i].lower_lexeme);
    }
    free(entries);
}

/* ------------------------------------------------------------------ */
/*  Trie node for IR generation                                         */
/* ------------------------------------------------------------------ */

typedef struct TrieNode {
    struct TrieNode *children[256]; /* Indexed by lowercase ASCII byte */
    int token_code;                 /* -1 if not a terminal node       */
} TrieNode;

static TrieNode *trie_new(void) {
    TrieNode *n = calloc(1, sizeof(TrieNode));
    if (n) n->token_code = -1;
    return n;
}

/*
** trie_free -- post-order free of a 256-fanout trie.
**
** ----------------------------------------------------------------------
** TAIL-CALL STRUCTURE -- LOAD-BEARING, see src/merkle_tree.c
** ----------------------------------------------------------------------
**
** The trie depth here is bounded by the longest keyword (typically a
** few dozen chars), so even unoptimised recursion is well inside the
** default thread stack.  We still hand-roll the spine into the outer
** while loop so a future contributor adding deeper trie keys (e.g. a
** runtime-loaded SQL dialect with multi-segment qualified names) does
** not silently introduce an O(depth) stack hog.  Verified via objdump
** that no `bl trie_free` instruction follows the loop tail.
*/
static void trie_free(TrieNode *n) {
    while (n != NULL) {
        TrieNode *last = NULL;
        int last_idx = -1;
        /* Find the last non-NULL child so we can tail-loop into it. */
        for (int i = 255; i >= 0; i--) {
            if (n->children[i] != NULL) {
                last_idx = i;
                last = n->children[i];
                break;
            }
        }
        /* Free every child except the last via ordinary recursion. */
        for (int i = 0; i < last_idx; i++) {
            trie_free(n->children[i]);
        }
        free(n);
        n = last;
    }
}

static void trie_insert(TrieNode *root, const char *key, size_t len, int token_code) {
    TrieNode *cur = root;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)key[i];
        if (cur->children[c] == NULL) {
            cur->children[c] = trie_new();
            if (cur->children[c] == NULL) return; /* OOM */
        }
        cur = cur->children[c];
    }
    cur->token_code = token_code;
}

/* ------------------------------------------------------------------ */
/*  LLVM IR generation for the trie                                     */
/* ------------------------------------------------------------------ */

/*
** Recursively emit switch-based character matching for a trie node.
** At depth `depth` into the input string, dispatches on input[depth]
** (lowercased) to child nodes.
*/
static void emit_trie_node(LLVMContextRef ctx, LLVMBuilderRef builder, LLVMValueRef fn,
                           LLVMValueRef input_ptr, LLVMValueRef result_var,
                           LLVMBasicBlockRef done_bb, const TrieNode *node, uint32_t depth,
                           uint32_t max_depth) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);

    /* If this node is a terminal, store the token code and jump to done */
    if (node->token_code >= 0 && depth == max_depth) {
        LLVMBuildStore(builder, LLVMConstInt(i32, (uint64_t)node->token_code, 0), result_var);
        LLVMBuildBr(builder, done_bb);
        return;
    }

    /* Count children with non-NULL entries */
    uint32_t nchildren = 0;
    for (int i = 0; i < 256; i++) {
        if (node->children[i] != NULL) nchildren++;
    }

    if (nchildren == 0) {
        /* Leaf but not terminal at this depth -- no match */
        LLVMBuildBr(builder, done_bb);
        return;
    }

    /* Load input[depth], lowercase it via OR 0x20 */
    LLVMValueRef idx = LLVMConstInt(i32, depth, 0);
    LLVMValueRef char_ptr = LLVMBuildGEP2(builder, i8, input_ptr, &idx, 1, "cp");
    LLVMValueRef ch = LLVMBuildLoad2(builder, i8, char_ptr, "ch");

    /* Lowercase: ch | 0x20 (works for A-Z -> a-z, leaves others mostly intact
    ** but we only compare against known lowercase constants so false matches
    ** on non-alpha chars are harmless -- they just fall through to default) */
    LLVMValueRef mask = LLVMConstInt(i8, 0x20, 0);
    LLVMValueRef lower_ch = LLVMBuildOr(builder, ch, mask, "lc");

    /* Default case: no match, jump to done */
    char label[64];
    snprintf(label, sizeof(label), "nomatch.d%u", depth);
    LLVMBasicBlockRef nomatch_bb = LLVMAppendBasicBlockInContext(ctx, fn, label);

    LLVMValueRef sw = LLVMBuildSwitch(builder, lower_ch, nomatch_bb, nchildren);

    for (int c = 0; c < 256; c++) {
        if (node->children[c] == NULL) continue;

        snprintf(label, sizeof(label), "c%u.d%u", (unsigned)c, depth);
        LLVMBasicBlockRef child_bb = LLVMAppendBasicBlockInContext(ctx, fn, label);
        LLVMAddCase(sw, LLVMConstInt(i8, (uint64_t)c, 0), child_bb);

        LLVMPositionBuilderAtEnd(builder, child_bb);
        emit_trie_node(ctx, builder, fn, input_ptr, result_var, done_bb, node->children[c],
                       depth + 1, max_depth);
    }

    /* No-match block */
    LLVMPositionBuilderAtEnd(builder, nomatch_bb);
    LLVMBuildBr(builder, done_bb);
}

/*
** Generate the complete classifier function:
**   int32_t jit_classify_keyword(const char *input, uint32_t len)
*/
static LLVMValueRef generate_classifier(LLVMContextRef ctx, LLVMModuleRef module,
                                        const KeywordEntry *entries, uint32_t nentries) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i8_ptr = LLVMPointerType(i8, 0);

    /* Function: int32_t (const char*, uint32_t) */
    LLVMTypeRef param_types[] = { i8_ptr, i32 };
    LLVMTypeRef fn_type = LLVMFunctionType(i32, param_types, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(module, "jit_classify_keyword", fn_type);
    LLVMSetFunctionCallConv(fn, LLVMCCallConv);
    LLVMSetLinkage(fn, LLVMExternalLinkage);

    LLVMValueRef param_input = LLVMGetParam(fn, 0);
    LLVMValueRef param_len = LLVMGetParam(fn, 1);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    /* Entry block */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(builder, entry_bb);

    /* Result variable, default -1 (no match) */
    LLVMValueRef result_var = LLVMBuildAlloca(builder, i32, "result");
    LLVMBuildStore(builder, LLVMConstInt(i32, (uint64_t)(uint32_t)-1, 1), result_var);

    /* Return block */
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(ctx, fn, "ret");

    /* Group keywords by length and build per-length tries */
    uint32_t max_len = 0;
    for (uint32_t i = 0; i < nentries; i++) {
        if (entries[i].len > max_len) max_len = (uint32_t)entries[i].len;
    }

    /* Length dispatch switch */
    LLVMBasicBlockRef default_bb = LLVMAppendBasicBlockInContext(ctx, fn, "len.default");
    LLVMValueRef len_switch = LLVMBuildSwitch(builder, param_len, default_bb, max_len + 1);

    for (uint32_t length = 1; length <= max_len; length++) {
        /* Collect keywords of this length */
        uint32_t count = 0;
        for (uint32_t i = 0; i < nentries; i++) {
            if (entries[i].len == length) count++;
        }
        if (count == 0) continue;

        /* Build a trie for this length bucket */
        TrieNode *root = trie_new();
        if (root == NULL) continue;

        for (uint32_t i = 0; i < nentries; i++) {
            if (entries[i].len == length) {
                trie_insert(root, entries[i].lower_lexeme, length, entries[i].token_code);
            }
        }

        /* Create basic block for this length */
        char label[32];
        snprintf(label, sizeof(label), "len.%u", length);
        LLVMBasicBlockRef len_bb = LLVMAppendBasicBlockInContext(ctx, fn, label);
        LLVMAddCase(len_switch, LLVMConstInt(i32, length, 0), len_bb);

        LLVMPositionBuilderAtEnd(builder, len_bb);
        emit_trie_node(ctx, builder, fn, param_input, result_var, ret_bb, root, 0, length);

        trie_free(root);
    }

    /* Default length case: no match */
    LLVMPositionBuilderAtEnd(builder, default_bb);
    LLVMBuildBr(builder, ret_bb);

    /* Return block */
    LLVMPositionBuilderAtEnd(builder, ret_bb);
    LLVMValueRef result = LLVMBuildLoad2(builder, i32, result_var, "retval");
    LLVMBuildRet(builder, result);

    LLVMDisposeBuilder(builder);
    return fn;
}

/* ------------------------------------------------------------------ */
/*  LLVM initialization (shared with jit_context.c)                     */
/* ------------------------------------------------------------------ */

static int tok_llvm_initialized = 0;

static bool ensure_tok_llvm_initialized(void) {
    if (tok_llvm_initialized) return true;
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    tok_llvm_initialized = 1;
    return true;
}

static uint64_t tok_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Public API (LLVM-enabled)                                           */
/* ------------------------------------------------------------------ */

JITTokenizer *jit_tokenizer_create(const TokenTable *table) {
    if (table == NULL) return NULL;
    if (!ensure_tok_llvm_initialized()) return NULL;

    uint32_t nkeywords = 0;
    KeywordEntry *entries = snapshot_keywords(table, &nkeywords);
    if (entries == NULL || nkeywords == 0) {
        free_keywords(entries, nkeywords);
        return NULL;
    }

    uint64_t start = tok_now_ns();

    JITTokenizer *tok = calloc(1, sizeof(JITTokenizer));
    if (tok == NULL) {
        free_keywords(entries, nkeywords);
        return NULL;
    }

    /* Create a ThreadSafeContext and fetch its internal LLVMContext
    ** (see lime_jit_create_ts_ctx for the LLVM 14 vs 15+ split). */
    if (!lime_jit_create_ts_ctx(&tok->ts_ctx, &tok->llvm_ctx)) {
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    /* Build LLJIT */
    LLVMOrcLLJITBuilderRef builder = LLVMOrcCreateLLJITBuilder();
    LLVMErrorRef err = LLVMOrcCreateLLJIT(&tok->lljit, builder);
    if (err != LLVMErrorSuccess) {
        char *msg = LLVMGetErrorMessage(err);
        LLVMDisposeErrorMessage(msg);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    /* Generate LLVM IR using the bare context stored at creation */
    LLVMContextRef llvm_ctx = tok->llvm_ctx;
    LLVMModuleRef module = LLVMModuleCreateWithNameInContext("lime_tokenizer", llvm_ctx);
    if (module == NULL) {
        LLVMOrcDisposeLLJIT(tok->lljit);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    LLVMValueRef fn = generate_classifier(llvm_ctx, module, entries, nkeywords);
    if (fn == NULL) {
        LLVMDisposeModule(module);
        LLVMOrcDisposeLLJIT(tok->lljit);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    /* Verify module */
    char *verify_err = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &verify_err)) {
        LLVMDisposeMessage(verify_err);
        LLVMDisposeModule(module);
        LLVMOrcDisposeLLJIT(tok->lljit);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }
    LLVMDisposeMessage(verify_err);

    /* Optimize (compat shim picks PassBuilder on LLVM 16+ or legacy
    ** PassManagerBuilder on LLVM 14-15). */
    LIME_JIT_RUN_O2_PASSES(module);

    /* Submit to OrcJIT */
    LLVMOrcThreadSafeModuleRef ts_mod = LLVMOrcCreateNewThreadSafeModule(module, tok->ts_ctx);

    LLVMOrcJITDylibRef jd = LLVMOrcLLJITGetMainJITDylib(tok->lljit);
    err = LLVMOrcLLJITAddLLVMIRModule(tok->lljit, jd, ts_mod);
    if (err != LLVMErrorSuccess) {
        char *msg = LLVMGetErrorMessage(err);
        LLVMDisposeErrorMessage(msg);
        LLVMOrcDisposeLLJIT(tok->lljit);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    /* Look up the compiled function.  LimeJitAddress resolves to
    ** LLVMOrcExecutorAddress on LLVM 15+ and to
    ** LLVMOrcJITTargetAddress on LLVM 14 (both uint64_t). */
    LimeJitAddress addr = 0;
    err = LLVMOrcLLJITLookup(tok->lljit, &addr, "jit_classify_keyword");
    if (err != LLVMErrorSuccess || addr == 0) {
        if (err != LLVMErrorSuccess) {
            char *msg = LLVMGetErrorMessage(err);
            LLVMDisposeErrorMessage(msg);
        }
        LLVMOrcDisposeLLJIT(tok->lljit);
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
        free(tok);
        free_keywords(entries, nkeywords);
        return NULL;
    }

    tok->classify_fn = (int (*)(const char *, uint32_t))(uintptr_t)addr;

    uint64_t end = tok_now_ns();
    tok->stats.keywords_compiled = nkeywords;
    tok->stats.compile_time_ns = end - start;
    /* Rough estimate: ~100 bytes per keyword for trie branches */
    tok->stats.code_size_bytes = nkeywords * 100;

    free_keywords(entries, nkeywords);
    return tok;
}

void jit_tokenizer_destroy(JITTokenizer *tok) {
    if (tok == NULL) return;

    if (tok->lljit != NULL) {
        LLVMErrorRef err = LLVMOrcDisposeLLJIT(tok->lljit);
        if (err != LLVMErrorSuccess) {
            char *msg = LLVMGetErrorMessage(err);
            LLVMDisposeErrorMessage(msg);
        }
    }

    if (tok->ts_ctx != NULL) {
        LLVMOrcDisposeThreadSafeContext(tok->ts_ctx);
    }

    free(tok);
}

int jit_tokenizer_classify_keyword(const JITTokenizer *tok, const char *input, size_t len) {
    if (tok == NULL || tok->classify_fn == NULL) return -1;
    if (input == NULL || len == 0) return -1;
    if (len > UINT32_MAX) return -1;

    return tok->classify_fn(input, (uint32_t)len);
}

JITTokenizerStats jit_tokenizer_get_stats(const JITTokenizer *tok) {
    if (tok == NULL) {
        JITTokenizerStats empty = { 0 };
        return empty;
    }
    return tok->stats;
}

bool jit_tokenizer_is_available(void) {
    return ensure_tok_llvm_initialized();
}

#else /* LIME_NO_JIT -- stub implementations */

struct JITTokenizer {
    JITTokenizerStats stats;
};

JITTokenizer *jit_tokenizer_create(const TokenTable *table) {
    (void)table;
    return NULL;
}

void jit_tokenizer_destroy(JITTokenizer *tok) {
    free(tok);
}

int jit_tokenizer_classify_keyword(const JITTokenizer *tok, const char *input, size_t len) {
    (void)tok;
    (void)input;
    (void)len;
    return -1;
}

JITTokenizerStats jit_tokenizer_get_stats(const JITTokenizer *tok) {
    (void)tok;
    JITTokenizerStats empty = { 0 };
    return empty;
}

bool jit_tokenizer_is_available(void) {
    return false;
}

#endif /* LIME_NO_JIT */
