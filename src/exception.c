/*
 * exception.c - the "exception trampoline" mentioned in the design doc.
 *
 * Problem: C++ exception internal representations (ThrowInfo, the magic
 * numbers embedded in the exception record, RTTI descriptors) differ
 * subtly between VC6, VC2005/2008 (which added /GS cookie-related fields)
 * and VC2010. If module A (built with VC6) throws and module B (built
 * with VC2010) tries to catch across the ucrt_xp boundary, the runtimes
 * can disagree about the exception layout.
 *
 * We don't try to reimplement C++ EH here (that's compiler-generated
 * code we can't intercept portably). What ucrt_xp *can* own is the SEH
 * layer underneath: a single, shared unhandled-exception path and a
 * translator hook so a crash in any module funnels through one place
 * with consistent diagnostics, instead of N different CRTs each doing
 * their own last-chance handling (and N different crash dialog styles).
 */
#include "internal.h"
#include <stdio.h>

static UCRT_XP_UNHANDLED_FN g_unhandled_fn = NULL;

__declspec(dllexport) void __cdecl ucrt_xp_set_unhandled_handler(UCRT_XP_UNHANDLED_FN fn)
{
    g_unhandled_fn = fn;
}

static const char *exception_code_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    case 0xE06D7363: /* MSVC C++ exception magic number */
                                           return "CXX_EXCEPTION_UNCAUGHT";
    default:                              return "UNKNOWN";
    }
}

/*
 * ucrt_xp_seh_filter is unchanged above this point in the design; the
 * additions below implement:
 *   - a per-thread translator hook (ucrt_xp_set_se_translator)
 *   - ucrt_xp_guarded_call(), a call-with-fault-containment primitive
 *
 * Two implementations are provided:
 *   - _MSC_VER: real __try/__except, the intended production path for
 *     VC6..VC2010, which is what this whole project targets.
 *   - everything else (GCC/MinGW, used here only to sanity-compile and
 *     smoke-test the rest of ucrt_xp in a non-Windows-MSVC sandbox):
 *     AddVectoredExceptionHandler + setjmp/longjmp. This does NOT run
 *     destructors/finally blocks during unwind and is NOT a general
 *     substitute for __try/__except - it exists purely so this file
 *     builds and its logic is testable outside of MSVC.
 */
#include <setjmp.h>

static DWORD g_translator_tls = TLS_OUT_OF_INDEXES;
static UCRT_XP_ONCE g_translator_tls_once = UCRT_XP_ONCE_INIT;

typedef struct TranslatorSlot {
    UCRT_XP_SE_TRANSLATOR_FN fn;
    void *user_context;
} TranslatorSlot;

static BOOL __cdecl init_translator_tls(void *param)
{
    (void)param;
    g_translator_tls = TlsAlloc();
    return g_translator_tls != TLS_OUT_OF_INDEXES;
}

static TranslatorSlot *get_translator_slot(BOOL create)
{
    TranslatorSlot *slot;
    ucrt_xp_once(&g_translator_tls_once, init_translator_tls, NULL);
    if (g_translator_tls == TLS_OUT_OF_INDEXES) return NULL;

    slot = (TranslatorSlot *)TlsGetValue(g_translator_tls);
    if (!slot && create) {
        slot = (TranslatorSlot *)ucrt_xp_malloc(sizeof(TranslatorSlot));
        if (slot) {
            slot->fn = NULL;
            slot->user_context = NULL;
            TlsSetValue(g_translator_tls, slot);
        }
    }
    return slot;
}

__declspec(dllexport) UCRT_XP_SE_TRANSLATOR_FN __cdecl ucrt_xp_set_se_translator(
    UCRT_XP_SE_TRANSLATOR_FN fn, void *user_context)
{
    UCRT_XP_SE_TRANSLATOR_FN old = NULL;
    TranslatorSlot *slot = get_translator_slot(TRUE);
    if (!slot) return NULL;

    old = slot->fn;
    slot->fn = fn;
    slot->user_context = user_context;
    return old;
}

static void fill_normalized_info(UCRT_XP_EXCEPTION_INFO *out, EXCEPTION_RECORD *rec)
{
    DWORD i, n;
    ZeroMemory(out, sizeof(*out));
    if (!rec) return;

    out->code = rec->ExceptionCode;
    out->flags = rec->ExceptionFlags;
    out->address = rec->ExceptionAddress;

    n = rec->NumberParameters;
    if (n > EXCEPTION_MAXIMUM_PARAMETERS) n = EXCEPTION_MAXIMUM_PARAMETERS;
    out->param_count = n;
    for (i = 0; i < n; i++) {
        out->params[i] = rec->ExceptionInformation[i];
    }
}

static void invoke_translator_if_any(UCRT_XP_EXCEPTION_INFO *info)
{
    TranslatorSlot *slot = get_translator_slot(FALSE);
    if (slot && slot->fn) {
        slot->fn(info, slot->user_context);
    }
}

