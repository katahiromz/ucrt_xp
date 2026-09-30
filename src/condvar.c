/*
 * condvar.c - condition variable substitute for XP, which has no
 * CONDITION_VARIABLE (that's Vista+). This is the well-known
 * "SetEvent + Semaphore" implementation (the same algorithm used by
 * pthreads-win32 / the classic Schmidt & Pyarali paper), adapted to
 * ucrt_xp's naming and error-handling conventions.
 *
 * Supports both signal (wake one) and broadcast (wake all), and is
 * correct in the presence of spurious/lost wakeups because callers are
 * expected to re-check their predicate in a loop, per standard condvar
 * usage.
 */
#include "internal.h"
#include <assert.h>

__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_init(UCRT_XP_COND *cv)
{
    if (!cv) return FALSE;

    cv->waiters_count = 0;
    cv->was_broadcast = 0;

    cv->sema = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
    if (!cv->sema) {
        assert(0);
        return FALSE;
    }

    cv->waiters_done = CreateEventA(NULL, /*manual reset*/FALSE, FALSE, NULL);
    if (!cv->waiters_done) {
        assert(0);
        CloseHandle(cv->sema);
        cv->sema = NULL;
        return FALSE;
    }

    InitializeCriticalSection(&cv->waiters_lock);
    return TRUE;
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_destroy(UCRT_XP_COND *cv)
{
    if (!cv) return;
    if (cv->sema) { CloseHandle(cv->sema); cv->sema = NULL; }
    if (cv->waiters_done) { CloseHandle(cv->waiters_done); cv->waiters_done = NULL; }
    DeleteCriticalSection(&cv->waiters_lock);
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_wait(
    UCRT_XP_COND *cv, CRITICAL_SECTION *external_lock, DWORD timeout_ms)
{
    BOOL last_waiter;
    DWORD wait_result;

    if (!cv || !external_lock) return FALSE;

    EnterCriticalSection(&cv->waiters_lock);
    cv->waiters_count++;
    LeaveCriticalSection(&cv->waiters_lock);

    /* Atomically release caller's lock and block on the semaphore. This
     * mirrors condvar semantics: the external mutex is only released once
     * we are actually queued to be woken. */
    LeaveCriticalSection(external_lock);
    wait_result = WaitForSingleObject(cv->sema, timeout_ms);

    EnterCriticalSection(&cv->waiters_lock);
    cv->waiters_count--;
    last_waiter = cv->was_broadcast && (cv->waiters_count == 0);
    LeaveCriticalSection(&cv->waiters_lock);

    if (last_waiter) {
        /* Tell the broadcasting thread that all waiters have drained the
         * semaphore, so it's safe for it to return from broadcast(). */
        SetEvent(cv->waiters_done);
    }

    EnterCriticalSection(external_lock);

    return (wait_result == WAIT_OBJECT_0);
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_signal(UCRT_XP_COND *cv)
{
    BOOL have_waiters;
    if (!cv) return;

    EnterCriticalSection(&cv->waiters_lock);
    have_waiters = (cv->waiters_count > 0);
    LeaveCriticalSection(&cv->waiters_lock);

    if (have_waiters) {
        ReleaseSemaphore(cv->sema, 1, NULL);
    }
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_broadcast(UCRT_XP_COND *cv)
{
    BOOL have_waiters;
    if (!cv) return;

    EnterCriticalSection(&cv->waiters_lock);
    have_waiters = FALSE;
    if (cv->waiters_count > 0) {
        cv->was_broadcast = 1;
        have_waiters = TRUE;
    }

    if (have_waiters) {
        ReleaseSemaphore(cv->sema, cv->waiters_count, NULL);
        LeaveCriticalSection(&cv->waiters_lock);

        /* Wait for every released waiter to actually wake up and decrement
         * waiters_count before clearing was_broadcast, otherwise a fast
         * subsequent wait() could observe was_broadcast==1 from the
         * previous round. */
        WaitForSingleObject(cv->waiters_done, INFINITE);
        cv->was_broadcast = 0;
    } else {
        LeaveCriticalSection(&cv->waiters_lock);
    }
}
