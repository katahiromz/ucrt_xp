/*
 * ucrt_xp.h  -  Public, ABI-stable interface for "Universal CRT for Windows XP"
 *
 * DESIGN RULES (do not violate without bumping UCRT_XP_ABI_VERSION):
 *   1. Every struct here is laid out explicitly; no compiler-dependent
 *      padding assumptions, no bit-fields.
 *   2. Every exported function uses __cdecl and is declared extern "C".
 *   3. New functionality is added by APPENDING new functions/structs,
 *      never by changing existing signatures.
 *   4. This header must compile under VC6 (/Za not required, but no C99-only
 *      syntax) as well as modern MinGW/MSVC, and must not pull in anything
 *      newer than _WIN32_WINNT 0x0501.
 */
#ifndef UCRT_XP_H
#define UCRT_XP_H

#define _WIN32_WINNT 0x0501   /* Windows XP */
#define WINVER       0x0501
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Versioning / ABI negotiation                                        */
/* ------------------------------------------------------------------ */

/*
 * Bump the MAJOR part only for a breaking ABI change (must never happen
 * once a version ships). Bump MINOR for additive, backward-compatible
 * changes (new functions appended at the end of this header).
 */
#define UCRT_XP_ABI_MAJOR 1
#define UCRT_XP_ABI_MINOR 0
#define UCRT_XP_ABI_VERSION ((UCRT_XP_ABI_MAJOR << 16) | UCRT_XP_ABI_MINOR)

typedef struct UCRT_XP_VERSION_INFO {
    DWORD cb;              /* sizeof(UCRT_XP_VERSION_INFO), set by caller */
    DWORD abi_version;     /* filled in by ucrt_xp on return */
    DWORD build_number;    /* internal build counter */
    DWORD os_sp_level;     /* 2 = SP2, 3 = SP3, 0 = unknown/RTM */
} UCRT_XP_VERSION_INFO;

/*
 * Every module MUST call this once before using any other ucrt_xp
 * function (normally done automatically from CRT startup glue).
 * Returns TRUE on success. On ABI mismatch it does NOT return: it shows
 * a diagnostic message box and terminates the process, because a
 * silent partial-compat run is worse than a hard failure.
 */
__declspec(dllexport) BOOL __cdecl ucrt_xp_init(DWORD expected_abi_version);

/* Query loaded runtime version/build info. info->cb must be set first. */
__declspec(dllexport) BOOL __cdecl ucrt_xp_get_version(UCRT_XP_VERSION_INFO *info);

/*
 * Optional automatic startup - the usability shortcut for the manual
 * ucrt_xp_init(UCRT_XP_ABI_VERSION) call every program otherwise needs.
 *
 * In exactly ONE source file, before including this header, write:
 *     #define UCRT_XP_IMPLEMENT_AUTOSTART
 *     #include "ucrt_xp.h"
 * and the ABI check runs automatically before main() - no call needed
 * anywhere in the program. This works the same way single-header C
 * libraries' "#define X_IMPLEMENTATION" convention does: exactly one
 * definition, anywhere else is a normal include.
 *
 * How it works, per toolchain:
 *   - MSVC: a function pointer placed in the ".CRT$XCU" section, which
 *     the real CRT startup code walks and calls before main(). This is
 *     the same mechanism C++'s static object constructors use.
 *   - GCC/Clang (MinGW): __attribute__((constructor)), a directly
 *     supported "run this before main()" feature.
 *
 * Caveat: on MSVC, aggressive whole-program/LTCG optimization can in
 * rare configurations strip an unreferenced data-only symbol before the
 * linker processes CRT init sections. If ucrt_xp_get_version() ever
 * reports build/ABI info that looks stale/uninitialized at program
 * start, that's the symptom - the reliable fallback is always an
 * explicit ucrt_xp_init(UCRT_XP_ABI_VERSION) call at the top of main().
 */
#ifdef UCRT_XP_IMPLEMENT_AUTOSTART
static void __cdecl ucrt_xp__autostart(void)
{
    ucrt_xp_init(UCRT_XP_ABI_VERSION);
}

