/*
 * ucrt_xp_compat.h - the "make it usable without renaming everything"
 * shortcut. Existing code written against the standard CRT can, for
 * the functions covered below, get ucrt_xp's stability guarantees by
 * changing a #include and defining one macro - no find-and-replace of
 * every call site required.
 *
 * USAGE:
 *     #define UCRT_XP_USE_STD_NAMES
 *     #include "ucrt_xp_compat.h"
 *
 *     void *p = malloc(64);       // -> ucrt_xp_malloc(64)
 *     UCRT_XP_FILE *f = fopen("x.txt", "w"); // -> ucrt_xp_fopen(...)
 *     fprintf(f, "%d\n", 42);     // -> ucrt_xp_fprintf(...)
 *     free(p);
 *
 * IMPORTANT CAVEATS (read before using this header):
 *
 *   1. Do NOT also include <stdio.h>/<stdlib.h> in the same translation
 *      unit after this header with UCRT_XP_USE_STD_NAMES defined - the
 *      function-like macros below will rewrite the CRT's own
 *      declarations and produce very confusing errors. Either use only
 *      this header's functions in that file, or don't define
 *      UCRT_XP_USE_STD_NAMES and call the ucrt_xp_* names explicitly
 *      instead (which always works, in any file, with no caveats).
 *
 *   2. FILE* is deliberately NOT aliased. `fopen()` here returns
 *      UCRT_XP_FILE*, not FILE* - write `UCRT_XP_FILE *f = fopen(...)`.
 *      Aliasing the FILE type itself is what causes the worst conflicts
 *      if a real <stdio.h> is anywhere in the same compilation; keeping
 *      the type name distinct while aliasing the function names is the
 *      safer middle ground.
 *
 *   3. This header does not (and cannot) alias operators like `sizeof`
 *      quirks or change struct layouts - it is purely a set of
 *      preprocessor renames over the functions ucrt_xp.h already
 *      exports. Read ucrt_xp.h's per-function comments for actual
 *      behavioral differences from the real CRT (e.g. text-mode CRLF
 *      handling, the printf conversion subset).
 *
 * A second, independent switch, UCRT_XP_USE_VISTA_NAMES, gives Vista's
 * CONDITION_VARIABLE / InitOnceExecuteOnce family - see the section near
 * the end of this file.
 *
 * Without either switch defined, including this header is a no-op beyond
 * pulling in ucrt_xp.h - safe to include unconditionally.
 */
#ifndef UCRT_XP_COMPAT_H
#define UCRT_XP_COMPAT_H

#include "ucrt_xp.h"

#ifdef UCRT_XP_USE_STD_NAMES

/* ------------------------------------------------------------------ */
/* <stdlib.h>                                                         */
/* ------------------------------------------------------------------ */
#define malloc(size)              ucrt_xp_malloc(size)
#define calloc(count, size)       ucrt_xp_calloc(count, size)
#define realloc(ptr, size)        ucrt_xp_realloc(ptr, size)
#define free(ptr)                 ucrt_xp_free(ptr)
#define atoi(s)                   ucrt_xp_atoi(s)
#define atol(s)                   ucrt_xp_atol(s)
#define atof(s)                   ucrt_xp_atof(s)
#define strtol(s, e, b)           ucrt_xp_strtol(s, e, b)
#define strtoul(s, e, b)          ucrt_xp_strtoul(s, e, b)
#define strtod(s, e)              ucrt_xp_strtod(s, e)
#define abs(v)                    ucrt_xp_abs(v)
#define labs(v)                   ucrt_xp_labs(v)
#define srand(seed)               ucrt_xp_srand(seed)
#define rand()                    ucrt_xp_rand()
#define qsort(base, n, sz, cmp)   ucrt_xp_qsort(base, n, sz, cmp)
#define bsearch(k, base, n, sz, cmp) ucrt_xp_bsearch(k, base, n, sz, cmp)
#define exit(code)                ucrt_xp_exit(code)
#define abort()                   ucrt_xp_abort()
#define atexit(fn)                ucrt_xp_atexit(fn)
#define getenv(n)                 ucrt_xp_getenv(n)
#define system(c)                 ucrt_xp_system(c)
#define _itoa(v, s, r)            ucrt_xp_itoa(v, s, r)
#define _ltoa(v, s, r)            ucrt_xp_ltoa(v, s, r)
#define _ultoa(v, s, r)           ucrt_xp_ultoa(v, s, r)