#if defined(_MSC_VER)

__declspec(dllexport) BOOL __cdecl ucrt_xp_guarded_call(
    UCRT_XP_GUARDED_FN fn, void *param, int *result,
    UCRT_XP_EXCEPTION_INFO *out_info)
{
    __try {
        int r = fn(param);
        if (result) *result = r;
        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        EXCEPTION_RECORD *rec = (GetExceptionInformation())->ExceptionRecord;
        UCRT_XP_EXCEPTION_INFO info;
        fill_normalized_info(&info, rec);
        invoke_translator_if_any(&info);
        if (out_info) *out_info = info;
        return FALSE;
    }
}

#else /* non-MSVC fallback: vectored handler + setjmp/longjmp */

static DWORD g_guard_tls = TLS_OUT_OF_INDEXES;
static UCRT_XP_ONCE g_guard_tls_once = UCRT_XP_ONCE_INIT;

typedef struct GuardFrame {
    jmp_buf jmp;
    UCRT_XP_EXCEPTION_INFO info;
    int active;
} GuardFrame;

static BOOL __cdecl init_guard_tls(void *param)
{
    (void)param;
    g_guard_tls = TlsAlloc();
    return g_guard_tls != TLS_OUT_OF_INDEXES;
}

static LONG CALLBACK guard_vectored_handler(EXCEPTION_POINTERS *ep)
{
    GuardFrame *frame;
    DWORD code;
    ucrt_xp_once(&g_guard_tls_once, init_guard_tls, NULL);
    if (g_guard_tls == TLS_OUT_OF_INDEXES) return EXCEPTION_CONTINUE_SEARCH;

    frame = (GuardFrame *)TlsGetValue(g_guard_tls);
    if (!frame || !frame->active) return EXCEPTION_CONTINUE_SEARCH;

    code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH; /* C++ EH: not ours */

    fill_normalized_info(&frame->info, ep->ExceptionRecord);
    invoke_translator_if_any(&frame->info);
    frame->active = 0;
    longjmp(frame->jmp, 1);
    /* not reached */
    return EXCEPTION_CONTINUE_SEARCH;
}

static PVOID g_guard_handle = NULL;
static UCRT_XP_ONCE g_guard_install_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl install_guard_handler(void *param)
{
    (void)param;
    g_guard_handle = AddVectoredExceptionHandler(1, guard_vectored_handler);
    return g_guard_handle != NULL;
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_guarded_call(
    UCRT_XP_GUARDED_FN fn, void *param, int *result,
    UCRT_XP_EXCEPTION_INFO *out_info)
{
    GuardFrame *frame;
    ucrt_xp_once(&g_guard_install_once, install_guard_handler, NULL);
    ucrt_xp_once(&g_guard_tls_once, init_guard_tls, NULL);
    if (g_guard_tls == TLS_OUT_OF_INDEXES) return FALSE;

    frame = (GuardFrame *)TlsGetValue(g_guard_tls);
    if (!frame) {
        frame = (GuardFrame *)ucrt_xp_malloc(sizeof(GuardFrame));
        if (!frame) return FALSE;
        TlsSetValue(g_guard_tls, frame);
    }

    frame->active = 1;
    if (setjmp(frame->jmp) == 0) {
        int r = fn(param);
        frame->active = 0;
        if (result) *result = r;
        return TRUE;
    }
    /* Landed here via longjmp from guard_vectored_handler. */
    if (out_info) *out_info = frame->info;
    return FALSE;
}

#endif /* _MSC_VER */

__declspec(dllexport) LONG __cdecl ucrt_xp_seh_filter(EXCEPTION_POINTERS *ep)
{
    char buf[512];
    DWORD code;

    if (!ep || !ep->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    code = ep->ExceptionRecord->ExceptionCode;

    /* A genuine, uncaught C++ exception (0xE06D7363) is not something we
     * want to swallow here - a debugger or a module-local catch(...)
     * higher up the stack should still get first refusal. We only take
     * ownership of true structured (hardware/OS) exceptions. */
    if (code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    {
        UCRT_XP_EXCEPTION_INFO info;
        fill_normalized_info(&info, ep->ExceptionRecord);
        invoke_translator_if_any(&info);
    }

    wsprintfA(buf,
        "Unhandled exception 0x%08lX (%hs) at address 0x%p",
        code, exception_code_name(code),
        ep->ExceptionRecord->ExceptionAddress);

    if (g_unhandled_fn) {
        g_unhandled_fn(buf, ep);
    } else {
        OutputDebugStringA(buf);
    }

    /* Let the OS proceed with normal unhandled-exception processing
     * (Dr. Watson / WER) after our diagnostic hook has run, rather than
     * pretending to have handled it. */
    return EXCEPTION_EXECUTE_HANDLER;
}
