/*
 * process.c - the small set of process-lifetime primitives real CRT
 * code tends to assume exist: a settable/readable errno, exit()/abort(),
 * and atexit() callback registration.
 *
 * errno here is per-thread (TLS-backed), which is what every CRT has
 * converged on since the mid-90s anyway (MSVCRT.DLL's errno was
 * historically process-global and a genuine multithreading hazard -
 * exactly the kind of inconsistency this whole project exists to paper
 * over). ucrt_xp itself doesn't set errno from its own functions in this
 * reference implementation (most of ucrt_xp's API reports errors via
 * return value / GetLastError instead, which is more precise on
 * Win32) - this is provided so ported code that reads/writes errno
 * directly keeps compiling and behaving sanely.
 */
#include "internal.h"

/* ------------------------------------------------------------------ */
/* errno                                                                */
/* ------------------------------------------------------------------ */

static DWORD g_errno_tls = TLS_OUT_OF_INDEXES;
static UCRT_XP_ONCE g_errno_tls_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_errno_tls(void *param)
{
    (void)param;
    g_errno_tls = TlsAlloc();
    return g_errno_tls != TLS_OUT_OF_INDEXES;
}

__declspec(dllexport) int* __cdecl ucrt_xp_errno_location(void)
{
    /* Returns a pointer to this thread's errno cell, lazily allocated -
     * the same "return a stable per-thread lvalue" pattern glibc's
     * __errno_location() uses, so callers can either call
     * ucrt_xp_get_errno()/set_errno() or, if porting code that expects
     * an lvalue `errno`, dereference this directly:
     *     #define errno (*ucrt_xp_errno_location())
     */
    int *cell;
    ucrt_xp_once(&g_errno_tls_once, init_errno_tls, NULL);
    if (g_errno_tls == TLS_OUT_OF_INDEXES) {
        static int fallback = 0; /* out of TLS slots: degrade to shared,
                                   * rather than crash on a NULL deref */
        return &fallback;
    }

    cell = (int *)TlsGetValue(g_errno_tls);
    if (!cell) {
        cell = (int *)ucrt_xp_malloc(sizeof(int));
        if (!cell) {
            static int fallback2 = 0;
            return &fallback2;
        }
        *cell = 0;
        TlsSetValue(g_errno_tls, cell);
    }
    return cell;
}

__declspec(dllexport) int __cdecl ucrt_xp_get_errno(void)
{
    return *ucrt_xp_errno_location();
}

__declspec(dllexport) void __cdecl ucrt_xp_set_errno(int value)
{
    *ucrt_xp_errno_location() = value;
}

/* ------------------------------------------------------------------ */
/* atexit / exit / abort                                               */
/* ------------------------------------------------------------------ */

#define UCRT_XP_MAX_ATEXIT 64

static UCRT_XP_ATEXIT_FN g_atexit_fns[UCRT_XP_MAX_ATEXIT];
static int g_atexit_count = 0;
static CRITICAL_SECTION g_atexit_lock;
static UCRT_XP_ONCE g_atexit_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_atexit_lock(void *param)
{
    (void)param;
    InitializeCriticalSection(&g_atexit_lock);
    return TRUE;
}

__declspec(dllexport) int __cdecl ucrt_xp_atexit(UCRT_XP_ATEXIT_FN fn)
{
    ucrt_xp_once(&g_atexit_once, init_atexit_lock, NULL);
    if (!fn) return -1;

    EnterCriticalSection(&g_atexit_lock);
    if (g_atexit_count >= UCRT_XP_MAX_ATEXIT) {
        LeaveCriticalSection(&g_atexit_lock);
        return -1; /* matches the standard's documented failure return */
    }
    g_atexit_fns[g_atexit_count++] = fn;
    LeaveCriticalSection(&g_atexit_lock);
    return 0;
}

