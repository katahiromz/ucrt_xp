/*
 * test_sync.c - exercises the Vista-style condition variable and
 * InitOnceExecuteOnce in BOTH implementations:
 *
 *   pass 1: whatever the OS provides (native on Vista+/Wine, fallback on XP)
 *   pass 2: the XP fallback, forced on (only meaningful when pass 1 was native)
 *
 * The test code is written with the Vista names on purpose, through
 * ucrt_xp_compat.h, so it also proves that a Vista-style program builds
 * and behaves the same either way.
 */
#define UCRT_XP_USE_VISTA_NAMES
#include "ucrt_xp_compat.h"
#include <stdio.h>
#include <stdlib.h>

/* Internal test hook from sync.c; this program links the static library. */
extern void ucrt_xp__sync_force_fallback(int on);

static int g_failures = 0;
static int g_pass = 0;

/* Pass 0 relies purely on static initialization (no init call at all).
 * Later passes switch implementation, and an object last driven by the
 * OS implementation may hold OS-private bits, so we reset it first, the
 * way a real program would re-initialize an object it re-uses. */
#define REARM_CV(cv)   do { if (g_pass > 0) InitializeConditionVariable(cv); } while (0)

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static int owns(CRITICAL_SECTION *cs)
{
    return (DWORD)(ULONG_PTR)cs->OwningThread == GetCurrentThreadId();
}

/* Fail the whole run instead of hanging forever if something deadlocks. */
static void wait_all_or_die(HANDLE *h, DWORD n, DWORD ms, const char *what)
{
    DWORD r = WaitForMultipleObjects(n, h, TRUE, ms);
    if (r == WAIT_TIMEOUT) {
        printf("  [FAIL] DEADLOCK/HANG in: %s\n", what);
        fflush(stdout);
        ExitProcess(2);
    }
}

/* ------------------------------------------------------------------ */
/* 1. static init + handshake                                          */
/* ------------------------------------------------------------------ */
static CONDITION_VARIABLE g_cv1 = CONDITION_VARIABLE_INIT;   /* no init call */
static CRITICAL_SECTION   g_cs1;
static int                g_ready1;

static DWORD WINAPI t_handshake(LPVOID p)
{
    (void)p;
    Sleep(50);
    EnterCriticalSection(&g_cs1);
    g_ready1 = 1;
    LeaveCriticalSection(&g_cs1);
    WakeConditionVariable(&g_cv1);
    return 0;
}

static void test_handshake(void)
{
    HANDLE th;
    REARM_CV(&g_cv1);
    InitializeCriticalSection(&g_cs1);
    g_ready1 = 0;
    th = CreateThread(NULL, 0, t_handshake, NULL, 0, NULL);
    EnterCriticalSection(&g_cs1);
    while (!g_ready1) {
        CHECK(SleepConditionVariableCS(&g_cv1, &g_cs1, 10000));
        CHECK(owns(&g_cs1));
    }
    LeaveCriticalSection(&g_cs1);
    wait_all_or_die(&th, 1, 10000, "handshake");
    CloseHandle(th);
    DeleteCriticalSection(&g_cs1);
    printf("  [ok] static-initialized cv, handshake, CS re-held on return\n");
}

/* ------------------------------------------------------------------ */
/* 2. timeout                                                          */
/* ------------------------------------------------------------------ */
static void test_timeout(void)
{
    CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
    CRITICAL_SECTION cs;
    DWORD t0, dt;
    BOOL r;
    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
    t0 = GetTickCount();
    r = SleepConditionVariableCS(&cv, &cs, 150);
    dt = GetTickCount() - t0;
    CHECK(r == FALSE);
    CHECK(GetLastError() == ERROR_TIMEOUT);
    CHECK(dt >= 120 && dt < 2000);
    CHECK(owns(&cs));                       /* re-acquired on the timeout path */
    r = SleepConditionVariableCS(&cv, &cs, 0);   /* zero timeout must not hang */
    CHECK(r == FALSE);
    CHECK(owns(&cs));
    LeaveCriticalSection(&cs);
    DeleteCriticalSection(&cs);
    printf("  [ok] timeout: FALSE + ERROR_TIMEOUT, ~%lums, lock re-held\n", (unsigned long)dt);
}

