/*
 * condvar.c - legacy init/destroy-style condition variable API.
 *
 * These five functions are part of the shipped ABI, so they stay, but
 * they no longer contain an implementation of their own: each one
 * forwards to the Vista-style functions in sync.c. That means they get
 * the same behavior as everything else:
 *
 *   - On Vista+ the operating system's CONDITION_VARIABLE does the work.
 *   - On XP, sync.c's per-waiter event queue does.
 *
 * (An earlier version of this file emulated condition variables with a
 * semaphore + event pair. That scheme cannot aim a wakeup at a specific
 * waiter, so a thread arriving late could consume a wakeup meant for one
 * that was already waiting, and its comment claimed the lock release and
 * the block were atomic when they are not. It was removed rather than
 * kept as the XP fallback for those reasons.)
 *
 * The struct layout of UCRT_XP_COND is unchanged (ABI rule 1); only its
 * first pointer-sized field is used, as the condition-variable slot.
 */
#include "internal.h"

__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_init(UCRT_XP_COND *cv)
{
    if (!cv) return FALSE;
    ZeroMemory(cv, sizeof(*cv));
    ucrt_xp_InitializeConditionVariable((UCRT_XP_CONDITION_VARIABLE *)cv);
    return TRUE;
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_destroy(UCRT_XP_COND *cv)
{
    /* Nothing to release: no kernel objects are created any more, and
     * the native CONDITION_VARIABLE has no destructor either. Destroying
     * an object that still has waiters is a caller bug, as before. */
    (void)cv;
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_wait(
    UCRT_XP_COND *cv, CRITICAL_SECTION *external_lock, DWORD timeout_ms)
{
    if (!cv) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return ucrt_xp_SleepConditionVariableCS(
        (UCRT_XP_CONDITION_VARIABLE *)cv, external_lock, timeout_ms);
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_signal(UCRT_XP_COND *cv)
{
    if (!cv) return;
    ucrt_xp_WakeConditionVariable((UCRT_XP_CONDITION_VARIABLE *)cv);
}

__declspec(dllexport) void __cdecl ucrt_xp_cond_broadcast(UCRT_XP_COND *cv)
{
    if (!cv) return;
    ucrt_xp_WakeAllConditionVariable((UCRT_XP_CONDITION_VARIABLE *)cv);
}
