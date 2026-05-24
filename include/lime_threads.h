/*
** lime_threads.h -- minimal pthread shim for Windows portability.
**
** On POSIX (Linux, macOS, FreeBSD, Solaris, etc.) this header
** is just a passthrough to <pthread.h>; existing code that uses
** pthread_rwlock_t / pthread_mutex_t / pthread_t compiles
** unchanged.
**
** On Windows / MSVC, where <pthread.h> is not in the platform
** SDK, this header provides the pthread API surface Lime
** actually uses, implemented on top of Win32 SRWLock,
** CRITICAL_SECTION, and the classic thread API.  The mapping
** is pragmatic, not faithful: error returns are 0 on success
** (not POSIX EBUSY/EINVAL details) since Lime's call sites
** only check `!= 0` to decide whether to abort.
**
** Subset implemented (= what Lime needs as of v0.2.x):
**
**   pthread_rwlock_t        -> SRWLOCK
**   pthread_rwlock_init     -> InitializeSRWLock + 0
**   pthread_rwlock_destroy  -> 0 (SRWLOCK has no destructor)
**   pthread_rwlock_rdlock   -> AcquireSRWLockShared
**   pthread_rwlock_wrlock   -> AcquireSRWLockExclusive
**   pthread_rwlock_unlock   -> ReleaseSRWLockShared (read)
**                              or ReleaseSRWLockExclusive (write)
**     -- callers MUST pair lock/unlock by hand on Windows
**        because SRWLOCK is two distinct release paths.  Lime
**        wraps with two thin macros: LIME_RWLOCK_RDUNLOCK and
**        LIME_RWLOCK_WRUNLOCK.  POSIX users use the same
**        macros (which expand to plain pthread_rwlock_unlock)
**        for source-compatibility.
**
**   pthread_mutex_t         -> CRITICAL_SECTION
**   pthread_mutex_init      -> InitializeCriticalSection + 0
**   pthread_mutex_destroy   -> DeleteCriticalSection + 0
**   pthread_mutex_lock      -> EnterCriticalSection + 0
**   pthread_mutex_unlock    -> LeaveCriticalSection + 0
**
**   pthread_t               -> HANDLE
**   pthread_attr_t          -> int (unused)
**   pthread_attr_init       -> 0
**   pthread_attr_setdetachstate -> 0
**   pthread_attr_destroy    -> 0
**   pthread_create          -> _beginthreadex (then closes the
**                              handle if detached)
**   pthread_detach          -> CloseHandle + 0
**   PTHREAD_CREATE_DETACHED -> 1
*/
#ifndef LIME_THREADS_H
#define LIME_THREADS_H

#if !defined(_WIN32)

/* POSIX passthrough: include the real <pthread.h>.  The
** preceding #ifndef LIME_THREADS_H guard makes this safe; the
** earlier sed sweep that converted Lime-source `#include
** <pthread.h>` to `#include "lime_threads.h"` would have
** loop-included this very header, but the system pthread
** include below is intentional and breaks the recursion. */
#include <pthread.h>

/* On POSIX the rd/wr unlock macros expand to the same
** pthread_rwlock_unlock to keep call sites identical between
** platforms. */
#define LIME_RWLOCK_RDUNLOCK(lock_ptr) pthread_rwlock_unlock(lock_ptr)
#define LIME_RWLOCK_WRUNLOCK(lock_ptr) pthread_rwlock_unlock(lock_ptr)

#else /* _WIN32 */

#include <windows.h>
#include <process.h>

typedef SRWLOCK             pthread_rwlock_t;
typedef CRITICAL_SECTION    pthread_mutex_t;
typedef HANDLE              pthread_t;
typedef int                 pthread_attr_t;
typedef int                 pthread_rwlockattr_t;
typedef int                 pthread_mutexattr_t;

#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_CREATE_JOINABLE 0

/* --- rwlock --- */
static __inline int pthread_rwlock_init(pthread_rwlock_t *lock,
                                        const pthread_rwlockattr_t *a) {
    (void)a;
    InitializeSRWLock(lock);
    return 0;
}
static __inline int pthread_rwlock_destroy(pthread_rwlock_t *lock) {
    (void)lock;
    return 0;
}
static __inline int pthread_rwlock_rdlock(pthread_rwlock_t *lock) {
    AcquireSRWLockShared(lock);
    return 0;
}
static __inline int pthread_rwlock_wrlock(pthread_rwlock_t *lock) {
    AcquireSRWLockExclusive(lock);
    return 0;
}
/* Generic unlock is ambiguous on SRWLOCK -- use the
** LIME_RWLOCK_RDUNLOCK / LIME_RWLOCK_WRUNLOCK macros instead. */
#define LIME_RWLOCK_RDUNLOCK(lock_ptr) ReleaseSRWLockShared(lock_ptr)
#define LIME_RWLOCK_WRUNLOCK(lock_ptr) ReleaseSRWLockExclusive(lock_ptr)
/* For source compatibility with code that uses pthread_rwlock_unlock
** generically: assume exclusive (write) lock since that's the
** stricter invariant; the rd-locked branch must use
** LIME_RWLOCK_RDUNLOCK explicitly.  Lime's own code is being
** updated to use the LIME_* macros directly. */
static __inline int pthread_rwlock_unlock(pthread_rwlock_t *lock) {
    /* SRWLOCK has no "release whatever you held" operation.
    ** Calling this on an SRWLOCK is wrong; we ABI-stub it as
    ** unlock-exclusive which matches the most common Lime
    ** call site (a write-locked path).  Read-locked paths in
    ** Lime have been audited and converted to LIME_RWLOCK_RDUNLOCK. */
    ReleaseSRWLockExclusive(lock);
    return 0;
}

/* --- mutex --- */
static __inline int pthread_mutex_init(pthread_mutex_t *m,
                                       const pthread_mutexattr_t *a) {
    (void)a;
    InitializeCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    DeleteCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_lock(pthread_mutex_t *m) {
    EnterCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}

/* --- thread --- */
static __inline int pthread_attr_init(pthread_attr_t *a) {
    *a = PTHREAD_CREATE_JOINABLE;
    return 0;
}
static __inline int pthread_attr_destroy(pthread_attr_t *a) {
    (void)a;
    return 0;
}
static __inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s) {
    *a = s;
    return 0;
}
typedef unsigned (__stdcall *lime_thread_fn)(void *);
static __inline int pthread_create(pthread_t *t,
                                   const pthread_attr_t *attr,
                                   void *(*start)(void *),
                                   void *arg) {
    /* Win32's _beginthreadex expects unsigned (__stdcall *)(void *)
    ** but POSIX expects void *(*)(void *).  We pun the function
    ** pointer via a void * cast -- callers ignore the return
    ** value of start_routine (Lime never inspects pthread_join
    ** return values), so the ABI mismatch on the return type
    ** is not observed. */
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0,
                                     (lime_thread_fn)(void *)start,
                                     arg, 0, NULL);
    if (h == NULL) return 1;
    *t = h;
    if (attr != NULL && *attr == PTHREAD_CREATE_DETACHED) {
        CloseHandle(h);
    }
    return 0;
}
static __inline int pthread_detach(pthread_t t) {
    CloseHandle(t);
    return 0;
}
static __inline int pthread_join(pthread_t t, void **rv) {
    WaitForSingleObject(t, INFINITE);
    if (rv != NULL) *rv = NULL;
    CloseHandle(t);
    return 0;
}

#endif /* _WIN32 */

#endif /* LIME_THREADS_H */
