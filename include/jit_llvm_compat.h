/*
** jit_llvm_compat.h -- LLVM C API compatibility shims for Lime.
**
** Lime declares a minimum LLVM version of 14.  Three LLVM C APIs that
** Lime uses changed across that range, so we hide the version split
** here:
**
**   * New PassBuilder API (LLVMRunPasses, LLVMPassBuilderOptionsRef)
**     is present from LLVM 16 onward.  On LLVM 14-15 we fall back to
**     the legacy LLVMPassManagerBuilder + LLVMPassManager combo.
**     LLVMPassManagerBuilder itself was removed from the C API in
**     LLVM 17, so these are complementary: legacy for 14-15, modern
**     for 16+.
**
**   * LLVMOrcCreateNewThreadSafeContextFromLLVMContext() was added in
**     LLVM 15.  On LLVM 14 we have to create the ThreadSafeContext
**     first and then extract its internal LLVMContextRef.  In both
**     cases the ThreadSafeContext owns the LLVMContext, so teardown
**     is just LLVMOrcDisposeThreadSafeContext().
**
**   * LLVMOrcLLJITLookup() takes an LLVMOrcJITTargetAddress * in
**     LLVM 14 and an LLVMOrcExecutorAddress * in LLVM 15+.  Both are
**     uint64_t, just differently named.  LimeJitAddress aliases to
**     whichever name the current LLVM exposes.
**
** Any code that runs the O2 pass pipeline, creates a thread-safe
** context, or calls LLVMOrcLLJITLookup should use the helpers defined
** here instead of the underlying LLVM symbols directly.
*/

#ifndef LIME_JIT_LLVM_COMPAT_H
#define LIME_JIT_LLVM_COMPAT_H

#ifndef LIME_NO_JIT

/* LLVM_VERSION_MAJOR / _MINOR / _PATCH come from here.  The header
** lives under the C++ include path but contains only #defines, so it
** is usable from C translation units. */
#include <llvm/Config/llvm-config.h>

#include <stdbool.h>
#include <stddef.h>

#include <llvm-c/Core.h>
#include <llvm-c/Orc.h>

#if LLVM_VERSION_MAJOR >= 16
#include <llvm-c/Transforms/PassBuilder.h>
#else
#include <llvm-c/Transforms/PassManagerBuilder.h>
#endif

/*
** LIME_JIT_RUN_O2_PASSES(module)
**
** Run the -O2 optimisation pipeline over the given LLVM module.
** Evaluates `module` exactly once.  Void-returning; does not signal
** failure (the legacy path has no status channel, and the new path's
** LLVMErrorRef return is ignored to keep the call-site symmetrical --
** revisit if we ever care about pipeline setup errors).
*/
#if LLVM_VERSION_MAJOR >= 16

#define LIME_JIT_RUN_O2_PASSES(module)                                                             \
    do {                                                                                           \
        LLVMModuleRef _lime_mod = (module);                                                        \
        LLVMPassBuilderOptionsRef _lime_opts = LLVMCreatePassBuilderOptions();                     \
        LLVMErrorRef _lime_err = LLVMRunPasses(_lime_mod, "default<O2>", NULL, _lime_opts);        \
        if (_lime_err != LLVMErrorSuccess) {                                                       \
            char *_lime_msg = LLVMGetErrorMessage(_lime_err);                                      \
            LLVMDisposeErrorMessage(_lime_msg);                                                    \
        }                                                                                          \
        LLVMDisposePassBuilderOptions(_lime_opts);                                                 \
    } while (0)

#else /* LLVM 14 / 15 -- legacy PassManagerBuilder */

#define LIME_JIT_RUN_O2_PASSES(module)                                                             \
    do {                                                                                           \
        LLVMModuleRef _lime_mod = (module);                                                        \
        LLVMPassManagerBuilderRef _lime_pmb = LLVMPassManagerBuilderCreate();                      \
        LLVMPassManagerBuilderSetOptLevel(_lime_pmb, 2);                                           \
        LLVMPassManagerRef _lime_pm = LLVMCreatePassManager();                                     \
        LLVMPassManagerBuilderPopulateModulePassManager(_lime_pmb, _lime_pm);                      \
        LLVMRunPassManager(_lime_pm, _lime_mod);                                                   \
        LLVMDisposePassManager(_lime_pm);                                                          \
        LLVMPassManagerBuilderDispose(_lime_pmb);                                                  \
    } while (0)

#endif /* LLVM_VERSION_MAJOR >= 16 */

/*
** LimeJitAddress -- the uint64_t-sized type LLVMOrcLLJITLookup writes
** its result into.  Use this type at every LLVMOrcLLJITLookup call
** site; it resolves to whichever name the current LLVM headers
** expose.
*/
#if LLVM_VERSION_MAJOR >= 15
typedef LLVMOrcExecutorAddress LimeJitAddress;
#else
typedef LLVMOrcJITTargetAddress LimeJitAddress;
#endif

/*
** lime_jit_create_ts_ctx()
**
** Create a ThreadSafeContext and return both it and its internal
** LLVMContextRef.  On success, *ts_ctx_out owns the returned
** llvm_ctx_out -- callers must dispose only *ts_ctx_out during
** teardown.  Returns false on allocation failure, in which case
** neither out-parameter is touched.
*/
static inline bool lime_jit_create_ts_ctx(LLVMOrcThreadSafeContextRef *ts_ctx_out,
                                          LLVMContextRef *llvm_ctx_out) {
#if LLVM_VERSION_MAJOR >= 15
    LLVMContextRef llvm_ctx = LLVMContextCreate();
    if (llvm_ctx == NULL) {
        return false;
    }
    LLVMOrcThreadSafeContextRef ts_ctx = LLVMOrcCreateNewThreadSafeContextFromLLVMContext(llvm_ctx);
    if (ts_ctx == NULL) {
        LLVMContextDispose(llvm_ctx);
        return false;
    }
#else
    /* LLVM 14: no way to wrap an externally-created context; the
    ** ThreadSafeContext creates and owns its own LLVMContext, which
    ** we then fetch via the getter. */
    LLVMOrcThreadSafeContextRef ts_ctx = LLVMOrcCreateNewThreadSafeContext();
    if (ts_ctx == NULL) {
        return false;
    }
    LLVMContextRef llvm_ctx = LLVMOrcThreadSafeContextGetContext(ts_ctx);
    if (llvm_ctx == NULL) {
        LLVMOrcDisposeThreadSafeContext(ts_ctx);
        return false;
    }
#endif
    *ts_ctx_out = ts_ctx;
    *llvm_ctx_out = llvm_ctx;
    return true;
}

#endif /* !LIME_NO_JIT */

#endif /* LIME_JIT_LLVM_COMPAT_H */
