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
#include <stdarg.h>

#ifndef _INTPTR_T_DEFINED
#ifdef _WIN64
typedef __int64          intptr_t;
typedef unsigned __int64 uintptr_t;
#else
typedef int              intptr_t;
typedef unsigned int     uintptr_t;
#endif
#define _INTPTR_T_DEFINED
#endif

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
#ifndef NDEBUG
    ZeroMemory(&pi, sizeof(pi));
#endif

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

/* ------------------------------------------------------------------ */
/* Process identity / environment                                      */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_getpid(void)
{
    return (int)GetCurrentProcessId();
}

/* _putenv: argument is "NAME=value" or "NAME=" (to clear). Returns 0
 * on success, -1 on failure. Matches MSVC's non-_s form. */
__declspec(dllexport) int __cdecl ucrt_xp_putenv(const char *envstring)
{
    char *copy, *eq;
    size_t len;
    BOOL ok;

    if (!envstring || !*envstring) return -1;
    len = ucrt_xp_strlen(envstring);
    copy = (char *)ucrt_xp_malloc(len + 1);
    if (!copy) return -1;
    CopyMemory(copy, envstring, len + 1);

    eq = ucrt_xp_strchr(copy, '=');
    if (!eq) {
        ucrt_xp_free(copy);
        return -1;
    }
    *eq = 0;
    /* empty value => clear the variable (MSVC _putenv("NAME=") behavior) */
    ok = SetEnvironmentVariableA(copy, (eq[1] == 0) ? NULL : (eq + 1));
    ucrt_xp_free(copy);
    return ok ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Pipes                                                               */
/* ------------------------------------------------------------------ */

/* _pipe: create a pipe, store read/write fds in pipedes[0]/[1].
 * psize is a hint (ignored on Win32 beyond security attrs). text_mode
 * selects UCRT text translation on the resulting descriptors.
 * Returns 0 on success, -1 on failure. */
__declspec(dllexport) int __cdecl ucrt_xp_pipe(int *pipedes, unsigned int psize, int text_mode)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL;
    int fd_rd, fd_wr;

    (void)psize;
    if (!pipedes) return -1;

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;

    /* Make the parent ends non-inheritable so only the child's side is
     * duplicated into a future CreateProcess. */
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);

    fd_rd = ucrt_xp__register_fd(rd, text_mode ? 1 : 0, 0);
    fd_wr = ucrt_xp__register_fd(wr, text_mode ? 1 : 0, 0);
    if (fd_rd < 0 || fd_wr < 0) {
        if (fd_rd >= 0) ucrt_xp_close(fd_rd);
        if (fd_wr >= 0) ucrt_xp_close(fd_wr);
        if (fd_rd < 0 && rd) CloseHandle(rd);
        if (fd_wr < 0 && wr) CloseHandle(wr);
        return -1;
    }
    pipedes[0] = fd_rd;
    pipedes[1] = fd_wr;
    return 0;
}

/* ------------------------------------------------------------------ */
/* popen / pclose                                                      */
/* ------------------------------------------------------------------ */

typedef struct PopenEntry {
    UCRT_XP_FILE *file;
    HANDLE        process;
    struct PopenEntry *next;
} PopenEntry;

static PopenEntry *g_popen_list = NULL;
static CRITICAL_SECTION g_popen_lock;
static UCRT_XP_ONCE g_popen_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_popen_lock(void *param)
{
    (void)param;
    InitializeCriticalSection(&g_popen_lock);
    return TRUE;
}

static void popen_register(UCRT_XP_FILE *f, HANDLE proc)
{
    PopenEntry *e;
    ucrt_xp_once(&g_popen_once, init_popen_lock, NULL);
    e = (PopenEntry *)ucrt_xp_malloc(sizeof(PopenEntry));
    if (!e) return;
    e->file = f;
    e->process = proc;
    EnterCriticalSection(&g_popen_lock);
    e->next = g_popen_list;
    g_popen_list = e;
    LeaveCriticalSection(&g_popen_lock);
}

static HANDLE popen_unregister(UCRT_XP_FILE *f)
{
    PopenEntry **pp, *e;
    HANDLE proc = NULL;
    ucrt_xp_once(&g_popen_once, init_popen_lock, NULL);
    EnterCriticalSection(&g_popen_lock);
    for (pp = &g_popen_list; *pp; pp = &(*pp)->next) {
        if ((*pp)->file == f) {
            e = *pp;
            *pp = e->next;
            proc = e->process;
            ucrt_xp_free(e);
            break;
        }
    }
    LeaveCriticalSection(&g_popen_lock);
    return proc;
}