#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU"))
static void (__cdecl *ucrt_xp__autostart_ptr)(void) = ucrt_xp__autostart;
#ifdef _WIN64
#pragma comment(linker, "/include:ucrt_xp__autostart_ptr")
#else
#pragma comment(linker, "/include:_ucrt_xp__autostart_ptr")
#endif
#elif defined(__GNUC__)
__attribute__((constructor))
static void ucrt_xp__autostart_ctor(void)
{
    ucrt_xp__autostart();
}
#endif
#endif /* UCRT_XP_IMPLEMENT_AUTOSTART */

/* ------------------------------------------------------------------ */
/* One-time initialization (InitOnceExecuteOnce substitute)            */
/* ------------------------------------------------------------------ */

typedef struct UCRT_XP_ONCE {
    volatile LONG state; /* 0=not started,1=running,2=done - do not touch */
} UCRT_XP_ONCE;

#define UCRT_XP_ONCE_INIT { 0 }

typedef BOOL (__cdecl *UCRT_XP_ONCE_FN)(void *param);

__declspec(dllexport) BOOL __cdecl ucrt_xp_once(
    UCRT_XP_ONCE *once,
    UCRT_XP_ONCE_FN fn,
    void *param);

/* ------------------------------------------------------------------ */
/* Condition variable substitute (CONDITION_VARIABLE is Vista+ only)   */
/* ------------------------------------------------------------------ */

typedef struct UCRT_XP_COND {
    HANDLE sema;                 /* counts waiters that were signaled */
    HANDLE waiters_done;         /* manual-reset event: broadcast done */
    CRITICAL_SECTION waiters_lock;
    LONG waiters_count;
    LONG was_broadcast;
} UCRT_XP_COND;

__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_init(UCRT_XP_COND *cv);
__declspec(dllexport) void __cdecl ucrt_xp_cond_destroy(UCRT_XP_COND *cv);
__declspec(dllexport) BOOL __cdecl ucrt_xp_cond_wait(
    UCRT_XP_COND *cv, CRITICAL_SECTION *external_lock, DWORD timeout_ms);
__declspec(dllexport) void __cdecl ucrt_xp_cond_signal(UCRT_XP_COND *cv);
__declspec(dllexport) void __cdecl ucrt_xp_cond_broadcast(UCRT_XP_COND *cv);

/* ------------------------------------------------------------------ */
/* Heap                                                                 */
/* ------------------------------------------------------------------ */

__declspec(dllexport) BOOL  __cdecl ucrt_xp_heap_init(void);
__declspec(dllexport) void* __cdecl ucrt_xp_malloc(size_t size);
__declspec(dllexport) void* __cdecl ucrt_xp_calloc(size_t count, size_t size);
__declspec(dllexport) void* __cdecl ucrt_xp_realloc(void *ptr, size_t size);
__declspec(dllexport) void  __cdecl ucrt_xp_free(void *ptr);

/* ------------------------------------------------------------------ */
/* Locale (thread-local, refcounted, immutable objects)                */
/* ------------------------------------------------------------------ */

typedef struct UCRT_XP_LOCALE *ucrt_xp_locale_t;

__declspec(dllexport) ucrt_xp_locale_t __cdecl ucrt_xp_locale_create(const char *name);
__declspec(dllexport) ucrt_xp_locale_t __cdecl ucrt_xp_locale_addref(ucrt_xp_locale_t loc);
__declspec(dllexport) void __cdecl ucrt_xp_locale_release(ucrt_xp_locale_t loc);
__declspec(dllexport) ucrt_xp_locale_t __cdecl ucrt_xp_locale_get_thread(void);
__declspec(dllexport) void __cdecl ucrt_xp_locale_set_thread(ucrt_xp_locale_t loc);

__declspec(dllexport) int __cdecl ucrt_xp_stricmp_l(
    const char *a, const char *b, ucrt_xp_locale_t loc);

/* Non-locale (thread-default locale) case-insensitive compare.
 * Mirrors MSVC's _stricmp. */
__declspec(dllexport) int __cdecl ucrt_xp_stricmp(
    const char *a, const char *b);

/* Additional per-category accessors, resolved once at locale-creation
 * time (see locale.c) so hot-path callers never touch the OS during
 * normal operation. */
typedef struct UCRT_XP_LCONV {
    char decimal_point[8];
    char thousands_sep[8];
    char currency_symbol[16];
} UCRT_XP_LCONV;

__declspec(dllexport) BOOL __cdecl ucrt_xp_locale_get_lconv(
    ucrt_xp_locale_t loc, UCRT_XP_LCONV *out);
