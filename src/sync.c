/*
 * sync.c - Vista-style condition variable + one-time init, with runtime
 * dispatch between the OS's native implementation and an XP fallback.
 *
 * WHY THIS FILE EXISTS
 *   CONDITION_VARIABLE, SleepConditionVariableCS, WakeConditionVariable,
 *   WakeAllConditionVariable and InitOnceExecuteOnce were all introduced
 *   in Windows Vista. None of them exist on XP (any service pack).
 *   This file exposes the SAME shapes under a ucrt_xp_ prefix:
 *
 *       - no init/destroy calls; a zero-filled object is a valid one
 *         (UCRT_XP_CONDITION_VARIABLE_INIT / UCRT_XP_INIT_ONCE_STATIC_INIT)
 *       - SleepConditionVariableCS takes a CRITICAL_SECTION and returns
 *         FALSE + ERROR_TIMEOUT on timeout, exactly like the native one
 *       - InitOnceExecuteOnce takes the native 3-argument callback
 *
 *   so code written for Vista+ ports by renaming, and (see
 *   ucrt_xp_compat.h, UCRT_XP_USE_VISTA_NAMES) often by not even that.
 *
 * DISPATCH
 *   On first use we look the five APIs up in kernel32.dll with
 *   GetProcAddress. If ALL five are present (Vista+), every call is
 *   forwarded to the OS. If any is missing (XP), the fallback below is
 *   used. The decision is made once per process and never changes, so
 *   every object is always driven by the same implementation. Both
 *   implementations treat an all-zero object as "fresh", which is what
 *   makes the shared static initializers work.
 *
 * FALLBACK DESIGN (per-waiter event queue)
 *   Each waiter puts a small node on its own stack, appends it to the
 *   condition variable's FIFO queue, releases the caller's lock, and
 *   blocks on a private event. Wake-one pops the head and sets THAT
 *   waiter's event; wake-all pops everyone. Because a wakeup is aimed at
 *   a specific waiter, a thread that starts waiting later can never
 *   steal it ("stolen wakeup"), which is the well-known flaw of
 *   semaphore-counting condvar emulations. A timed-out waiter removes
 *   itself under the same lock the waker uses, so exactly one of
 *   "timed out" / "was woken" is decided, never both and never neither.
 *
 *   Locking: one process-wide CRITICAL_SECTION (g_lock) guards every
 *   fallback queue, the event pool, and fallback once-state. It is a
 *   LEAF lock: while holding it we never take a caller's lock, so it
 *   cannot take part in a deadlock cycle. It is shared by all fallback
 *   objects; that is deliberate (an XP-only path with short critical
 *   sections) and is what lets a zero-filled object need no per-object
 *   setup.
 *
 *   Memory ordering: fast-path reads of a condition variable's queue
 *   head and of once-state rely on x86's strong store/load ordering,
 *   which is the only architecture this project targets (see README).
 */
#include "internal.h"

/* ------------------------------------------------------------------ */
/* Native (Vista+) entry points, resolved at runtime                    */
/* ------------------------------------------------------------------ */

typedef VOID (WINAPI *PFN_InitializeConditionVariable)(PVOID cv);
typedef BOOL (WINAPI *PFN_SleepConditionVariableCS)(PVOID cv, PCRITICAL_SECTION cs, DWORD ms);
typedef VOID (WINAPI *PFN_WakeConditionVariable)(PVOID cv);
typedef BOOL (WINAPI *PFN_InitOnceExecuteOnce)(PVOID once, PVOID fn, PVOID param, PVOID *ctx);

static PFN_InitializeConditionVariable p_InitializeConditionVariable;
static PFN_SleepConditionVariableCS    p_SleepConditionVariableCS;
static PFN_WakeConditionVariable       p_WakeConditionVariable;
static PFN_WakeConditionVariable       p_WakeAllConditionVariable;
static PFN_InitOnceExecuteOnce         p_InitOnceExecuteOnce;