/* ------------------------------------------------------------------ */
/* 3. Wake wakes exactly one; WakeAll wakes the rest                   */
/* ------------------------------------------------------------------ */
static CONDITION_VARIABLE g_cv3 = CONDITION_VARIABLE_INIT;
static CRITICAL_SECTION   g_cs3;
static volatile LONG      g_woke3, g_waiting3;
static int                g_tokens3;

static DWORD WINAPI t_waiter3(LPVOID p)
{
    (void)p;
    EnterCriticalSection(&g_cs3);
    g_waiting3++;
    while (g_tokens3 == 0) {
        SleepConditionVariableCS(&g_cv3, &g_cs3, INFINITE);
    }
    g_tokens3--;
    g_woke3++;
    LeaveCriticalSection(&g_cs3);
    return 0;
}

static void test_wake_counts(void)
{
    HANDLE th[3];
    int i;
    REARM_CV(&g_cv3);
    InitializeCriticalSection(&g_cs3);
    g_tokens3 = 0; g_woke3 = 0; g_waiting3 = 0;
    for (i = 0; i < 3; i++) th[i] = CreateThread(NULL, 0, t_waiter3, NULL, 0, NULL);
    for (;;) {                                /* all three inside the wait loop */
        LONG n;
        EnterCriticalSection(&g_cs3); n = g_waiting3; LeaveCriticalSection(&g_cs3);
        if (n == 3) break;
        Sleep(5);
    }
    Sleep(100);                               /* ...and actually blocked */

    EnterCriticalSection(&g_cs3); g_tokens3 = 1; LeaveCriticalSection(&g_cs3);
    WakeConditionVariable(&g_cv3);            /* one token, one wake */
    Sleep(300);
    EnterCriticalSection(&g_cs3);
    CHECK(g_woke3 == 1);
    g_tokens3 += 2;
    LeaveCriticalSection(&g_cs3);
    WakeAllConditionVariable(&g_cv3);
    wait_all_or_die(th, 3, 10000, "wake_counts");
    CHECK(g_woke3 == 3);
    for (i = 0; i < 3; i++) CloseHandle(th[i]);
    DeleteCriticalSection(&g_cs3);
    printf("  [ok] Wake wakes exactly one waiter, WakeAll the rest\n");
}

/* ------------------------------------------------------------------ */
/* 4. bounded queue stress using single Wake only                      */
/*    (any lost or stolen wakeup shows up as a hang or a wrong sum)    */
/* ------------------------------------------------------------------ */
#define QCAP      4
#define NPROD     4
#define NCONS     4
#define PER_PROD  5000

static CRITICAL_SECTION   g_qcs;
static CONDITION_VARIABLE g_not_full  = CONDITION_VARIABLE_INIT;
static CONDITION_VARIABLE g_not_empty = CONDITION_VARIABLE_INIT;
static int g_q[QCAP], g_qn, g_qhead, g_qtail, g_prod_done;
static __int64 g_sum_consumed;
static long    g_count_consumed;

static DWORD WINAPI t_producer(LPVOID p)
{
    int base = (int)(ULONG_PTR)p, i;
    for (i = 1; i <= PER_PROD; i++) {
        EnterCriticalSection(&g_qcs);
        while (g_qn == QCAP) {
            SleepConditionVariableCS(&g_not_full, &g_qcs, INFINITE);
        }
        g_q[g_qtail] = base + i;
        g_qtail = (g_qtail + 1) % QCAP;
        g_qn++;
        LeaveCriticalSection(&g_qcs);
        WakeConditionVariable(&g_not_empty);
    }
    EnterCriticalSection(&g_qcs);
    g_prod_done++;
    LeaveCriticalSection(&g_qcs);
    WakeAllConditionVariable(&g_not_empty);   /* let consumers notice the end */
    return 0;
}

