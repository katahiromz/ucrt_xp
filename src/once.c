/*
 * once.c - legacy one-time initialization. InitOnceExecuteOnce is a
 * Vista+ API (it does not exist on any XP service pack), so this version
 * uses nothing but InterlockedCompareExchange + Sleep and therefore runs
 * on every XP, RTM included.
 *
 * Waiters poll instead of blocking, which is fine for short initializers
 * but wasteful for long ones. For a blocking implementation with the
 * native callback shape, use ucrt_xp_InitOnceExecuteOnce (sync.c).
 *
 * State machine per UCRT_XP_ONCE.state:
 *   0 = not started
 *   1 = a thread is currently running fn()
 *   2 = fn() has completed
 */
#include "internal.h"

#define ONCE_NOT_STARTED 0
#define ONCE_RUNNING      1
#define ONCE_DONE         2

__declspec(dllexport) BOOL __cdecl ucrt_xp_once(
    UCRT_XP_ONCE *once, UCRT_XP_ONCE_FN fn, void *param)
{
    if (!once || !fn) {
        return FALSE;
    }

    if (InterlockedCompareExchange(&once->state, ONCE_RUNNING, ONCE_NOT_STARTED)
        == ONCE_NOT_STARTED) {
        /* We won the race: run the initializer exactly once. */
        BOOL ok = fn(param);
        if (ok) {
            InterlockedExchange(&once->state, ONCE_DONE);
        } else {
            /* Allow a future caller to retry on failure, mirroring
             * InitOnceExecuteOnce's own semantics for a failed callback. */
            InterlockedExchange(&once->state, ONCE_NOT_STARTED);
        }
        return ok;
    }

    /* Someone else is running or has run it: spin-wait for completion.
     * We deliberately avoid a kernel wait object here to keep UCRT_XP_ONCE
     * a single LONG (cheap to embed in any struct, no handle lifetime to
     * manage). Contention on a one-time-init path is expected to be rare
     * and short-lived. */
    {
        /* Sleep(0) only yields to threads of equal or higher priority, so
         * if the initializing thread has LOWER priority, spinning on it
         * alone can starve it. After a short burst, sleep for real. */
        int spins = 0;
        while (once->state == ONCE_RUNNING) {
            if (++spins < 64) {
                Sleep(0);
            } else {
                Sleep(1);
            }
        }
    }
    return (once->state == ONCE_DONE);
}