static void run_atexit_handlers(void)
{
    int i;
    ucrt_xp_once(&g_atexit_once, init_atexit_lock, NULL);

    /* Run in reverse registration order, per the standard, and take a
     * private snapshot under the lock so a handler that itself calls
     * ucrt_xp_atexit() (legal, if unusual) can't corrupt the array out
     * from under this loop. */
    EnterCriticalSection(&g_atexit_lock);
    {
        UCRT_XP_ATEXIT_FN snapshot[UCRT_XP_MAX_ATEXIT];
        int count = g_atexit_count;
        for (i = 0; i < count; i++) snapshot[i] = g_atexit_fns[i];
        LeaveCriticalSection(&g_atexit_lock);

        for (i = count - 1; i >= 0; i--) {
            snapshot[i]();
        }
    }
}

__declspec(dllexport) void __cdecl ucrt_xp_exit(int code)
{
    run_atexit_handlers();
    ExitProcess((UINT)code);
}

__declspec(dllexport) void __cdecl ucrt_xp_abort(void)
{
    /* Deliberately skips atexit handlers, matching the standard's
     * abort() contract (abnormal termination, not normal cleanup) - and
     * raises a real fault rather than a clean ExitProcess, so a debugger
     * or crash-reporting hook attached to the process still sees it as
     * the abnormal exit it is. */
    RaiseException((DWORD)0xC0000602 /* matches classic CRT's abort() signature-ish code */,
                    0, 0, NULL);
    ExitProcess(3); /* fallback if RaiseException somehow returns */
}

/* ------------------------------------------------------------------ */
/* strerror / perror                                                   */
/* ------------------------------------------------------------------ */

__declspec(dllexport) char* __cdecl ucrt_xp_strerror(int errnum)
{
    /* Minimal static table covering the errno values ported code most
     * often checks. Not a full POSIX catalog. */
    switch (errnum) {
    case 0:  return "No error";
    case 1:  return "Operation not permitted";
    case 2:  return "No such file or directory";
    case 7:  return "Arg list too long";
    case 9:  return "Bad file descriptor";
    case 12: return "Not enough space";
    case 13: return "Permission denied";
    case 17: return "File exists";
    case 22: return "Invalid argument";
    case 24: return "Too many open files";
    case 28: return "No space left on device";
    case 34: return "Result too large";
    default: return "Unknown error";
    }
}

__declspec(dllexport) void __cdecl ucrt_xp_perror(const char *s)
{
    const char *msg = ucrt_xp_strerror(ucrt_xp_get_errno());
    UCRT_XP_FILE *err = ucrt_xp_stderr();
    if (s && *s) {
        ucrt_xp_fputs(s, err);
        ucrt_xp_fputs(": ", err);
    }
    ucrt_xp_fputs(msg, err);
    ucrt_xp_fputs("\n", err);
}

/* ------------------------------------------------------------------ */
/* getenv / system                                                     */
/* ------------------------------------------------------------------ */

__declspec(dllexport) char* __cdecl ucrt_xp_getenv(const char *name)
{
    /* Classic getenv returns a pointer into a static buffer that is
     * overwritten on subsequent calls. Match that contract. */
    static char buf[32768];
    DWORD n;
    if (!name) return NULL;
    n = GetEnvironmentVariableA(name, buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return NULL;
    return buf;
}

__declspec(dllexport) int __cdecl ucrt_xp_system(const char *command)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    char *cmd_copy;
    size_t len;

    if (!command) {
        /* Presence probe: is a command interpreter available? */
        return GetEnvironmentVariableA("COMSPEC", NULL, 0) > 0 ? 1 : 0;
    }

    len = ucrt_xp_strlen(command);
    cmd_copy = (char *)ucrt_xp_malloc(len + 1);
    if (!cmd_copy) return -1;
    CopyMemory(cmd_copy, command, len + 1);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        ucrt_xp_free(cmd_copy);
        return -1;
    }
    ucrt_xp_free(cmd_copy);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