static DWORD WINAPI t_consumer(LPVOID p)
{
    (void)p;
    for (;;) {
        int v;
        EnterCriticalSection(&g_qcs);
        while (g_qn == 0) {
            if (g_prod_done == NPROD) { LeaveCriticalSection(&g_qcs); return 0; }
            SleepConditionVariableCS(&g_not_empty, &g_qcs, INFINITE);
        }
        v = g_q[g_qhead];
        g_qhead = (g_qhead + 1) % QCAP;
        g_qn--;
        g_sum_consumed += v;
        g_count_consumed++;
        LeaveCriticalSection(&g_qcs);
        WakeConditionVariable(&g_not_full);
    }
}

static void test_bounded_queue(void)
{
    HANDLE th[NPROD + NCONS];
    int i;
    __int64 expect = 0;
    REARM_CV(&g_not_full);
    REARM_CV(&g_not_empty);
    InitializeCriticalSection(&g_qcs);
    g_qn = g_qhead = g_qtail = g_prod_done = 0;
    g_sum_consumed = 0; g_count_consumed = 0;
    for (i = 0; i < NPROD; i++) {
        int base = (i + 1) * 1000000, k;
        for (k = 1; k <= PER_PROD; k++) expect += base + k;
    }
    for (i = 0; i < NCONS; i++)
        th[i] = CreateThread(NULL, 0, t_consumer, NULL, 0, NULL);
    for (i = 0; i < NPROD; i++)
        th[NCONS + i] = CreateThread(NULL, 0, t_producer,
                                     (LPVOID)(ULONG_PTR)((i + 1) * 1000000), 0, NULL);
    wait_all_or_die(th, NPROD + NCONS, 60000, "bounded queue (4 producers / 4 consumers)");
    CHECK(g_count_consumed == NPROD * PER_PROD);
    CHECK(g_sum_consumed == expect);
    for (i = 0; i < NPROD + NCONS; i++) CloseHandle(th[i]);
    DeleteCriticalSection(&g_qcs);
    printf("  [ok] %d items through a %d-slot queue, 4P/4C, single Wake: none lost\n",
           NPROD * PER_PROD, QCAP);
}

/* ------------------------------------------------------------------ */
/* 5. timeouts racing against wakes                                    */
/* ------------------------------------------------------------------ */
static CONDITION_VARIABLE g_cv5 = CONDITION_VARIABLE_INIT;
static CRITICAL_SECTION   g_cs5;
static volatile LONG      g_stop5, g_lock_lost5;
static volatile LONG      g_timeouts5, g_wakes5;

static DWORD WINAPI t_racer(LPVOID p)
{
    int i;
    (void)p;
    for (i = 0; i < 1500; i++) {
        EnterCriticalSection(&g_cs5);
        if (SleepConditionVariableCS(&g_cv5, &g_cs5, (DWORD)(i % 3)))   /* 0,1,2 ms */ InterlockedIncrement(&g_wakes5);
        else                                             InterlockedIncrement(&g_timeouts5);
        if (!owns(&g_cs5)) InterlockedIncrement(&g_lock_lost5);
        LeaveCriticalSection(&g_cs5);
    }
    return 0;
}

static DWORD WINAPI t_waker(LPVOID p)
{
    (void)p;
    while (!g_stop5) {
        /* Paced to the waiters' ~1ms timeouts so that wakes land right at,
         * just before and just after the timeout instant. */
        WakeConditionVariable(&g_cv5);
        Sleep(1);
        WakeAllConditionVariable(&g_cv5);
        Sleep(1);
    }
    return 0;
}