static CRITICAL_SECTION g_lock;
static volatile LONG g_state = 0;          /* 0 = not set up, 1 = setting up, 2 = ready */
static volatile LONG g_native_available = 0;
static volatile LONG g_force_fallback = 0; /* test hook, see below */

#define POOL_MAX 32
static HANDLE g_ev_pool[POOL_MAX];
static int    g_ev_pool_n = 0;

static void sync_ensure(void)
{
    if (g_state == 2) {
        return;
    }
    if (InterlockedCompareExchange(&g_state, 1, 0) == 0) {
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        InitializeCriticalSection(&g_lock);
        if (k32) {
            p_InitializeConditionVariable = (PFN_InitializeConditionVariable)(void *)GetProcAddress(k32, "InitializeConditionVariable");
            p_SleepConditionVariableCS    = (PFN_SleepConditionVariableCS)(void *)GetProcAddress(k32, "SleepConditionVariableCS");
            p_WakeConditionVariable       = (PFN_WakeConditionVariable)(void *)GetProcAddress(k32, "WakeConditionVariable");
            p_WakeAllConditionVariable    = (PFN_WakeConditionVariable)(void *)GetProcAddress(k32, "WakeAllConditionVariable");
            p_InitOnceExecuteOnce         = (PFN_InitOnceExecuteOnce)(void *)GetProcAddress(k32, "InitOnceExecuteOnce");
        }
        if (p_InitializeConditionVariable && p_SleepConditionVariableCS &&
            p_WakeConditionVariable && p_WakeAllConditionVariable &&
            p_InitOnceExecuteOnce) {
            InterlockedExchange(&g_native_available, 1);
        }
        InterlockedExchange(&g_state, 2); /* full barrier: publishes everything above */
    } else {
        while (g_state != 2) {
            Sleep(1); /* another thread is inside the one-time setup above */
        }
    }
}

static int use_native(void)
{
    return g_native_available && !g_force_fallback;
}

/* TEST HOOK (internal, not exported): force the XP fallback even on a
 * Vista+ machine so both implementations can be exercised on one box.
 * Only call while no condition variable / once object is in use. */
void ucrt_xp__sync_force_fallback(int on)
{
    sync_ensure();
    InterlockedExchange(&g_force_fallback, on ? 1 : 0);
}

