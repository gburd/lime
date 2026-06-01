/*
** lime_time.h -- portable monotonic / wall-clock nanosecond timer.
**
** On POSIX (Linux, macOS, FreeBSD, Solaris, ...) this header is
** a thin wrapper over clock_gettime(CLOCK_MONOTONIC, ...) /
** clock_gettime(CLOCK_REALTIME, ...).  On Windows it uses
** QueryPerformanceCounter / QueryPerformanceFrequency for the
** monotonic clock and GetSystemTimePreciseAsFileTime for the
** wall-clock equivalent.
**
** Both functions return a uint64_t nanosecond value.  The
** monotonic clock starts at an arbitrary epoch (only diffs are
** meaningful); the realtime clock returns nanoseconds since the
** Unix epoch.
**
** Provided as static inline so the compiler can elide the call
** to nothing more than the underlying syscall / Win32 API
** invocation on the hot path (timing benchmarks).  Including
** this header from a translation unit incurs only the
** declarations -- no extra link-time code.
*/
#ifndef LIME_TIME_H
#define LIME_TIME_H

#include <stdint.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* MinGW already supplies <time.h> with `struct timespec`,
** `clock_gettime`, and the CLOCK_* constants -- using the same
** definitions as the rest of the toolchain.  Including our shim
** there triggers `redefinition of 'struct timespec'` errors
** because <sys/types.h> on MinGW pulls them in transitively.
** Detect MinGW and skip the shim; only MSVC (and clang-cl) need
** the locally-defined POSIX-style stubs. */
/* MinGW supplies the full POSIX layer (clock_gettime + CLOCK_*
** + struct timespec).  UCRT supplies struct timespec but not
** clock_gettime.  Legacy MSVCRT supplies neither. */
#if defined(__MINGW32__)
#include <time.h>
#else
#include <time.h>

/* Define struct timespec only when neither MinGW nor UCRT have
** already done it.  _UCRT is defined by clang/MSVC against the
** Universal CRT (Win10 1809+) which supplies the C11 struct. */
#if !defined(_TIMESPEC_DEFINED) && !defined(__struct_timespec_defined) \
    && !defined(_UCRT)
struct timespec {
    long long tv_sec;
    long      tv_nsec;
};
#define _TIMESPEC_DEFINED
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME  0
#endif

/* clock_gettime is NOT in UCRT or legacy MSVC; provide it.
** MinGW's POSIX layer already does, so the !defined(__MINGW32__)
** outer guard prevents a redefinition there. */
static inline int clock_gettime(int clk_id, struct timespec *tp) {
    if (clk_id == CLOCK_MONOTONIC) {
        LARGE_INTEGER counter, frequency;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&frequency);
        long long secs = counter.QuadPart / frequency.QuadPart;
        long long rem  = counter.QuadPart % frequency.QuadPart;
        tp->tv_sec  = secs;
        tp->tv_nsec = (long)((rem * 1000000000LL) / frequency.QuadPart);
        return 0;
    } else if (clk_id == CLOCK_REALTIME) {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        unsigned long long t =
            ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        if (t < 116444736000000000ULL) { tp->tv_sec = 0; tp->tv_nsec = 0; return 0; }
        unsigned long long ns_epoch = (t - 116444736000000000ULL) * 100ULL;
        tp->tv_sec  = (long long)(ns_epoch / 1000000000ULL);
        tp->tv_nsec = (long)(ns_epoch % 1000000000ULL);
        return 0;
    }
    return -1;
}
#endif /* !__MINGW32__ */
#else
#include <time.h>
#endif

static inline uint64_t lime_now_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    /* Compute (counter * 1e9) / frequency without overflowing
    ** 64-bit on long-uptime systems: split the multiply. */
    uint64_t secs   = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    uint64_t rem    = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return secs * 1000000000ULL
           + (rem * 1000000000ULL) / (uint64_t)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static inline uint64_t lime_now_realtime_ns(void) {
#if defined(_WIN32)
    /* GetSystemTimePreciseAsFileTime returns 100-ns intervals
    ** since 1601-01-01.  116444736000000000 is the offset to
    ** 1970-01-01 (the Unix epoch). */
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    if (t < 116444736000000000ULL) return 0;
    return (t - 116444736000000000ULL) * 100ULL;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/*
** LIME_USE(x) -- prevent the compiler from optimising away a
** computation whose result the program doesn't otherwise need.
** Useful in benchmarks where we time the cost of producing a
** value we don't actually consume.
**
** On gcc/clang the canonical pattern is `__asm__ volatile(""
** : : "r"(x))` which forces x into a register and stops the
** dead-code eliminator at that point.  MSVC has no equivalent
** inline-asm syntax in C; instead we route x through a
** `volatile` storage location which the compiler cannot
** optimise across.
*/
#if defined(__GNUC__) || defined(__clang__)
#define LIME_USE(x) __asm__ volatile("" : : "r"(x))
#elif defined(_MSC_VER)
#define LIME_USE(x) do { volatile long long _lime_use_sink = (long long)(x); \
                         (void)_lime_use_sink; } while (0)
#else
#define LIME_USE(x) ((void)(x))
#endif

/*
** LIME_LIKELY(x) / LIME_UNLIKELY(x) -- branch-prediction hints.
**
** On gcc/clang these wrap __builtin_expect, which has been used
** in src/parse_engine.c's per-token hot path since commit
** 602c305 ("perf(x86): hoist JIT pointer cache + branch hints").
**
** MSVC has no __builtin_expect in C.  C++23 introduces the
** [[likely]] / [[unlikely]] attributes but C has no equivalent.
** On MSVC the macros expand to plain (x): the optimizer's
** built-in heuristics do a reasonable job on the parser's hot
** path even without explicit hints.
*/
#if defined(__GNUC__) || defined(__clang__)
#define LIME_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LIME_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIME_LIKELY(x)   (x)
#define LIME_UNLIKELY(x) (x)
#endif

/*
** LIME_HOT / LIME_COLD -- function-placement attributes.
**
** gcc/clang group LIME_HOT functions into .text.hot and LIME_COLD
** functions into .text.unlikely, so the linker keeps frequently-
** called code dense (better icache density) and rarely-called
** code (error paths, destructors) out of the way.  No-op on MSVC
** and other compilers.
**
** Per .agent/notes/c-perf-audit.md item #4: lime had zero usage
** of these annotations despite multiple obvious candidates.
*/
#if defined(__GNUC__) || defined(__clang__)
#define LIME_HOT  __attribute__((hot))
#define LIME_COLD __attribute__((cold))
#else
#define LIME_HOT
#define LIME_COLD
#endif

/*
** LIME_RESTRICT -- pointer-aliasing-free attribute.
**
** Marks a pointer parameter as not aliasing any other pointer in
** the same function -- the compiler can promote loads to registers
** across stores through other pointers.  C99 has the `restrict`
** keyword; gcc/clang accept both `restrict` and `__restrict__`.
** MSVC has `__restrict` (single underscore).  No-op elsewhere.
*/
#if defined(__GNUC__) || defined(__clang__)
#define LIME_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define LIME_RESTRICT __restrict
#else
#define LIME_RESTRICT
#endif

#endif /* LIME_TIME_H */