/* ------------------------------------------------------------------ */
/* <stdio.h>  (see caveat 2 above re: FILE*)                          */
/* ------------------------------------------------------------------ */
#define fopen(path, mode)         ucrt_xp_fopen(path, mode)
#define fclose(f)                 ucrt_xp_fclose(f)
#define fread(buf, sz, n, f)      ucrt_xp_fread(buf, sz, n, f)
#define fwrite(buf, sz, n, f)     ucrt_xp_fwrite(buf, sz, n, f)
#define fflush(f)                 ucrt_xp_fflush(f)
#define fseek(f, off, origin)     ucrt_xp_fseek(f, off, origin)
#define ftell(f)                  ucrt_xp_ftell(f)
#define fgetc(f)                  ucrt_xp_fgetc(f)
#define fputc(c, f)               ucrt_xp_fputc(c, f)
#define feof(f)                   ucrt_xp_feof(f)
#define ferror(f)                 ucrt_xp_ferror(f)
#define clearerr(f)               ucrt_xp_clearerr(f)
#define rewind(f)                 ucrt_xp_rewind(f)
#define fileno(f)                 ucrt_xp_fileno(f)
#define ungetc(c, f)              ucrt_xp_ungetc(c, f)
#define fgets(s, n, f)            ucrt_xp_fgets(s, n, f)
#define fputs(s, f)               ucrt_xp_fputs(s, f)
#define fprintf                   ucrt_xp_fprintf
#define sprintf                   ucrt_xp_sprintf
#define vsprintf                  ucrt_xp_vsprintf
#define snprintf                  ucrt_xp_snprintf
#define vsnprintf                 ucrt_xp_vsnprintf
#define sscanf                    ucrt_xp_sscanf
#define fscanf                    ucrt_xp_fscanf
#define scanf                     ucrt_xp_scanf
#define remove(path)              ucrt_xp_remove(path)
#define rename(oldp, newp)        ucrt_xp_rename(oldp, newp)
#define fdopen(fd, mode)          ucrt_xp_fdopen(fd, mode)
#define freopen(path, mode, f)    ucrt_xp_freopen(path, mode, f)
#define setvbuf(f, buf, mode, sz) ucrt_xp_setvbuf(f, buf, mode, sz)
#define setbuf(f, buf)            ucrt_xp_setbuf(f, buf)
#define perror(s)                 ucrt_xp_perror(s)

/* Standard streams - function-call accessors under the hood (see
 * stdio.c), but usable as plain `stdout`/`stderr`/`stdin` expressions
 * here, same as real CRTs that implement them as macros around a
 * function call (e.g. modern MSVC's __acrt_iob_func-based stdout). */
#define stdin                     ucrt_xp_stdin()
#define stdout                    ucrt_xp_stdout()
#define stderr                    ucrt_xp_stderr()
#define printf                    ucrt_xp_printf
#define puts(s)                   ucrt_xp_puts(s)
#define putchar(c)                ucrt_xp_putchar(c)
#define getchar()                 ucrt_xp_getchar()

#ifndef SEEK_SET
#define SEEK_SET                  UCRT_XP_SEEK_SET
#endif
#ifndef SEEK_CUR
#define SEEK_CUR                  UCRT_XP_SEEK_CUR
#endif
#ifndef SEEK_END
#define SEEK_END                  UCRT_XP_SEEK_END
#endif

/* MSVC low-level I/O and path extensions */
#define _open(path, oflag, pmode) ucrt_xp_open(path, oflag, pmode)
#define _close(fd)                ucrt_xp_close(fd)
#define _read(fd, buf, count)     ucrt_xp_read(fd, buf, count)
#define _write(fd, buf, count)    ucrt_xp_write(fd, buf, count)
#define _lseek(fd, off, origin)   ucrt_xp_lseek(fd, off, origin)
#define _fseeki64(f, off, org)    ucrt_xp_fseeki64(f, off, org)
#define _ftelli64(f)              ucrt_xp_ftelli64(f)
#define _access(path, mode)       ucrt_xp_access(path, mode)
#define _stat(path, st)           ucrt_xp_stat(path, st)
#define _mkdir(path)              ucrt_xp_mkdir(path)
#define _chdir(path)              ucrt_xp_chdir(path)
#define _getcwd(buf, n)           ucrt_xp_getcwd(buf, n)

/* ------------------------------------------------------------------ */
/* <string.h>                                                         */
/* ------------------------------------------------------------------ */
#define memcpy(d, s, n)           ucrt_xp_memcpy(d, s, n)
#define memmove(d, s, n)          ucrt_xp_memmove(d, s, n)
#define memset(d, c, n)           ucrt_xp_memset(d, c, n)
#define memcmp(a, b, n)           ucrt_xp_memcmp(a, b, n)
#define memchr(s, c, n)           ucrt_xp_memchr(s, c, n)
#define strlen(s)                 ucrt_xp_strlen(s)
#define strcpy(d, s)              ucrt_xp_strcpy(d, s)
#define strncpy(d, s, n)          ucrt_xp_strncpy(d, s, n)
#define strcat(d, s)              ucrt_xp_strcat(d, s)
#define strncat(d, s, n)          ucrt_xp_strncat(d, s, n)
#define strcmp(a, b)              ucrt_xp_strcmp(a, b)
#define strncmp(a, b, n)          ucrt_xp_strncmp(a, b, n)
#define strchr(s, c)              ucrt_xp_strchr(s, c)
#define strrchr(s, c)             ucrt_xp_strrchr(s, c)
#define strstr(h, n)              ucrt_xp_strstr(h, n)
#define strspn(s, a)              ucrt_xp_strspn(s, a)
#define strcspn(s, r)             ucrt_xp_strcspn(s, r)
#define strpbrk(s, a)             ucrt_xp_strpbrk(s, a)
#define strnlen(s, n)             ucrt_xp_strnlen(s, n)
#define strerror(e)               ucrt_xp_strerror(e)
/* Reentrant form only - classic strtok() with hidden static state is
 * intentionally not provided. Callers should use strtok_r. */