/* mode: "r" reads child stdout; "w" writes child stdin. Optional 't'/'b'
 * for text/binary (default text). Returns a UCRT_XP_FILE* or NULL. */
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_popen(const char *command, const char *mode)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE child_rd = NULL, child_wr = NULL;
    HANDLE parent_end = NULL, child_end = NULL;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char *cmd_copy;
    size_t len;
    int reading;
    int text_mode = 1;
    int fd;
    UCRT_XP_FILE *f;
    const char *m;

    if (!command || !mode) return NULL;
    reading = (mode[0] == 'r');
    if (!reading && mode[0] != 'w') return NULL;
    for (m = mode + 1; *m; m++) {
        if (*m == 'b') text_mode = 0;
        if (*m == 't') text_mode = 1;
    }

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&child_rd, &child_wr, &sa, 0)) return NULL;

    if (reading) {
        /* parent reads from child_rd; child writes to child_wr */
        parent_end = child_rd;
        child_end  = child_wr;
        SetHandleInformation(parent_end, HANDLE_FLAG_INHERIT, 0);
    } else {
        /* parent writes to child_wr; child reads from child_rd */
        parent_end = child_wr;
        child_end  = child_rd;
        SetHandleInformation(parent_end, HANDLE_FLAG_INHERIT, 0);
    }

    len = ucrt_xp_strlen(command);
    cmd_copy = (char *)ucrt_xp_malloc(len + 1);
    if (!cmd_copy) {
        CloseHandle(child_rd);
        CloseHandle(child_wr);
        return NULL;
    }
    CopyMemory(cmd_copy, command, len + 1);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = reading ? GetStdHandle(STD_INPUT_HANDLE)  : child_end;
    si.hStdOutput = reading ? child_end : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd_copy, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        ucrt_xp_free(cmd_copy);
        CloseHandle(child_rd);
        CloseHandle(child_wr);
        return NULL;
    }
    ucrt_xp_free(cmd_copy);
    CloseHandle(pi.hThread);
    /* Parent no longer needs the child's end of the pipe. */
    CloseHandle(child_end);

    fd = ucrt_xp__register_fd(parent_end, text_mode, 0);
    if (fd < 0) {
        CloseHandle(parent_end);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return NULL;
    }
    f = ucrt_xp__file_from_fd(fd);
    if (!f) {
        ucrt_xp_close(fd);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return NULL;
    }
    popen_register(f, pi.hProcess);
    return f;
}

__declspec(dllexport) int __cdecl ucrt_xp_pclose(UCRT_XP_FILE *f)
{
    HANDLE proc;
    DWORD code = (DWORD)-1;

    if (!f) return -1;
    proc = popen_unregister(f);
    ucrt_xp_fclose(f);
    if (!proc) return -1;
    WaitForSingleObject(proc, INFINITE);
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}

/* ------------------------------------------------------------------ */
/* spawn / exec helpers                                                */
/* ------------------------------------------------------------------ */

/* MSVC spawn mode constants (values match the real CRT). */
#ifndef UCRT_XP_P_WAIT
#define UCRT_XP_P_WAIT    0
#define UCRT_XP_P_NOWAIT  1
#define UCRT_XP_P_OVERLAY 2
#define UCRT_XP_P_NOWAITO 3
#define UCRT_XP_P_DETACH  4
#endif

/* Build a quoted Windows command line from argv. Caller frees result. */
static char *build_cmdline(const char *const *argv)
{
    size_t total = 0;
    int i, n;
    char *out, *p;

    if (!argv || !argv[0]) return NULL;
    for (n = 0; argv[n]; n++) {
        total += ucrt_xp_strlen(argv[n]) + 3; /* quotes + space */
    }
    out = (char *)ucrt_xp_malloc(total + 1);
    if (!out) return NULL;
    p = out;
    for (i = 0; i < n; i++) {
        size_t L = ucrt_xp_strlen(argv[i]);
        int need_q = (ucrt_xp_strchr(argv[i], ' ') != NULL ||
                      ucrt_xp_strchr(argv[i], '\t') != NULL || L == 0);
        if (i) *p++ = ' ';
        if (need_q) *p++ = '"';
        CopyMemory(p, argv[i], L);
        p += L;
        if (need_q) *p++ = '"';
    }
    *p = 0;
    return out;
}

