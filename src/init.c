/*
 * init.c - ABI version negotiation and DLL entry point.
 *
 * The whole point of this file: if a module was compiled against a header
 * whose ABI version does not match the ucrt_xp.dll that got loaded, fail
 * LOUD and IMMEDIATELY instead of limping along with mismatched struct
 * layouts. This is the single most important safety net in the design.
 */
#include "internal.h"
#include <stdio.h>
#include <assert.h>

HANDLE g_ucrt_xp_heap = NULL;
static volatile LONG g_initialized = 0;

DWORD ucrt_xp_detect_sp_level(void)
{
    OSVERSIONINFOEXA vi;
    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);

    /* GetVersionEx is deprecated on modern SDKs but is exactly the API
     * that exists unconditionally on XP RTM/SP2/SP3, which is the point. */
#pragma warning(push)
#pragma warning(disable: 4996)
    if (!GetVersionExA((OSVERSIONINFOA *)&vi)) {
        return 0;
    }
#pragma warning(pop)

    if (vi.wServicePackMajor >= 1) {
        return (DWORD)vi.wServicePackMajor;
    }
    return 0;
}

static void ucrt_xp_fatal_abi_mismatch(DWORD expected, DWORD actual)
{
#ifndef NDEBUG
    char msg[256];
    wsprintfA(msg,
        "ucrt_xp.dll ABI mismatch.\n\n"
        "This module was built expecting ABI 0x%08lX\n"
        "but the loaded ucrt_xp.dll reports ABI 0x%08lX.\n\n"
        "Replace ucrt_xp.dll with a matching version or rebuild the module.",
        expected, actual);
    switch (MessageBoxA(NULL, msg, "ucrt_xp: fatal ABI mismatch", MB_ABORTRETRYIGNORE))
    {
    case IDABORT:
        ucrt_xp_abort();
        break;
    case IDRETRY:
        assert(0);
        break;
    case IDIGNORE:
        return;
    }
#endif
    /* Do not attempt any further ucrt_xp / CRT usage past this point -
     * the state of the runtime cannot be trusted. */
    TerminateProcess(GetCurrentProcess(), (UINT)0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN-ish sentinel */);
}

UCRT_XP_API BOOL __cdecl ucrt_xp_init(DWORD expected_abi_version)
{
    /* Only the MAJOR component must match exactly; a DLL with an equal
     * or higher MINOR is backward compatible by construction (additive
     * changes only, see ucrt_xp.h). A lower MINOR than expected means
     * the caller may reference functions this DLL doesn't have yet. */
    DWORD expected_major = expected_abi_version >> 16;
    DWORD expected_minor = expected_abi_version & 0xFFFF;
    DWORD actual_major = UCRT_XP_ABI_MAJOR;
    DWORD actual_minor = UCRT_XP_ABI_MINOR;

    if (expected_major != actual_major || expected_minor > actual_minor) {
        ucrt_xp_fatal_abi_mismatch(expected_abi_version, UCRT_XP_ABI_VERSION);
        return FALSE; /* unreachable */
    }

    if (InterlockedCompareExchange(&g_initialized, 1, 0) == 0) {
        ucrt_xp_heap_init();
    }
    return TRUE;
}

UCRT_XP_API BOOL __cdecl ucrt_xp_get_version(UCRT_XP_VERSION_INFO *info)
{
    if (!info || info->cb < sizeof(UCRT_XP_VERSION_INFO)) {
        return FALSE;
    }
    info->abi_version = UCRT_XP_ABI_VERSION;
    info->build_number = UCRT_XP_BUILD_NUMBER;
    info->os_sp_level = ucrt_xp_detect_sp_level();
    return TRUE;
}

#ifdef UCRT_XP_BUILD_DLL
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        /* Defer heavy init to ucrt_xp_init() so that ABI checking happens
         * first and deterministically, rather than racing loader lock. */
        break;
    case DLL_THREAD_DETACH:
        ucrt_xp_heap_thread_cleanup();
        break;
    case DLL_PROCESS_DETACH:
        ucrt_xp__sync_cleanup();
        ucrt_xp_heap_thread_cleanup();
        if (g_ucrt_xp_heap) {
            HeapDestroy(g_ucrt_xp_heap);
            g_ucrt_xp_heap = NULL;
        }
        break;
    default:
        break;
    }
    return TRUE;
}
#endif