#define strtok_r(s, d, sp)        ucrt_xp_strtok_r(s, d, sp)
#define _stricmp(a, b)            ucrt_xp_stricmp(a, b)
#define _strnicmp(a, b, n)        ucrt_xp_strnicmp(a, b, n)
#define _strdup(s)                ucrt_xp_strdup(s)
#define memccpy(d, s, c, n)       ucrt_xp_memccpy(d, s, c, n)
#define _strlwr(s)                ucrt_xp_strlwr(s)
#define _strupr(s)                ucrt_xp_strupr(s)
#define strcoll(a, b)             ucrt_xp_strcoll(a, b)
#define strxfrm(d, s, n)          ucrt_xp_strxfrm(d, s, n)

/* ------------------------------------------------------------------ */
/* <ctype.h>                                                          */
/* ------------------------------------------------------------------ */
#define isalpha(c)                ucrt_xp_isalpha(c)
#define isdigit(c)                ucrt_xp_isdigit(c)
#define isalnum(c)                ucrt_xp_isalnum(c)
#define isspace(c)                ucrt_xp_isspace(c)
#define isupper(c)                ucrt_xp_isupper(c)
#define islower(c)                ucrt_xp_islower(c)
#define ispunct(c)                ucrt_xp_ispunct(c)
#define iscntrl(c)                ucrt_xp_iscntrl(c)
#define isxdigit(c)               ucrt_xp_isxdigit(c)
#define isprint(c)                ucrt_xp_isprint(c)
#define isgraph(c)                ucrt_xp_isgraph(c)
#define toupper(c)                ucrt_xp_toupper(c)
#define tolower(c)                ucrt_xp_tolower(c)

/* ------------------------------------------------------------------ */
/* errno                                                              */
/* ------------------------------------------------------------------ */
#define errno                     (*ucrt_xp_errno_location())

/* ------------------------------------------------------------------ */
/* <time.h>
 * time()/clock()/strftime map cleanly. localtime()/gmtime() do NOT:
 * ucrt_xp versions take an explicit out-parameter and return BOOL
 * (thread-safe by design), so their signatures differ from the classic
 * CRT and cannot be macro-aliased without breaking call sites. Use
 * ucrt_xp_localtime / ucrt_xp_gmtime explicitly. */
/* ------------------------------------------------------------------ */
#define time(t)                   ucrt_xp_time(t)
#define clock()                   ucrt_xp_clock()
#define strftime(buf, n, fmt, tm) ucrt_xp_strftime(buf, n, fmt, tm)

/* ------------------------------------------------------------------ */
/* wchar_t family
 * MSVC uses leading underscores for wide-path open (_wopen/_wfopen)
 * and case-insensitive compare (_wcsicmp). */