static void test_timeout_wake_race(void)
{
    HANDLE th[4], waker;
    int i;
    REARM_CV(&g_cv5);
    InitializeCriticalSection(&g_cs5);
    g_stop5 = g_lock_lost5 = g_timeouts5 = g_wakes5 = 0;
    waker = CreateThread(NULL, 0, t_waker, NULL, 0, NULL);
    for (i = 0; i < 4; i++) th[i] = CreateThread(NULL, 0, t_racer, NULL, 0, NULL);
    wait_all_or_die(th, 4, 60000, "timeout/wake race");
    g_stop5 = 1;
    wait_all_or_die(&waker, 1, 10000, "waker shutdown");
    CHECK(g_lock_lost5 == 0);
    CHECK(g_timeouts5 + g_wakes5 == 4 * 1500);
    CHECK(g_timeouts5 > 100 && g_wakes5 > 100);   /* both outcomes really happened */
    for (i = 0; i < 4; i++) CloseHandle(th[i]);
    CloseHandle(waker);
    DeleteCriticalSection(&g_cs5);
    printf("  [ok] 6000 timed waits racing wakes: %ld woken, %ld timed out, no hang, lock always re-held\n",
           (long)g_wakes5, (long)g_timeouts5);
}

/* ------------------------------------------------------------------ */
/* 6. InitOnceExecuteOnce                                              */
/* ------------------------------------------------------------------ */
static INIT_ONCE g_io = INIT_ONCE_STATIC_INIT;
static volatile LONG g_io_calls;
static int g_io_payload = 1234;     /* aligned => low two bits clear */
static volatile LONG g_io_bad_ctx_seen;

static BOOL WINAPI io_slow_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param;
    InterlockedIncrement(&g_io_calls);
    Sleep(150);                      /* long enough that every racer arrives */
    *ctx = &g_io_payload;
    return TRUE;
}

static DWORD WINAPI t_io(LPVOID p)
{
    PVOID ctx = NULL;
    BOOL ok = InitOnceExecuteOnce(&g_io, io_slow_init, NULL, &ctx);
    if (!ok || ctx != &g_io_payload) InterlockedIncrement(&g_io_bad_ctx_seen);
    (void)p;
    return 0;
}

static BOOL WINAPI io_fail_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)ctx;
    (*(volatile LONG *)param)++;
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
}

static BOOL WINAPI io_ok_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)ctx;
    (*(volatile LONG *)param)++;
    return TRUE;
}

static BOOL WINAPI io_badctx_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param;
    *ctx = (PVOID)1;                 /* low bit set: not allowed */
    return TRUE;
}

static void test_initonce(int is_native)
{
    HANDLE th[8];
    int i;
    PVOID ctx = NULL;
    INIT_ONCE retry;
    volatile LONG fails = 0, oks = 0;
    BOOL r;

    if (g_pass > 0) InitOnceInitialize(&g_io);

    /* 8 racers, slow initializer: runs once, everybody sees the context */
    g_io_calls = 0; g_io_bad_ctx_seen = 0;
    for (i = 0; i < 8; i++) th[i] = CreateThread(NULL, 0, t_io, NULL, 0, NULL);
    wait_all_or_die(th, 8, 30000, "InitOnce racers");
    CHECK(g_io_calls == 1);
    CHECK(g_io_bad_ctx_seen == 0);
    for (i = 0; i < 8; i++) CloseHandle(th[i]);

    /* later calls take the fast path and still return the context */
    ctx = NULL;
    CHECK(InitOnceExecuteOnce(&g_io, io_slow_init, NULL, &ctx));
    CHECK(ctx == &g_io_payload);
    CHECK(g_io_calls == 1);

    /* failed initializer: FALSE, error preserved, and a retry is allowed */
    InitOnceInitialize(&retry);
    SetLastError(0);
    r = InitOnceExecuteOnce(&retry, io_fail_init, (PVOID)&fails, NULL);
    CHECK(r == FALSE);
    CHECK(GetLastError() == ERROR_INVALID_DATA);
    r = InitOnceExecuteOnce(&retry, io_ok_init, (PVOID)&oks, NULL);
    CHECK(r == TRUE);
    CHECK(fails == 1 && oks == 1);
    r = InitOnceExecuteOnce(&retry, io_ok_init, (PVOID)&oks, NULL);
    CHECK(r == TRUE && oks == 1);            /* already done: not run again */

    /* argument validation */
    CHECK(InitOnceExecuteOnce(NULL, io_ok_init, NULL, NULL) == FALSE);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(InitOnceExecuteOnce(&retry, NULL, NULL, NULL) == FALSE);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    if (!is_native) {
        /* Reserved-bit context: our fallback rejects it like native does.
         * (Not asserted against the native path: that is the OS's business.) */
        INIT_ONCE bad = INIT_ONCE_STATIC_INIT;
        CHECK(InitOnceExecuteOnce(&bad, io_badctx_init, NULL, NULL) == FALSE);
        CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    }
    printf("  [ok] InitOnce: once across 8 racers, context shared, failure retries, args checked\n");
}