/* Called from DllMain(PROCESS_DETACH). */
void ucrt_xp__sync_cleanup(void)
{
    int i;
    if (g_state != 2) {
        return;
    }
    for (i = 0; i < g_ev_pool_n; i++) {
        CloseHandle(g_ev_pool[i]);
    }
    g_ev_pool_n = 0;
    DeleteCriticalSection(&g_lock);
    InterlockedExchange(&g_state, 0);
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_sync_is_native(void)
{
    sync_ensure();
    return use_native() ? TRUE : FALSE;
}

/* ------------------------------------------------------------------ */
/* Fallback: waiter queue                                               */
/* ------------------------------------------------------------------ */

typedef struct Waiter {
    struct Waiter *next;
    struct Waiter *prev;
    HANDLE ev;      /* private manual-reset event, from the pool */
    int queued;     /* 1 while linked into a queue; guarded by g_lock */
} Waiter;

/* The queue head lives in the object's single pointer-sized slot.
 * Circular doubly-linked list: head->prev is the tail, so append and
 * remove-anywhere are O(1). All functions below need g_lock held. */
static void q_push(Waiter **head, Waiter *w)
{
    w->queued = 1;
    if (!*head) {
        w->next = w->prev = w;
        *head = w;
    } else {
        Waiter *h = *head;
        w->prev = h->prev;
        w->next = h;
        h->prev->next = w;
        h->prev = w;
    }
}

static void q_remove(Waiter **head, Waiter *w)
{
    if (w->next == w) {
        *head = NULL;
    } else {
        w->prev->next = w->next;
        w->next->prev = w->prev;
        if (*head == w) {
            *head = w->next;
        }
    }
    w->queued = 0;
}

static void wake_one_locked(Waiter **head)
{
    Waiter *w = *head;
    if (w) {
        HANDLE ev = w->ev;
        q_remove(head, w);
        /* SetEvent happens while g_lock is held: a concurrently timing-out
         * waiter must take g_lock before deciding, so it will see either
         * "still queued" or "removed AND event already set". After this
         * call we must not touch *w - its owner may already be gone. */
        SetEvent(ev);
    }
}

static void wake_all_locked(Waiter **head)
{
    while (*head) {
        wake_one_locked(head);
    }
}

static HANDLE ev_acquire(void)
{
    HANDLE h = NULL;
    EnterCriticalSection(&g_lock);
    if (g_ev_pool_n > 0) {
        h = g_ev_pool[--g_ev_pool_n];
    }
    LeaveCriticalSection(&g_lock);
    if (!h) {
        h = CreateEventA(NULL, TRUE /*manual reset*/, FALSE, NULL);
    }
    return h;
}

static void ev_release(HANDLE h)
{
    /* Safe to reset and reuse: by now this waiter is out of every queue,
     * so nobody can SetEvent it again. */
    ResetEvent(h);
    EnterCriticalSection(&g_lock);
    if (g_ev_pool_n < POOL_MAX) {
        g_ev_pool[g_ev_pool_n++] = h;
        h = NULL;
    }
    LeaveCriticalSection(&g_lock);
    if (h) {
        CloseHandle(h);
    }
}

/*
 * Precondition: the calling thread holds `cs` (once). If cs == &g_lock
 * the caller also holds g_lock (used by the once fallback below).
 * Postcondition: `cs` is held again, on every return path.
 * Returns TRUE if woken, FALSE on timeout (last error ERROR_TIMEOUT) or
 * on resource failure (last error set by the failing call).
 */
static BOOL wait_core(Waiter **head, CRITICAL_SECTION *cs, DWORD ms)
{
    Waiter w;
    DWORD r, err = 0;
    BOOL woken;
    int cs_is_g = (cs == &g_lock);

    w.ev = ev_acquire();
    if (!w.ev) {
        return FALSE; /* CreateEvent set the last error; cs still held */
    }

    /* Enqueue BEFORE releasing the caller's lock. Any waker that could
     * matter must take the caller's lock (to change the predicate)
     * after we release it, so it is guaranteed to find us queued. */
    if (!cs_is_g) EnterCriticalSection(&g_lock);
    q_push(head, &w);
    if (!cs_is_g) LeaveCriticalSection(&g_lock);

    LeaveCriticalSection(cs);
    r = WaitForSingleObject(w.ev, ms);
    woken = (r == WAIT_OBJECT_0);
    if (r == WAIT_FAILED) {
        err = GetLastError();
    }

    if (!woken) {
        EnterCriticalSection(&g_lock);
        if (w.queued) {
            q_remove(head, &w);   /* timed out / failed: nobody chose us */
        } else {
            /* A waker dequeued us and set our event in the race window
             * between the timeout and taking g_lock: it is a normal wake. */
            woken = TRUE;
        }
        LeaveCriticalSection(&g_lock);
    }

    ev_release(w.ev);
    EnterCriticalSection(cs);

    if (!woken) {
        SetLastError(r == WAIT_TIMEOUT ? ERROR_TIMEOUT : err);
    }
    return woken;
}

/* ------------------------------------------------------------------ */
/* Condition variable (public)                                          */
/* ------------------------------------------------------------------ */

__declspec(dllexport) void __cdecl ucrt_xp_InitializeConditionVariable(
    UCRT_XP_CONDITION_VARIABLE *cv)
{
    if (!cv) return;
    sync_ensure();
    if (use_native()) {
        p_InitializeConditionVariable(cv);
    } else {
        cv->Ptr = NULL;
    }
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_SleepConditionVariableCS(
    UCRT_XP_CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD timeout_ms)
{
    if (!cv || !cs) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    sync_ensure();
    if (use_native()) {
        return p_SleepConditionVariableCS(cv, cs, timeout_ms);
    }
    return wait_core((Waiter **)&cv->Ptr, cs, timeout_ms);
}

__declspec(dllexport) void __cdecl ucrt_xp_WakeConditionVariable(
    UCRT_XP_CONDITION_VARIABLE *cv)
{
    if (!cv) return;
    sync_ensure();
    if (use_native()) {
        p_WakeConditionVariable(cv);
        return;
    }
    if (*(void * volatile *)&cv->Ptr == NULL) {
        return; /* nobody queued: skip the shared lock entirely */
    }
    EnterCriticalSection(&g_lock);
    wake_one_locked((Waiter **)&cv->Ptr);
    LeaveCriticalSection(&g_lock);
}

__declspec(dllexport) void __cdecl ucrt_xp_WakeAllConditionVariable(
    UCRT_XP_CONDITION_VARIABLE *cv)
{
    if (!cv) return;
    sync_ensure();
    if (use_native()) {
        p_WakeAllConditionVariable(cv);
        return;
    }
    if (*(void * volatile *)&cv->Ptr == NULL) {
        return;
    }
    EnterCriticalSection(&g_lock);
    wake_all_locked((Waiter **)&cv->Ptr);
    LeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ */
/* One-time initialization (public)                                     */
/* ------------------------------------------------------------------ */

/* Fallback state lives in once->Ptr, mirroring the native encoding:
 *     NULL             not started
 *     1                a thread is running the callback
 *     (ctx | 2)        finished successfully; ctx is the stored context
 * so a context value must have its low two bits clear, same as native. */
#define ONCE_RUNNING   ((ULONG_PTR)1)
#define ONCE_DONE_BIT  ((ULONG_PTR)2)
#define ONCE_BITS      ((ULONG_PTR)3)

static Waiter *g_once_waiters = NULL; /* one queue shared by all fallback onces */

__declspec(dllexport) void __cdecl ucrt_xp_InitOnceInitialize(UCRT_XP_INIT_ONCE *once)
{
    if (once) {
        once->Ptr = NULL;
    }
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_InitOnceExecuteOnce(
    UCRT_XP_INIT_ONCE *once, UCRT_XP_INIT_ONCE_FN fn, PVOID param, PVOID *context)
{
    ULONG_PTR v;
    PVOID ctx = NULL;
    BOOL ok;
    DWORD cb_error;

    if (!once || !fn) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    sync_ensure();
    if (use_native()) {
        return p_InitOnceExecuteOnce(once, (PVOID)fn, param, context);
    }

    /* Fast path once initialized: no lock. */
    v = *(volatile ULONG_PTR *)&once->Ptr;
    if (v & ONCE_DONE_BIT) {
        if (context) *context = (PVOID)(v & ~ONCE_BITS);
        return TRUE;
    }

    EnterCriticalSection(&g_lock);
    for (;;) {
        v = (ULONG_PTR)once->Ptr;
        if (v & ONCE_DONE_BIT) {
            LeaveCriticalSection(&g_lock);
            if (context) *context = (PVOID)(v & ~ONCE_BITS);
            return TRUE;
        }
        if (v == 0) {
            break; /* we are the one to run it */
        }
        /* Someone else is running it: sleep (no spinning) until any
         * once-completion broadcast, then re-check our own state. */
        wait_core(&g_once_waiters, &g_lock, INFINITE);
    }
    once->Ptr = (PVOID)ONCE_RUNNING;
    LeaveCriticalSection(&g_lock);

    ok = fn(once, param, &ctx);
    cb_error = GetLastError();
    if (ok && ((ULONG_PTR)ctx & ONCE_BITS)) {
        ok = FALSE;
        cb_error = ERROR_INVALID_PARAMETER;
    }

    EnterCriticalSection(&g_lock);
    once->Ptr = ok ? (PVOID)((ULONG_PTR)ctx | ONCE_DONE_BIT) : NULL; /* NULL = retry allowed */
    wake_all_locked(&g_once_waiters);
    LeaveCriticalSection(&g_lock);

    if (ok) {
        if (context) *context = ctx;
    } else {
        SetLastError(cb_error);
    }
    return ok;
}