/* ------------------------------------------------------------------ */
#define wcslen(s)                 ucrt_xp_wcslen(s)
#define wcsnlen(s, n)             ucrt_xp_wcsnlen(s, n)
#define wcscmp(a, b)              ucrt_xp_wcscmp(a, b)
#define wcsncmp(a, b, n)          ucrt_xp_wcsncmp(a, b, n)
#define wcscpy(dst, src)          ucrt_xp_wcscpy(dst, src)
#define wcsncpy(dst, src, n)      ucrt_xp_wcsncpy(dst, src, n)
#define wcscat(dst, src)          ucrt_xp_wcscat(dst, src)
#define wcsncat(dst, src, n)      ucrt_xp_wcsncat(dst, src, n)
#define wcschr(s, c)              ucrt_xp_wcschr(s, c)
#define wcsrchr(s, c)             ucrt_xp_wcsrchr(s, c)
#define wcsstr(h, n)              ucrt_xp_wcsstr(h, n)
#define wcsspn(s, a)              ucrt_xp_wcsspn(s, a)
#define wcscspn(s, r)             ucrt_xp_wcscspn(s, r)
#define wcspbrk(s, a)             ucrt_xp_wcspbrk(s, a)
#define _wcsnicmp(a, b, n)        ucrt_xp_wcsnicmp(a, b, n)
#define _wcsdup(s)                ucrt_xp_wcsdup(s)
#define wcstok_s(s, d, sp)        ucrt_xp_wcstok_r(s, d, sp)
#define wmemcpy(d, s, n)          ucrt_xp_wmemcpy(d, s, n)
#define wmemmove(d, s, n)         ucrt_xp_wmemmove(d, s, n)
#define wmemset(d, c, n)          ucrt_xp_wmemset(d, c, n)
#define wmemcmp(a, b, n)          ucrt_xp_wmemcmp(a, b, n)
#define wmemchr(s, c, n)          ucrt_xp_wmemchr(s, c, n)
#define swprintf                  ucrt_xp_swprintf
#define vswprintf                 ucrt_xp_vswprintf
#define fwprintf                  ucrt_xp_fwprintf
#define swscanf                   ucrt_xp_swscanf
#define vswscanf                  ucrt_xp_vswscanf
#define fwscanf                   ucrt_xp_fwscanf
#define vfwscanf                  ucrt_xp_vfwscanf
#define wscanf                    ucrt_xp_wscanf
#define _wopen(path, oflag, pmode) ucrt_xp_wopen(path, oflag, pmode)
#define _wfopen(path, mode)       ucrt_xp_wfopen(path, mode)
#define _wcsicmp(a, b)            ucrt_xp_wcsicmp(a, b)
#define wcsxfrm(d, s, n)          ucrt_xp_wcsxfrm(d, s, n)
#define _wcslwr(s)                ucrt_xp_wcslwr(s)
#define _wcsupr(s)                ucrt_xp_wcsupr(s)

#endif /* UCRT_XP_USE_STD_NAMES */

/* ------------------------------------------------------------------ */
/* Vista-era synchronization names (separate, opt-in switch)           */
/*                                                                     */
/*     #define UCRT_XP_USE_VISTA_NAMES                                 */
/*     #include "ucrt_xp_compat.h"                                     */
/*                                                                     */
/*     static CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;         */
/*     SleepConditionVariableCS(&cv, &cs, INFINITE);                   */
/*     InitOnceExecuteOnce(&once, MyInitFn, NULL, NULL);               */
/*                                                                     */
/* Code written for Vista+ then builds unchanged and runs on XP: the   */
/* OS implementation is used where it exists, ucrt_xp's otherwise.     */
/*                                                                     */
/* CAVEAT (same spirit as UCRT_XP_USE_STD_NAMES above): these macros   */
/* rewrite the real Win32 names. Define the switch only in files that  */
/* are compiled with _WIN32_WINNT < 0x0600 (the default for this       */
/* project, where the SDK does not declare these APIs at all) and that */
/* do not include Vista+ SDK declarations of them after this header.   */
/* Not supported: SleepConditionVariableSRW, InitOnceBeginInitialize/  */
/* InitOnceComplete - see ucrt_xp.h.                                   */
/* ------------------------------------------------------------------ */
#ifdef UCRT_XP_USE_VISTA_NAMES

#undef CONDITION_VARIABLE
#undef PCONDITION_VARIABLE
#undef INIT_ONCE
#undef PINIT_ONCE
#undef PINIT_ONCE_FN
#undef CONDITION_VARIABLE_INIT
#undef INIT_ONCE_STATIC_INIT
#define CONDITION_VARIABLE                  UCRT_XP_CONDITION_VARIABLE
#define PCONDITION_VARIABLE                 UCRT_XP_CONDITION_VARIABLE *
#define CONDITION_VARIABLE_INIT             UCRT_XP_CONDITION_VARIABLE_INIT
#define INIT_ONCE                           UCRT_XP_INIT_ONCE
#define PINIT_ONCE                          UCRT_XP_INIT_ONCE *
#define PINIT_ONCE_FN                       UCRT_XP_INIT_ONCE_FN
#define INIT_ONCE_STATIC_INIT               UCRT_XP_INIT_ONCE_STATIC_INIT

#define InitializeConditionVariable(cv)         ucrt_xp_InitializeConditionVariable(cv)
#define SleepConditionVariableCS(cv, cs, ms)    ucrt_xp_SleepConditionVariableCS(cv, cs, ms)
#define WakeConditionVariable(cv)               ucrt_xp_WakeConditionVariable(cv)
#define WakeAllConditionVariable(cv)            ucrt_xp_WakeAllConditionVariable(cv)
#define InitOnceInitialize(once)                ucrt_xp_InitOnceInitialize(once)
#define InitOnceExecuteOnce(once, fn, p, ctx)   ucrt_xp_InitOnceExecuteOnce(once, fn, p, ctx)

#endif /* UCRT_XP_USE_VISTA_NAMES */

#endif /* UCRT_XP_COMPAT_H */