/* ------------------------------------------------------------------ */
/* 7. legacy init/destroy API rides on the same machinery              */
/* ------------------------------------------------------------------ */
static UCRT_XP_COND      g_lcv;
static CRITICAL_SECTION  g_lcs;
static int               g_lready;

static DWORD WINAPI t_legacy(LPVOID p)
{
    (void)p;
    Sleep(50);
    EnterCriticalSection(&g_lcs);
    g_lready = 1;
    LeaveCriticalSection(&g_lcs);
    ucrt_xp_cond_signal(&g_lcv);
    return 0;
}

static void test_legacy_api(void)
{
    HANDLE th;
    BOOL r;
    InitializeCriticalSection(&g_lcs);
    CHECK(ucrt_xp_cond_init(&g_lcv));
    g_lready = 0;
    th = CreateThread(NULL, 0, t_legacy, NULL, 0, NULL);
    EnterCriticalSection(&g_lcs);
    while (!g_lready) CHECK(ucrt_xp_cond_wait(&g_lcv, &g_lcs, 10000));
    r = ucrt_xp_cond_wait(&g_lcv, &g_lcs, 50);       /* nobody will signal */
    CHECK(r == FALSE);
    LeaveCriticalSection(&g_lcs);
    wait_all_or_die(&th, 1, 10000, "legacy handshake");
    CloseHandle(th);
    ucrt_xp_cond_broadcast(&g_lcv);
    ucrt_xp_cond_destroy(&g_lcv);
    DeleteCriticalSection(&g_lcs);
    printf("  [ok] legacy ucrt_xp_cond_* wrappers: handshake + timeout\n");
}

/* ------------------------------------------------------------------ */

static void run_pass(const char *label, int force_fallback)
{
    int native;
    ucrt_xp__sync_force_fallback(force_fallback);
    native = ucrt_xp_sync_is_native() ? 1 : 0;
    printf("== %s  (implementation in use: %s) ==\n", label,
           native ? "OS-native" : "ucrt_xp fallback");
    test_handshake();
    test_timeout();
    test_wake_counts();
    test_bounded_queue();
    test_timeout_wake_race();
    test_initonce(native);
    test_legacy_api();
    g_pass++;
}

int main(void)
{
    int first_native;
    ucrt_xp_init(UCRT_XP_ABI_VERSION);

    first_native = ucrt_xp_sync_is_native() ? 1 : 0;
    run_pass("pass 1: as the OS provides", 0);
    if (first_native) {
        /* Reset the shared fixtures' state by construction: they are all
         * re-initialized at the start of each test. */
        run_pass("pass 2: XP fallback forced on", 1);
    } else {
        printf("(native APIs not present: pass 1 already ran the XP fallback)\n");
    }

    if (g_failures) {
        printf("\n%d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("\nAll sync tests passed.\n");
    return 0;
}