/* Search PATH for an executable name (simple, no PATHEXT expansion
 * beyond .exe). Returns a malloc'd full path or a copy of name. */
static char *search_path(const char *name)
{
    char buf[MAX_PATH];
    char *path_env, *tmp, *tok, *result = NULL;
    DWORD n;

    if (!name || !*name) return NULL;
    /* Absolute or relative with separator: use as-is. */
    if (ucrt_xp_strchr(name, '\\') || ucrt_xp_strchr(name, '/') ||
        (name[0] && name[1] == ':')) {
        size_t L = ucrt_xp_strlen(name);
        result = (char *)ucrt_xp_malloc(L + 1);
        if (result) CopyMemory(result, name, L + 1);
        return result;
    }

    n = SearchPathA(NULL, name, ".exe", MAX_PATH, buf, NULL);
    if (n > 0 && n < MAX_PATH) {
        result = (char *)ucrt_xp_malloc(n + 1);
        if (result) CopyMemory(result, buf, n + 1);
        return result;
    }
    /* Fallback: return the bare name and let CreateProcess try. */
    {
        size_t L = ucrt_xp_strlen(name);
        result = (char *)ucrt_xp_malloc(L + 1);
        if (result) CopyMemory(result, name, L + 1);
    }
    (void)path_env; (void)tmp; (void)tok;
    return result;
}