__declspec(dllexport) int __cdecl ucrt_xp_toupper_l(int c, ucrt_xp_locale_t loc);
__declspec(dllexport) int __cdecl ucrt_xp_tolower_l(int c, ucrt_xp_locale_t loc);

/* ------------------------------------------------------------------ */
/* Exception trampoline                                                */
/* ------------------------------------------------------------------ */

typedef void (__cdecl *UCRT_XP_UNHANDLED_FN)(const char *what, void *context);

__declspec(dllexport) void __cdecl ucrt_xp_set_unhandled_handler(UCRT_XP_UNHANDLED_FN fn);
__declspec(dllexport) LONG __cdecl ucrt_xp_seh_filter(EXCEPTION_POINTERS *ep);

/* Normalized exception record: a fixed, ABI-stable snapshot of an SEH
 * exception, independent of whatever EXCEPTION_RECORD layout quirks a
 * particular CRT/compiler version might introduce around it. */
typedef struct UCRT_XP_EXCEPTION_INFO {
    DWORD  code;
    DWORD  flags;
    void  *address;
    DWORD  param_count;
    ULONG_PTR params[EXCEPTION_MAXIMUM_PARAMETERS];
} UCRT_XP_EXCEPTION_INFO;

typedef int (__cdecl *UCRT_XP_GUARDED_FN)(void *param);

/*
 * Runs fn(param) under a top-level SEH __try/__except. If a hardware/OS
 * exception (access violation, div-by-zero, ...) occurs, execution stops
 * at the fault point, *out_info is filled in, and the function returns
 * FALSE. If fn completes normally, *result receives its return value and
 * the function returns TRUE.
 *
 * NOTE: this deliberately does NOT catch C++ exceptions (0xE06D7363) -
 * see exception.c for why true cross-VC-version C++ EH interop needs
 * compiler cooperation this function can't provide. Use ordinary
 * try/catch in the throwing module for that; use this for guarding
 * against genuine faults at a module boundary (e.g. calling into a
 * plugin DLL you don't trust).
 */
__declspec(dllexport) BOOL __cdecl ucrt_xp_guarded_call(
    UCRT_XP_GUARDED_FN fn, void *param, int *result,
    UCRT_XP_EXCEPTION_INFO *out_info);

/* Per-thread translator hook, modeled after _set_se_translator but
 * explicitly a C callback (not a C++ throw expression) so it works
 * uniformly regardless of which VC version the caller was built with.
 * Invoked from inside ucrt_xp_seh_filter before the default logging. */
typedef void (__cdecl *UCRT_XP_SE_TRANSLATOR_FN)(
    UCRT_XP_EXCEPTION_INFO *info, void *user_context);

__declspec(dllexport) UCRT_XP_SE_TRANSLATOR_FN __cdecl ucrt_xp_set_se_translator(
    UCRT_XP_SE_TRANSLATOR_FN fn, void *user_context);

/* ------------------------------------------------------------------ */
/* stdio layer: file descriptor table + buffered streams               */
/* ------------------------------------------------------------------ */

#define UCRT_XP_O_RDONLY  0x0000
#define UCRT_XP_O_WRONLY  0x0001
#define UCRT_XP_O_RDWR    0x0002
#define UCRT_XP_O_APPEND  0x0008
#define UCRT_XP_O_CREAT   0x0100
#define UCRT_XP_O_TRUNC   0x0200
#define UCRT_XP_O_EXCL    0x0400
#define UCRT_XP_O_TEXT    0x4000  /* LF<->CRLF translation at the stdio layer */
#define UCRT_XP_O_BINARY  0x8000

#define UCRT_XP_SEEK_SET  0
#define UCRT_XP_SEEK_CUR  1
#define UCRT_XP_SEEK_END  2

/* Low-level, unbuffered, descriptor-table-based I/O (the "_open/_read/
 * _write/_close/_lseek" layer). Descriptors are small non-negative ints,
 * independent of the raw Win32 HANDLE value, so code that assumes POSIX-
 * style fd semantics keeps working. */
__declspec(dllexport) int   __cdecl ucrt_xp_open(const char *path, int oflag, int pmode);
__declspec(dllexport) int   __cdecl ucrt_xp_close(int fd);
__declspec(dllexport) long  __cdecl ucrt_xp_read(int fd, void *buf, unsigned int count);
__declspec(dllexport) long  __cdecl ucrt_xp_write(int fd, const void *buf, unsigned int count);
__declspec(dllexport) __int64 __cdecl ucrt_xp_lseek(int fd, __int64 offset, int origin);

