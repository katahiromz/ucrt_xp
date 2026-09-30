#ifndef UCRT_XP_INTERNAL_H
#define UCRT_XP_INTERNAL_H

#include "ucrt_xp.h"

/* Build counter - bump manually per release. Not part of the ABI contract,
 * purely informational for crash-dump triage. */
#define UCRT_XP_BUILD_NUMBER 1

/* Detects the running OS's service pack level using only APIs that exist
 * on XP RTM, so this check itself never faults on an unpatched box. */
DWORD ucrt_xp_detect_sp_level(void);

/* Shared process-wide heap handle, created lazily by ucrt_xp_heap_init(). */
extern HANDLE g_ucrt_xp_heap;

/* Returns cached small blocks for the current thread to the real heap.
 * Called from DllMain on THREAD_DETACH/PROCESS_DETACH. */
void ucrt_xp_heap_thread_cleanup(void);

/* Shared internals between stdio.c and wide.c: registers an
 * already-open Win32 HANDLE (from CreateFileA or CreateFileW - the fd
 * table doesn't care which) in the descriptor table, and builds a
 * UCRT_XP_FILE wrapping a freshly allocated fd. Not part of the public
 * ABI (no dllexport), just an internal seam so wide.c doesn't have to
 * duplicate the fd-table/FILE-buffer bookkeeping that already lives in
 * stdio.c. */
int ucrt_xp__register_fd(HANDLE h, int text_mode, int append_mode);
UCRT_XP_FILE *ucrt_xp__file_from_fd(int fd);

/* Internal accessor (locale.c) so wide.c/other internal callers can get
 * at a locale's resolved LCID without UCRT_XP_LOCALE's layout being part
 * of the public ABI (it's an opaque pointer in ucrt_xp.h on purpose - see
 * the "immutable, refcounted locale objects" design note in locale.c). */
LCID ucrt_xp__locale_lcid(ucrt_xp_locale_t loc);

/* Internal seams (format.c) reused by wide.c's ucrt_xp_vswprintf so the
 * wide formatter shares exactly one integer/float-to-ASCII-digits
 * implementation instead of maintaining a second, possibly divergent
 * one. Both produce pure-ASCII output, which is always safe to widen
 * 1:1 into wchar_t. */
char *ucrt_xp__utoa_generic(unsigned __int64 value, int base, int uppercase, char *buf_end);
int ucrt_xp__format_double_ascii(char *out, size_t outcap, double value,
                                  int precision, char conv,
                                  int force_sign, int space_sign);

#endif