static intptr_t spawnve_internal(int mode, const char *cmdname,
                                  const char *const *argv, int use_path)
{
    char *cmdline, *app;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = (DWORD)-1;
    BOOL ok;

    if (!cmdname || !argv || !argv[0]) return -1;

    app = use_path ? search_path(cmdname) : NULL;
    if (!use_path) {
        size_t L = ucrt_xp_strlen(cmdname);
        app = (char *)ucrt_xp_malloc(L + 1);
        if (app) CopyMemory(app, cmdname, L + 1);
    }
    cmdline = build_cmdline(argv);
    if (!app || !cmdline) {
        ucrt_xp_free(app);
        ucrt_xp_free(cmdline);
        return -1;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    ok = CreateProcessA(app, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    ucrt_xp_free(app);
    ucrt_xp_free(cmdline);
    if (!ok) return -1;

    CloseHandle(pi.hThread);

    if (mode == UCRT_XP_P_WAIT) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        return (intptr_t)exit_code;
    }
    if (mode == UCRT_XP_P_OVERLAY) {
        /* Approximate overlay: wait then terminate self with child's code. */
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        ucrt_xp_exit((int)exit_code);
        return -1; /* not reached */
    }
    if (mode == UCRT_XP_P_DETACH) {
        CloseHandle(pi.hProcess);
        return 0;
    }
    /* P_NOWAIT / P_NOWAITO: return process id-ish handle cast. */
    {
        intptr_t id = (intptr_t)pi.hProcess;
        /* Caller cannot CloseHandle this easily; keep handle open so
         * they can wait via other means. Documented limitation. */
        return id;
    }
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_spawnv(int mode, const char *cmdname, const char *const *argv)
{
    return spawnve_internal(mode, cmdname, argv, 0);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_spawnvp(int mode, const char *cmdname, const char *const *argv)
{
    return spawnve_internal(mode, cmdname, argv, 1);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_spawnl(int mode, const char *cmdname, const char *arg0, ...)
{
    /* Collect varargs into a temporary argv array (max 64 args). */
    const char *argv[64];
    va_list ap;
    int i = 0;
    argv[i++] = arg0;
    va_start(ap, arg0);
    while (i < 63) {
        const char *a = va_arg(ap, const char *);
        argv[i++] = a;
        if (!a) break;
    }
    va_end(ap);
    if (i == 63) argv[63] = NULL;
    return spawnve_internal(mode, cmdname, argv, 0);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_spawnlp(int mode, const char *cmdname, const char *arg0, ...)
{
    const char *argv[64];
    va_list ap;
    int i = 0;
    argv[i++] = arg0;
    va_start(ap, arg0);
    while (i < 63) {
        const char *a = va_arg(ap, const char *);
        argv[i++] = a;
        if (!a) break;
    }
    va_end(ap);
    if (i == 63) argv[63] = NULL;
    return spawnve_internal(mode, cmdname, argv, 1);
}

/* exec* approximate overlay (spawn with P_OVERLAY). */
__declspec(dllexport) intptr_t __cdecl ucrt_xp_execv(const char *cmdname, const char *const *argv)
{
    return spawnve_internal(UCRT_XP_P_OVERLAY, cmdname, argv, 0);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_execvp(const char *cmdname, const char *const *argv)
{
    return spawnve_internal(UCRT_XP_P_OVERLAY, cmdname, argv, 1);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_execl(const char *cmdname, const char *arg0, ...)
{
    const char *argv[64];
    va_list ap;
    int i = 0;
    argv[i++] = arg0;
    va_start(ap, arg0);
    while (i < 63) {
        const char *a = va_arg(ap, const char *);
        argv[i++] = a;
        if (!a) break;
    }
    va_end(ap);
    if (i == 63) argv[63] = NULL;
    return spawnve_internal(UCRT_XP_P_OVERLAY, cmdname, argv, 0);
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_execlp(const char *cmdname, const char *arg0, ...)
{
    const char *argv[64];
    va_list ap;
    int i = 0;
    argv[i++] = arg0;
    va_start(ap, arg0);
    while (i < 63) {
        const char *a = va_arg(ap, const char *);
        argv[i++] = a;
        if (!a) break;
    }
    va_end(ap);
    if (i == 63) argv[63] = NULL;
    return spawnve_internal(UCRT_XP_P_OVERLAY, cmdname, argv, 1);
}

/* ------------------------------------------------------------------ */
/* _beginthread / _beginthreadex / _endthread / _endthreadex           */
/* ------------------------------------------------------------------ */

typedef struct BeginThreadCtx {
    void (__cdecl *start_cdecl)(void *);
    unsigned (__stdcall *start_stdcall)(void *);
    void *arg;
    int is_stdcall;
} BeginThreadCtx;

static DWORD WINAPI beginthread_trampoline(void *param)
{
    BeginThreadCtx ctx = *(BeginThreadCtx *)param;
    ucrt_xp_free(param);
    if (ctx.is_stdcall) {
        unsigned code = ctx.start_stdcall(ctx.arg);
        return (DWORD)code;
    } else {
        ctx.start_cdecl(ctx.arg);
        return 0;
    }
}

__declspec(dllexport) uintptr_t __cdecl ucrt_xp_beginthread(
    void (__cdecl *start_address)(void *), unsigned stack_size, void *arglist)
{
    BeginThreadCtx *ctx;
    HANDLE h;
    DWORD tid;

    if (!start_address) return (uintptr_t)-1L;
    ctx = (BeginThreadCtx *)ucrt_xp_malloc(sizeof(BeginThreadCtx));
    if (!ctx) return (uintptr_t)-1L;
    ctx->start_cdecl = start_address;
    ctx->start_stdcall = NULL;
    ctx->arg = arglist;
    ctx->is_stdcall = 0;

    h = CreateThread(NULL, stack_size, beginthread_trampoline, ctx, 0, &tid);
    if (!h) {
        ucrt_xp_free(ctx);
        return (uintptr_t)-1L;
    }
    /* MSVC _beginthread returns handle; library owns close on thread end
     * in the real CRT. We return the handle; caller may CloseHandle. */
    return (uintptr_t)h;
}

__declspec(dllexport) uintptr_t __cdecl ucrt_xp_beginthreadex(
    void *security, unsigned stack_size,
    unsigned (__stdcall *start_address)(void *), void *arglist,
    unsigned initflag, unsigned *thrdaddr)
{
    BeginThreadCtx *ctx;
    HANDLE h;
    DWORD tid;
    LPSECURITY_ATTRIBUTES sa = (LPSECURITY_ATTRIBUTES)security;

    if (!start_address) return 0;
    ctx = (BeginThreadCtx *)ucrt_xp_malloc(sizeof(BeginThreadCtx));
    if (!ctx) return 0;
    ctx->start_cdecl = NULL;
    ctx->start_stdcall = start_address;
    ctx->arg = arglist;
    ctx->is_stdcall = 1;

    h = CreateThread(sa, stack_size, beginthread_trampoline, ctx, initflag, &tid);
    if (!h) {
        ucrt_xp_free(ctx);
        return 0;
    }
    if (thrdaddr) *thrdaddr = (unsigned)tid;
    return (uintptr_t)h;
}

__declspec(dllexport) void __cdecl ucrt_xp_endthread(void)
{
    ExitThread(0);
}

__declspec(dllexport) void __cdecl ucrt_xp_endthreadex(unsigned retval)
{
    ExitThread(retval);
}