/* Buffered stream layer (the "FILE *" layer), built entirely on top of
 * the descriptor functions above - it never calls Win32 I/O directly. */
typedef struct UCRT_XP_FILE UCRT_XP_FILE;

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_fopen(const char *path, const char *mode);
__declspec(dllexport) int    __cdecl ucrt_xp_fclose(UCRT_XP_FILE *f);
__declspec(dllexport) size_t __cdecl ucrt_xp_fread(void *buf, size_t size, size_t count, UCRT_XP_FILE *f);
__declspec(dllexport) size_t __cdecl ucrt_xp_fwrite(const void *buf, size_t size, size_t count, UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fflush(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fseek(UCRT_XP_FILE *f, long offset, int origin);
__declspec(dllexport) long   __cdecl ucrt_xp_ftell(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fgetc(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fputc(int c, UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_feof(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_ferror(UCRT_XP_FILE *f);
__declspec(dllexport) void   __cdecl ucrt_xp_clearerr(UCRT_XP_FILE *f);
__declspec(dllexport) void   __cdecl ucrt_xp_rewind(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fileno(UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_ungetc(int c, UCRT_XP_FILE *f);
__declspec(dllexport) char*  __cdecl ucrt_xp_fgets(char *s, int n, UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fputs(const char *s, UCRT_XP_FILE *f);
__declspec(dllexport) int    __cdecl ucrt_xp_fprintf(UCRT_XP_FILE *f, const char *fmt, ...);

/* File-system helpers (thin Win32 wrappers). */
__declspec(dllexport) int __cdecl ucrt_xp_remove(const char *path);
__declspec(dllexport) int __cdecl ucrt_xp_rename(const char *oldpath, const char *newpath);

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_fdopen(int fd, const char *mode);
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_freopen(
    const char *path, const char *mode, UCRT_XP_FILE *f);
__declspec(dllexport) int  __cdecl ucrt_xp_setvbuf(UCRT_XP_FILE *f, char *buf, int mode, size_t size);
__declspec(dllexport) void __cdecl ucrt_xp_setbuf(UCRT_XP_FILE *f, char *buf);
__declspec(dllexport) int     __cdecl ucrt_xp_fseeki64(UCRT_XP_FILE *f, __int64 offset, int origin);
__declspec(dllexport) __int64 __cdecl ucrt_xp_ftelli64(UCRT_XP_FILE *f);

/* MSVC-style filesystem helpers. */
#ifndef UCRT_XP_STAT_DEFINED
#define UCRT_XP_STAT_DEFINED
typedef struct UCRT_XP_STAT {
    unsigned int st_mode;
    __int64      st_size;
    __int64      st_atime;
    __int64      st_mtime;
    __int64      st_ctime;
} UCRT_XP_STAT;
#endif

__declspec(dllexport) int   __cdecl ucrt_xp_access(const char *path, int mode);
__declspec(dllexport) int   __cdecl ucrt_xp_stat(const char *path, UCRT_XP_STAT *st);
__declspec(dllexport) int   __cdecl ucrt_xp_mkdir(const char *path);
__declspec(dllexport) int   __cdecl ucrt_xp_chdir(const char *path);
__declspec(dllexport) char* __cdecl ucrt_xp_getcwd(char *buf, int maxlen);

/* Standard streams - lazily-initialized accessors, not data exports.
 * See stdio.c's header comment on ucrt_xp_stdout() for why a function
 * call is used instead of an exported global. */
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stdin(void);
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stdout(void);
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stderr(void);

/* stdout-targeted convenience functions. */
__declspec(dllexport) int __cdecl ucrt_xp_printf(const char *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_puts(const char *s);
__declspec(dllexport) int __cdecl ucrt_xp_putchar(int c);
__declspec(dllexport) int __cdecl ucrt_xp_getchar(void);

/* ------------------------------------------------------------------ */
/* printf-family formatter (see format.c for supported conversions)    */
/* ------------------------------------------------------------------ */

#include <stdarg.h>

__declspec(dllexport) int __cdecl ucrt_xp_vsnprintf(
    char *buf, size_t bufsize, const char *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_snprintf(
    char *buf, size_t bufsize, const char *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_vsprintf(char *buf, const char *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_sprintf(char *buf, const char *fmt, ...);

/* scanf-family subset (see scan.c for supported conversions). */
__declspec(dllexport) int __cdecl ucrt_xp_vsscanf(const char *str, const char *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_sscanf(const char *str, const char *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_vfscanf(UCRT_XP_FILE *f, const char *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_fscanf(UCRT_XP_FILE *f, const char *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_scanf(const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* Minimal wide-character (wchar_t) layer                              */
/* ------------------------------------------------------------------ */

/*
 * Wide paths (_wopen/_wfopen), wcs* primitives, full wide printf, and
 * wide scanf (including scansets/%p). File-based wide I/O still routes
 * through the narrow stdio layer in text mode - see README.
 *
 * Naming note: the underlying symbols are ucrt_xp_wopen / ucrt_xp_wfopen.
 * MSVC CRT names these _wopen / _wfopen (leading underscore). The
 * UCRT_XP_USE_STD_NAMES macros in ucrt_xp_compat.h therefore expose
 * _wopen / _wfopen, not the underscore-less forms.
 */
__declspec(dllexport) int __cdecl ucrt_xp_wopen(const wchar_t *path, int oflag, int pmode);
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_wfopen(const wchar_t *path, const wchar_t *mode);

__declspec(dllexport) size_t __cdecl ucrt_xp_wcslen(const wchar_t *s);
__declspec(dllexport) size_t __cdecl ucrt_xp_wcsnlen(const wchar_t *s, size_t maxlen);
__declspec(dllexport) int    __cdecl ucrt_xp_wcscmp(const wchar_t *a, const wchar_t *b);
__declspec(dllexport) int    __cdecl ucrt_xp_wcsncmp(const wchar_t *a, const wchar_t *b, size_t n);
__declspec(dllexport) wchar_t* __cdecl ucrt_xp_wcscpy(wchar_t *dst, const wchar_t *src);
__declspec(dllexport) wchar_t* __cdecl ucrt_xp_wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
__declspec(dllexport) wchar_t* __cdecl ucrt_xp_wcscat(wchar_t *dst, const wchar_t *src);
__declspec(dllexport) int    __cdecl ucrt_xp_wcsicmp_l(
    const wchar_t *a, const wchar_t *b, ucrt_xp_locale_t loc);

/* Non-locale (thread-default locale) wide case-insensitive compare.
 * Mirrors MSVC's _wcsicmp. */
__declspec(dllexport) int __cdecl ucrt_xp_wcsicmp(
    const wchar_t *a, const wchar_t *b);

/* UTF-16 <-> ANSI (current locale's code page) conversion helpers. On
 * failure both return -1; on success, the number of wchar_t/char units
 * written (excluding the NUL). Passing out=NULL/outcap=0 returns the
 * required buffer size instead of writing, mirroring MultiByteToWideChar
 * / WideCharToMultiByte's own "query size" convention. */
__declspec(dllexport) int __cdecl ucrt_xp_ansi_to_wide(
    const char *in, wchar_t *out, int outcap);
__declspec(dllexport) int __cdecl ucrt_xp_wide_to_ansi(
    const wchar_t *in, char *out, int outcap);

/* Wide printf family. Mirrors ucrt_xp_vsnprintf's conversion set (see
 * format.c) but with wchar_t format string, output buffer, and %s
 * expecting a wchar_t* (the conventional wide-printf meaning). Shares
 * its integer/float-to-string core with the ANSI formatter, so the two
 * stay numerically consistent by construction. */
__declspec(dllexport) int __cdecl ucrt_xp_vswprintf(
    wchar_t *buf, size_t bufsize_chars, const wchar_t *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_swprintf(
    wchar_t *buf, size_t bufsize_chars, const wchar_t *fmt, ...);

/* Formats then writes through the ANSI stdio layer (ucrt_xp_fwrite),
 * converting the formatted wide text to the current thread locale's code
 * page first. This matches how a text-mode wide stream behaves on a
 * single-byte console/file in classic CRTs - it is NOT the same as
 * emitting raw UTF-16 to the file; use ucrt_xp_wfopen (compat: _wfopen)
 * + ucrt_xp_fwrite directly if you need actual UTF-16 bytes on disk. */
__declspec(dllexport) int __cdecl ucrt_xp_fwprintf(UCRT_XP_FILE *f, const wchar_t *fmt, ...);

/* Wide scanf family - same conversion set as the narrow scanf (including
 * scansets and %p). %ls/%lc take wchar_t*; plain %s/%c take char*.
 * File-based variants (fwscanf/wscanf) read via the narrow stdio layer
 * (one byte → one wchar), matching the project's text-mode model. */
__declspec(dllexport) int __cdecl ucrt_xp_vswscanf(
    const wchar_t *str, const wchar_t *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_swscanf(
    const wchar_t *str, const wchar_t *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_vfwscanf(
    UCRT_XP_FILE *f, const wchar_t *fmt, va_list args);
__declspec(dllexport) int __cdecl ucrt_xp_fwscanf(
    UCRT_XP_FILE *f, const wchar_t *fmt, ...);
__declspec(dllexport) int __cdecl ucrt_xp_wscanf(const wchar_t *fmt, ...);

/* ------------------------------------------------------------------ */
/* <string.h> family                                                   */
/* ------------------------------------------------------------------ */

__declspec(dllexport) void*  __cdecl ucrt_xp_memcpy(void *dst, const void *src, size_t n);
__declspec(dllexport) void*  __cdecl ucrt_xp_memmove(void *dst, const void *src, size_t n);
__declspec(dllexport) void*  __cdecl ucrt_xp_memset(void *dst, int c, size_t n);
__declspec(dllexport) int    __cdecl ucrt_xp_memcmp(const void *a, const void *b, size_t n);
__declspec(dllexport) void*  __cdecl ucrt_xp_memchr(const void *buf, int c, size_t n);

__declspec(dllexport) size_t __cdecl ucrt_xp_strlen(const char *s);
__declspec(dllexport) char*  __cdecl ucrt_xp_strcpy(char *dst, const char *src);
__declspec(dllexport) char*  __cdecl ucrt_xp_strncpy(char *dst, const char *src, size_t n);
__declspec(dllexport) char*  __cdecl ucrt_xp_strcat(char *dst, const char *src);
__declspec(dllexport) char*  __cdecl ucrt_xp_strncat(char *dst, const char *src, size_t n);
__declspec(dllexport) int    __cdecl ucrt_xp_strcmp(const char *a, const char *b);
__declspec(dllexport) int    __cdecl ucrt_xp_strncmp(const char *a, const char *b, size_t n);
__declspec(dllexport) int    __cdecl ucrt_xp_strnicmp(const char *a, const char *b, size_t n);
__declspec(dllexport) char*  __cdecl ucrt_xp_strchr(const char *s, int c);
__declspec(dllexport) char*  __cdecl ucrt_xp_strrchr(const char *s, int c);
__declspec(dllexport) char*  __cdecl ucrt_xp_strstr(const char *haystack, const char *needle);
__declspec(dllexport) char*  __cdecl ucrt_xp_strdup(const char *s);
/* Reentrant strtok - see string.c's comment on why this takes an
 * explicit saveptr instead of the classic CRT's hidden internal state. */
__declspec(dllexport) char*  __cdecl ucrt_xp_strtok_r(char *str, const char *delim, char **saveptr);

/* ------------------------------------------------------------------ */
/* <ctype.h> family (fixed "C"/ASCII locale - see ctype.c)             */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_isalpha(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isdigit(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isalnum(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isspace(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isupper(int c);
__declspec(dllexport) int __cdecl ucrt_xp_islower(int c);
__declspec(dllexport) int __cdecl ucrt_xp_ispunct(int c);
__declspec(dllexport) int __cdecl ucrt_xp_iscntrl(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isxdigit(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isprint(int c);
__declspec(dllexport) int __cdecl ucrt_xp_isgraph(int c);
__declspec(dllexport) int __cdecl ucrt_xp_toupper(int c);
__declspec(dllexport) int __cdecl ucrt_xp_tolower(int c);

/* ------------------------------------------------------------------ */
/* <stdlib.h>: numeric conversion, rand, qsort/bsearch                 */
/* ------------------------------------------------------------------ */

__declspec(dllexport) long          __cdecl ucrt_xp_strtol(const char *s, char **endptr, int base);
__declspec(dllexport) unsigned long __cdecl ucrt_xp_strtoul(const char *s, char **endptr, int base);
__declspec(dllexport) int           __cdecl ucrt_xp_atoi(const char *s);
__declspec(dllexport) long          __cdecl ucrt_xp_atol(const char *s);
__declspec(dllexport) double        __cdecl ucrt_xp_strtod(const char *s, char **endptr);
__declspec(dllexport) double        __cdecl ucrt_xp_atof(const char *s);
__declspec(dllexport) int           __cdecl ucrt_xp_abs(int v);
__declspec(dllexport) long          __cdecl ucrt_xp_labs(long v);

/* Integer -> string (MSVC _itoa / _ltoa / _ultoa). radix 2..36. */
__declspec(dllexport) char* __cdecl ucrt_xp_itoa(int value, char *str, int radix);
__declspec(dllexport) char* __cdecl ucrt_xp_ltoa(long value, char *str, int radix);
__declspec(dllexport) char* __cdecl ucrt_xp_ultoa(unsigned long value, char *str, int radix);

/* Always per-thread state - see convert.c's header comment for why this
 * deliberately differs from the classic CRT's ifdef-dependent rand(). */
__declspec(dllexport) void __cdecl ucrt_xp_srand(unsigned int seed);
__declspec(dllexport) int  __cdecl ucrt_xp_rand(void);

typedef int (__cdecl *UCRT_XP_COMPARE_FN)(const void *a, const void *b);
__declspec(dllexport) void  __cdecl ucrt_xp_qsort(
    void *base, size_t count, size_t size, UCRT_XP_COMPARE_FN cmp);
__declspec(dllexport) void* __cdecl ucrt_xp_bsearch(
    const void *key, const void *base, size_t count, size_t size, UCRT_XP_COMPARE_FN cmp);

/* ------------------------------------------------------------------ */
/* errno / exit / atexit                                               */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int* __cdecl ucrt_xp_errno_location(void);
__declspec(dllexport) int  __cdecl ucrt_xp_get_errno(void);
__declspec(dllexport) void __cdecl ucrt_xp_set_errno(int value);
__declspec(dllexport) char* __cdecl ucrt_xp_strerror(int errnum);
__declspec(dllexport) void  __cdecl ucrt_xp_perror(const char *s);

typedef void (__cdecl *UCRT_XP_ATEXIT_FN)(void);
__declspec(dllexport) int  __cdecl ucrt_xp_atexit(UCRT_XP_ATEXIT_FN fn);
__declspec(dllexport) void __cdecl ucrt_xp_exit(int code);
__declspec(dllexport) void __cdecl ucrt_xp_abort(void);

__declspec(dllexport) char* __cdecl ucrt_xp_getenv(const char *name);
__declspec(dllexport) int   __cdecl ucrt_xp_system(const char *command);

/* ------------------------------------------------------------------ */
/* <time.h>: 64-bit time_t, thread-safe localtime/gmtime               */
/* ------------------------------------------------------------------ */

/* Deliberately 64-bit (unlike the classic CRT's 32-bit time_t, which
 * overflows in 2038) - see time.c's header comment. */
typedef __int64 UCRT_XP_TIME_T;

typedef struct UCRT_XP_TM {
    int tm_sec;    /* 0-60 (60 for a leap second, if the OS ever reports one) */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11 */
    int tm_year;   /* years since 1900, same convention as struct tm */
    int tm_wday;   /* 0-6, Sunday = 0 */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* always -1 ("unknown") - see time.c */
} UCRT_XP_TM;

__declspec(dllexport) UCRT_XP_TIME_T __cdecl ucrt_xp_time(UCRT_XP_TIME_T *out);
__declspec(dllexport) unsigned long  __cdecl ucrt_xp_clock(void);
__declspec(dllexport) BOOL __cdecl ucrt_xp_gmtime(const UCRT_XP_TIME_T *timer, UCRT_XP_TM *out);
__declspec(dllexport) BOOL __cdecl ucrt_xp_localtime(const UCRT_XP_TIME_T *timer, UCRT_XP_TM *out);
__declspec(dllexport) int  __cdecl ucrt_xp_compute_yday(int year, int mon0, int mday);
__declspec(dllexport) size_t __cdecl ucrt_xp_strftime(
    char *buf, size_t bufsize, const char *fmt, const UCRT_XP_TM *tm);

#ifdef __cplusplus
}
#endif

#endif /* UCRT_XP_H */
