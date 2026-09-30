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
#undef malloc
#define malloc(size)              ucrt_xp_malloc(size)
#undef calloc
#define calloc(count, size)       ucrt_xp_calloc(count, size)
#undef realloc
#define realloc(ptr, size)        ucrt_xp_realloc(ptr, size)
#undef free
#define free(ptr)                 ucrt_xp_free(ptr)
#undef atoi
#define atoi(s)                   ucrt_xp_atoi(s)
#undef atol
#define atol(s)                   ucrt_xp_atol(s)
#undef atof
#define atof(s)                   ucrt_xp_atof(s)
#undef strtol
#define strtol(s, e, b)           ucrt_xp_strtol(s, e, b)
#undef strtoul
#define strtoul(s, e, b)          ucrt_xp_strtoul(s, e, b)
#undef strtod
#define strtod(s, e)              ucrt_xp_strtod(s, e)
#undef strtoll
#define strtoll(s, e, b)          ucrt_xp_strtoll(s, e, b)
#undef strtoull
#define strtoull(s, e, b)         ucrt_xp_strtoull(s, e, b)
#undef _strtoi64
#define _strtoi64(s, e, b)        ucrt_xp_strtoi64(s, e, b)
#undef _strtoui64
#define _strtoui64(s, e, b)       ucrt_xp_strtoui64(s, e, b)
#undef strtof
#define strtof(s, e)              ucrt_xp_strtof(s, e)
#undef strtold
#define strtold(s, e)             ucrt_xp_strtold(s, e)
#undef _wtof
#define _wtof(s)                  ucrt_xp_wtof(s)
#undef rand_s
#define rand_s(p)                 ucrt_xp_rand_s(p)
#undef _searchenv
#define _searchenv(f, v, p)       ucrt_xp_searchenv(f, v, p)
#undef _dupenv_s
#define _dupenv_s(b, n, v)        ucrt_xp_dupenv_s(b, n, v)

#undef abs
#define abs(v)                    ucrt_xp_abs(v)
#undef labs
#define labs(v)                   ucrt_xp_labs(v)
#undef srand
#define srand(seed)               ucrt_xp_srand(seed)
#undef rand
#define rand()                    ucrt_xp_rand()
#undef qsort
#define qsort(base, n, sz, cmp)   ucrt_xp_qsort(base, n, sz, cmp)
#undef bsearch
#define bsearch(k, base, n, sz, cmp) ucrt_xp_bsearch(k, base, n, sz, cmp)
#undef exit
#define exit(code)                ucrt_xp_exit(code)
#undef abort
#define abort()                   ucrt_xp_abort()
#undef atexit
#define atexit(fn)                ucrt_xp_atexit(fn)
#undef getenv
#define getenv(n)                 ucrt_xp_getenv(n)
#undef system
#define system(c)                 ucrt_xp_system(c)
#undef _getpid
#define _getpid()                 ucrt_xp_getpid()
#undef getpid
#define getpid()                  ucrt_xp_getpid()
#undef _putenv
#define _putenv(s)                ucrt_xp_putenv(s)
#undef _pipe
#define _pipe(pd, sz, tm)         ucrt_xp_pipe(pd, sz, tm)
#undef _popen
#define _popen(c, m)              ucrt_xp_popen(c, m)
#undef _pclose
#define _pclose(f)                ucrt_xp_pclose(f)
#undef popen
#define popen(c, m)               ucrt_xp_popen(c, m)
#undef pclose
#define pclose(f)                 ucrt_xp_pclose(f)
#undef _spawnv
#define _spawnv(m, c, a)          ucrt_xp_spawnv(m, c, a)
#undef _spawnvp
#define _spawnvp(m, c, a)         ucrt_xp_spawnvp(m, c, a)
#undef _spawnl
#define _spawnl                   ucrt_xp_spawnl
#undef _spawnlp
#define _spawnlp                  ucrt_xp_spawnlp
#undef _execv
#define _execv(c, a)              ucrt_xp_execv(c, a)
#undef _execvp
#define _execvp(c, a)             ucrt_xp_execvp(c, a)
#undef _execl
#define _execl                    ucrt_xp_execl
#undef _execlp
#define _execlp                   ucrt_xp_execlp
#undef _beginthread
#define _beginthread              ucrt_xp_beginthread
#undef _beginthreadex
#define _beginthreadex            ucrt_xp_beginthreadex
#undef _endthread
#define _endthread()              ucrt_xp_endthread()
#undef _endthreadex
#define _endthreadex(r)           ucrt_xp_endthreadex(r)
#ifndef P_WAIT
#undef P_WAIT
#define P_WAIT    UCRT_XP_P_WAIT
#undef P_NOWAIT
#define P_NOWAIT  UCRT_XP_P_NOWAIT
#undef P_OVERLAY
#define P_OVERLAY UCRT_XP_P_OVERLAY
#undef P_NOWAITO
#define P_NOWAITO UCRT_XP_P_NOWAITO
#undef P_DETACH
#define P_DETACH  UCRT_XP_P_DETACH
#endif
#undef _itoa
#define _itoa(v, s, r)            ucrt_xp_itoa(v, s, r)
#undef _ltoa
#define _ltoa(v, s, r)            ucrt_xp_ltoa(v, s, r)
#undef _ultoa
#define _ultoa(v, s, r)           ucrt_xp_ultoa(v, s, r)

/* ------------------------------------------------------------------ */
/* <stdio.h>  (see caveat 2 above re: FILE*)                          */
/* ------------------------------------------------------------------ */
#undef fopen
#define fopen(path, mode)         ucrt_xp_fopen(path, mode)
#undef fclose
#define fclose(f)                 ucrt_xp_fclose(f)
#undef fread
#define fread(buf, sz, n, f)      ucrt_xp_fread(buf, sz, n, f)
#undef fwrite
#define fwrite(buf, sz, n, f)     ucrt_xp_fwrite(buf, sz, n, f)
#undef fflush
#define fflush(f)                 ucrt_xp_fflush(f)
#undef fseek
#define fseek(f, off, origin)     ucrt_xp_fseek(f, off, origin)
#undef ftell
#define ftell(f)                  ucrt_xp_ftell(f)
#undef fgetc
#define fgetc(f)                  ucrt_xp_fgetc(f)
#undef fputc
#define fputc(c, f)               ucrt_xp_fputc(c, f)
#undef feof
#define feof(f)                   ucrt_xp_feof(f)
#undef ferror
#define ferror(f)                 ucrt_xp_ferror(f)
#undef clearerr
#define clearerr(f)               ucrt_xp_clearerr(f)
#undef rewind
#define rewind(f)                 ucrt_xp_rewind(f)
#undef fileno
#define fileno(f)                 ucrt_xp_fileno(f)
#undef ungetc
#define ungetc(c, f)              ucrt_xp_ungetc(c, f)
#undef fgets
#define fgets(s, n, f)            ucrt_xp_fgets(s, n, f)
#undef fputs
#define fputs(s, f)               ucrt_xp_fputs(s, f)
#undef fprintf
#define fprintf                   ucrt_xp_fprintf
#undef sprintf
#define sprintf                   ucrt_xp_sprintf
#undef vsprintf
#define vsprintf                  ucrt_xp_vsprintf
#undef snprintf
#define snprintf                  ucrt_xp_snprintf
#undef vsnprintf
#define vsnprintf                 ucrt_xp_vsnprintf
#undef sscanf
#define sscanf                    ucrt_xp_sscanf
#undef fscanf
#define fscanf                    ucrt_xp_fscanf
#undef scanf
#define scanf                     ucrt_xp_scanf
#undef remove
#define remove(path)              ucrt_xp_remove(path)
#undef rename
#define rename(oldp, newp)        ucrt_xp_rename(oldp, newp)
#undef fdopen
#define fdopen(fd, mode)          ucrt_xp_fdopen(fd, mode)
#undef freopen
#define freopen(path, mode, f)    ucrt_xp_freopen(path, mode, f)
#undef setvbuf
#define setvbuf(f, buf, mode, sz) ucrt_xp_setvbuf(f, buf, mode, sz)
#undef setbuf
#define setbuf(f, buf)            ucrt_xp_setbuf(f, buf)
#undef perror
#define perror(s)                 ucrt_xp_perror(s)

/* Standard streams - function-call accessors under the hood (see
 * stdio.c), but usable as plain `stdout`/`stderr`/`stdin` expressions
 * here, same as real CRTs that implement them as macros around a
 * function call (e.g. modern MSVC's __acrt_iob_func-based stdout). */
#undef stdin
#define stdin                     ucrt_xp_stdin()
#undef stdout
#define stdout                    ucrt_xp_stdout()
#undef stderr
#define stderr                    ucrt_xp_stderr()
#undef printf
#define printf                    ucrt_xp_printf
#undef puts
#define puts(s)                   ucrt_xp_puts(s)
#undef putchar
#define putchar(c)                ucrt_xp_putchar(c)
#undef getchar
#define getchar()                 ucrt_xp_getchar()

#ifndef SEEK_SET
#undef SEEK_SET
#define SEEK_SET                  UCRT_XP_SEEK_SET
#endif
#ifndef SEEK_CUR
#undef SEEK_CUR
#define SEEK_CUR                  UCRT_XP_SEEK_CUR
#endif
#ifndef SEEK_END
#undef SEEK_END
#define SEEK_END                  UCRT_XP_SEEK_END
#endif

/* MSVC low-level I/O and path extensions */
#undef _open
#define _open(path, oflag, pmode) ucrt_xp_open(path, oflag, pmode)
#undef _close
#define _close(fd)                ucrt_xp_close(fd)
#undef _read
#define _read(fd, buf, count)     ucrt_xp_read(fd, buf, count)
#undef _write
#define _write(fd, buf, count)    ucrt_xp_write(fd, buf, count)
#undef _lseek
#define _lseek(fd, off, origin)   ucrt_xp_lseek(fd, off, origin)
#undef _fseeki64
#define _fseeki64(f, off, org)    ucrt_xp_fseeki64(f, off, org)
#undef _ftelli64
#define _ftelli64(f)              ucrt_xp_ftelli64(f)
#undef _access
#define _access(path, mode)       ucrt_xp_access(path, mode)
#undef _findfirst
#define _findfirst(s, d)          ucrt_xp_findfirst(s, d)
#undef _findnext
#define _findnext(h, d)           ucrt_xp_findnext(h, d)
#undef _findclose
#define _findclose(h)             ucrt_xp_findclose(h)

#undef _stat
#define _stat(path, st)           ucrt_xp_stat(path, st)
#undef _mkdir
#define _mkdir(path)              ucrt_xp_mkdir(path)
#undef _chdir
#define _chdir(path)              ucrt_xp_chdir(path)
#undef _getcwd
#define _getcwd(buf, n)           ucrt_xp_getcwd(buf, n)

/* ------------------------------------------------------------------ */
/* <string.h>                                                         */
/* ------------------------------------------------------------------ */
#undef memcpy
#define memcpy(d, s, n)           ucrt_xp_memcpy(d, s, n)
#undef memmove
#define memmove(d, s, n)          ucrt_xp_memmove(d, s, n)
#undef memset
#define memset(d, c, n)           ucrt_xp_memset(d, c, n)
#undef memcmp
#define memcmp(a, b, n)           ucrt_xp_memcmp(a, b, n)
#undef memchr
#define memchr(s, c, n)           ucrt_xp_memchr(s, c, n)
#undef strlen
#define strlen(s)                 ucrt_xp_strlen(s)
#undef strcpy
#define strcpy(d, s)              ucrt_xp_strcpy(d, s)
#undef strncpy
#define strncpy(d, s, n)          ucrt_xp_strncpy(d, s, n)
#undef strcat
#define strcat(d, s)              ucrt_xp_strcat(d, s)
#undef strncat
#define strncat(d, s, n)          ucrt_xp_strncat(d, s, n)
#undef strcmp
#define strcmp(a, b)              ucrt_xp_strcmp(a, b)
#undef strncmp
#define strncmp(a, b, n)          ucrt_xp_strncmp(a, b, n)
#undef strchr
#define strchr(s, c)              ucrt_xp_strchr(s, c)
#undef strrchr
#define strrchr(s, c)             ucrt_xp_strrchr(s, c)
#undef strstr
#define strstr(h, n)              ucrt_xp_strstr(h, n)
#undef strspn
#define strspn(s, a)              ucrt_xp_strspn(s, a)
#undef strcspn
#define strcspn(s, r)             ucrt_xp_strcspn(s, r)
#undef strpbrk
#define strpbrk(s, a)             ucrt_xp_strpbrk(s, a)
#undef strnlen
#define strnlen(s, n)             ucrt_xp_strnlen(s, n)
#undef strerror
#define strerror(e)               ucrt_xp_strerror(e)
/* Reentrant form only - classic strtok() with hidden static state is
 * intentionally not provided. Callers should use strtok_r. */
#undef strtok_r
#define strtok_r(s, d, sp)        ucrt_xp_strtok_r(s, d, sp)
#undef _stricmp
#define _stricmp(a, b)            ucrt_xp_stricmp(a, b)
#undef _strnicmp
#define _strnicmp(a, b, n)        ucrt_xp_strnicmp(a, b, n)
#undef _strdup
#define _strdup(s)                ucrt_xp_strdup(s)
#undef memccpy
#define memccpy(d, s, c, n)       ucrt_xp_memccpy(d, s, c, n)
#undef _strlwr
#define _strlwr(s)                ucrt_xp_strlwr(s)
#undef _strupr
#define _strupr(s)                ucrt_xp_strupr(s)
#undef strcoll
#define strcoll(a, b)             ucrt_xp_strcoll(a, b)
#undef strxfrm
#define strxfrm(d, s, n)          ucrt_xp_strxfrm(d, s, n)
#undef _stricoll
#define _stricoll(a, b)           ucrt_xp_stricoll(a, b)
#undef _strnicoll
#define _strnicoll(a, b, n)       ucrt_xp_strnicoll(a, b, n)
#undef _memicmp
#define _memicmp(a, b, n)         ucrt_xp_memicmp(a, b, n)
#undef _strrev
#define _strrev(s)                ucrt_xp_strrev(s)
#undef strcasecmp
#define strcasecmp(a, b)          ucrt_xp_strcasecmp(a, b)
#undef strncasecmp
#define strncasecmp(a, b, n)      ucrt_xp_strncasecmp(a, b, n)

/* ------------------------------------------------------------------ */
/* <ctype.h>                                                          */
/* ------------------------------------------------------------------ */
#undef isalpha
#define isalpha(c)                ucrt_xp_isalpha(c)
#undef isdigit
#define isdigit(c)                ucrt_xp_isdigit(c)
#undef isalnum
#define isalnum(c)                ucrt_xp_isalnum(c)
#undef isspace
#define isspace(c)                ucrt_xp_isspace(c)
#undef isupper
#define isupper(c)                ucrt_xp_isupper(c)
#undef islower
#define islower(c)                ucrt_xp_islower(c)
#undef ispunct
#define ispunct(c)                ucrt_xp_ispunct(c)
#undef iscntrl
#define iscntrl(c)                ucrt_xp_iscntrl(c)
#undef isxdigit
#define isxdigit(c)               ucrt_xp_isxdigit(c)
#undef isprint
#define isprint(c)                ucrt_xp_isprint(c)
#undef isgraph
#define isgraph(c)                ucrt_xp_isgraph(c)
#undef toupper
#define toupper(c)                ucrt_xp_toupper(c)
#undef tolower
#define tolower(c)                ucrt_xp_tolower(c)
#undef isascii
#define isascii(c)                ucrt_xp_isascii(c)
#undef __isascii
#define __isascii(c)              ucrt_xp_isascii(c)
#undef toascii
#define toascii(c)                ucrt_xp_toascii(c)
#undef __toascii
#define __toascii(c)              ucrt_xp_toascii(c)
#undef isblank
#define isblank(c)                ucrt_xp_isblank(c)
#undef __iscsymf
#define __iscsymf(c)              ucrt_xp_iscsymf(c)
#undef iscsymf
#define iscsymf(c)                ucrt_xp_iscsymf(c)
#undef __iscsym
#define __iscsym(c)               ucrt_xp_iscsym(c)
#undef iscsym
#define iscsym(c)                 ucrt_xp_iscsym(c)
#undef _tolower
#define _tolower(c)               ucrt_xp__tolower(c)
#undef _toupper
#define _toupper(c)               ucrt_xp__toupper(c)
#undef iswalpha
#define iswalpha(c)               ucrt_xp_iswalpha(c)
#undef iswdigit
#define iswdigit(c)               ucrt_xp_iswdigit(c)
#undef iswalnum
#define iswalnum(c)               ucrt_xp_iswalnum(c)
#undef iswspace
#define iswspace(c)               ucrt_xp_iswspace(c)
#undef iswupper
#define iswupper(c)               ucrt_xp_iswupper(c)
#undef iswlower
#define iswlower(c)               ucrt_xp_iswlower(c)
#undef iswpunct
#define iswpunct(c)               ucrt_xp_iswpunct(c)
#undef iswcntrl
#define iswcntrl(c)               ucrt_xp_iswcntrl(c)
#undef iswxdigit
#define iswxdigit(c)              ucrt_xp_iswxdigit(c)
#undef iswprint
#define iswprint(c)               ucrt_xp_iswprint(c)
#undef iswgraph
#define iswgraph(c)               ucrt_xp_iswgraph(c)
#undef iswblank
#define iswblank(c)               ucrt_xp_iswblank(c)
#undef iswascii
#define iswascii(c)               ucrt_xp_iswascii(c)
#undef towupper
#define towupper(c)               ucrt_xp_towupper(c)
#undef towlower
#define towlower(c)               ucrt_xp_towlower(c)
#undef __iswcsymf
#define __iswcsymf(c)             ucrt_xp_iswcsymf(c)
#undef __iswcsym
#define __iswcsym(c)              ucrt_xp_iswcsym(c)


/* ------------------------------------------------------------------ */
/* errno                                                              */
/* ------------------------------------------------------------------ */
#undef errno
#define errno                     (*ucrt_xp_errno_location())

/* ------------------------------------------------------------------ */
/* <time.h>
 * time()/clock()/strftime map cleanly. localtime()/gmtime() do NOT:
 * ucrt_xp versions take an explicit out-parameter and return BOOL
 * (thread-safe by design), so their signatures differ from the classic
 * CRT and cannot be macro-aliased without breaking call sites. Use
 * ucrt_xp_localtime / ucrt_xp_gmtime explicitly. */
/* ------------------------------------------------------------------ */
#undef time
#define time(t)                   ucrt_xp_time(t)
#undef clock
#define clock()                   ucrt_xp_clock()
#undef strftime
#define strftime(buf, n, fmt, tm) ucrt_xp_strftime(buf, n, fmt, tm)
#undef asctime
#define asctime(tm)               ucrt_xp_asctime(tm)
#undef ctime
#define ctime(t)                  ucrt_xp_ctime(t)
#undef _strdate
#define _strdate(b)               ucrt_xp_strdate(b)
#undef _strtime
#define _strtime(b)               ucrt_xp_strtime(b)


/* ------------------------------------------------------------------ */
/* wchar_t family
 * MSVC uses leading underscores for wide-path open (_wopen/_wfopen)
 * and case-insensitive compare (_wcsicmp). */
/* ------------------------------------------------------------------ */
#undef wcslen
#define wcslen(s)                 ucrt_xp_wcslen(s)
#undef wcsnlen
#define wcsnlen(s, n)             ucrt_xp_wcsnlen(s, n)
#undef wcscmp
#define wcscmp(a, b)              ucrt_xp_wcscmp(a, b)
#undef wcsncmp
#define wcsncmp(a, b, n)          ucrt_xp_wcsncmp(a, b, n)
#undef wcscpy
#define wcscpy(dst, src)          ucrt_xp_wcscpy(dst, src)
#undef wcsncpy
#define wcsncpy(dst, src, n)      ucrt_xp_wcsncpy(dst, src, n)
#undef wcscat
#define wcscat(dst, src)          ucrt_xp_wcscat(dst, src)
#undef wcsncat
#define wcsncat(dst, src, n)      ucrt_xp_wcsncat(dst, src, n)
#undef wcschr
#define wcschr(s, c)              ucrt_xp_wcschr(s, c)
#undef wcsrchr
#define wcsrchr(s, c)             ucrt_xp_wcsrchr(s, c)
#undef wcsstr
#define wcsstr(h, n)              ucrt_xp_wcsstr(h, n)
#undef wcsspn
#define wcsspn(s, a)              ucrt_xp_wcsspn(s, a)
#undef wcscspn
#define wcscspn(s, r)             ucrt_xp_wcscspn(s, r)
#undef wcspbrk
#define wcspbrk(s, a)             ucrt_xp_wcspbrk(s, a)
#undef _wcsnicmp
#define _wcsnicmp(a, b, n)        ucrt_xp_wcsnicmp(a, b, n)
#undef _wcsdup
#define _wcsdup(s)                ucrt_xp_wcsdup(s)
#undef wmemcpy
#define wmemcpy(d, s, n)          ucrt_xp_wmemcpy(d, s, n)
#undef wmemmove
#define wmemmove(d, s, n)         ucrt_xp_wmemmove(d, s, n)
#undef wmemset
#define wmemset(d, c, n)          ucrt_xp_wmemset(d, c, n)
#undef wmemcmp
#define wmemcmp(a, b, n)          ucrt_xp_wmemcmp(a, b, n)
#undef wmemchr
#define wmemchr(s, c, n)          ucrt_xp_wmemchr(s, c, n)
#undef swprintf
#define swprintf                  ucrt_xp_swprintf
#undef vswprintf
#define vswprintf                 ucrt_xp_vswprintf
#undef fwprintf
#define fwprintf                  ucrt_xp_fwprintf
#undef swscanf
#define swscanf                   ucrt_xp_swscanf
#undef vswscanf
#define vswscanf                  ucrt_xp_vswscanf
#undef fwscanf
#define fwscanf                   ucrt_xp_fwscanf
#undef vfwscanf
#define vfwscanf                  ucrt_xp_vfwscanf
#undef wscanf
#define wscanf                    ucrt_xp_wscanf
#undef _wopen
#define _wopen(path, oflag, pmode) ucrt_xp_wopen(path, oflag, pmode)
#undef _wfopen
#define _wfopen(path, mode)       ucrt_xp_wfopen(path, mode)
#undef _wcsicmp
#define _wcsicmp(a, b)            ucrt_xp_wcsicmp(a, b)
#undef wcsxfrm
#define wcsxfrm(d, s, n)          ucrt_xp_wcsxfrm(d, s, n)
#undef _wcslwr
#define _wcslwr(s)                ucrt_xp_wcslwr(s)
#undef _wcsupr
#define _wcsupr(s)                ucrt_xp_wcsupr(s)
#undef wcscoll
#define wcscoll(a, b)             ucrt_xp_wcscoll(a, b)
#undef _wcsicoll
#define _wcsicoll(a, b)           ucrt_xp_wcsicoll(a, b)
#undef _wcsnicoll
#define _wcsnicoll(a, b, n)       ucrt_xp_wcsnicoll(a, b, n)
#undef _wcsrev
#define _wcsrev(s)                ucrt_xp_wcsrev(s)
#undef _waccess
#define _waccess(p, m)            ucrt_xp_waccess(p, m)
#undef _wmkdir
#define _wmkdir(p)                ucrt_xp_wmkdir(p)
#undef _wchdir
#define _wchdir(p)                ucrt_xp_wchdir(p)
#undef _wgetcwd
#define _wgetcwd(b, n)            ucrt_xp_wgetcwd(b, n)
#undef _wremove
#define _wremove(p)               ucrt_xp_wremove(p)
#undef _wrename
#define _wrename(o, n)            ucrt_xp_wrename(o, n)
#undef _wgetenv
#define _wgetenv(n)               ucrt_xp_wgetenv(n)
#undef _wputenv
#define _wputenv(s)               ucrt_xp_wputenv(s)
#undef _wsystem
#define _wsystem(c)               ucrt_xp_wsystem(c)


#undef _dup
#define _dup(fd)                  ucrt_xp_dup(fd)
#undef _dup2
#define _dup2(a, b)               ucrt_xp_dup2(a, b)
#undef _setmode
#define _setmode(fd, m)           ucrt_xp_setmode(fd, m)
#undef _commit
#define _commit(fd)               ucrt_xp_commit(fd)
#undef _get_osfhandle
#define _get_osfhandle(fd)        ucrt_xp_get_osfhandle(fd)
#undef _open_osfhandle
#define _open_osfhandle(h, f)     ucrt_xp_open_osfhandle(h, f)
#undef _eof
#define _eof(fd)                  ucrt_xp_eof(fd)
#undef _lseeki64
#define _lseeki64(fd, o, w)       ucrt_xp_lseeki64(fd, o, w)
#undef _telli64
#define _telli64(fd)              ucrt_xp_telli64(fd)
#undef _tell
#define _tell(fd)                 ucrt_xp_tell(fd)
#undef fgetpos
#define fgetpos(f, p)             ucrt_xp_fgetpos(f, p)
#undef fsetpos
#define fsetpos(f, p)             ucrt_xp_fsetpos(f, p)
#undef tmpnam
#define tmpnam(s)                 ucrt_xp_tmpnam(s)
#undef tmpfile
#define tmpfile()                 ucrt_xp_tmpfile()
#undef _fullpath
#define _fullpath(a, r, n)        ucrt_xp_fullpath(a, r, n)
#undef _splitpath
#define _splitpath(p, d, di, f, e) ucrt_xp_splitpath(p, d, di, f, e)
#undef _makepath
#define _makepath(p, d, di, f, e) ucrt_xp_makepath(p, d, di, f, e)
#undef _rmdir
#define _rmdir(p)                 ucrt_xp_rmdir(p)
#undef _unlink
#define _unlink(p)                ucrt_xp_unlink(p)
#undef unlink
#define unlink(p)                 ucrt_xp_unlink(p)
#undef setlocale
#define setlocale(c, l)           ucrt_xp_setlocale(c, l)
#undef localeconv
#define localeconv()              ucrt_xp_localeconv()
#undef atoll
#define atoll(s)                  ucrt_xp_atoll(s)
#undef _atoi64
#define _atoi64(s)                ucrt_xp_atoi64(s)
#undef _i64toa
#define _i64toa(v, s, r)          ucrt_xp_i64toa(v, s, r)
#undef _ui64toa
#define _ui64toa(v, s, r)         ucrt_xp_ui64toa(v, s, r)
#undef div
#define div(n, d)                 ucrt_xp_div(n, d)
#undef ldiv
#define ldiv(n, d)                ucrt_xp_ldiv(n, d)
#undef llabs
#define llabs(v)                  ucrt_xp_llabs(v)
#undef mblen
#define mblen(s, n)               ucrt_xp_mblen(s, n)
#undef mbtowc
#define mbtowc(p, s, n)           ucrt_xp_mbtowc(p, s, n)
#undef wctomb
#define wctomb(s, w)              ucrt_xp_wctomb(s, w)
#undef mbstowcs
#define mbstowcs(w, m, n)         ucrt_xp_mbstowcs(w, m, n)
#undef wcstombs
#define wcstombs(m, w, n)         ucrt_xp_wcstombs(m, w, n)
#undef _wpopen
#define _wpopen(c, m)             ucrt_xp_wpopen(c, m)
#undef _wfindfirst
#define _wfindfirst(s, d)         ucrt_xp_wfindfirst(s, d)
#undef _wfindnext
#define _wfindnext(h, d)          ucrt_xp_wfindnext(h, d)
#undef _wfindclose
#define _wfindclose(h)            ucrt_xp_wfindclose(h)

#undef assert
#define assert(e)                 UCRT_XP_ASSERT(e)
#undef wctype
#define wctype(p)                 ucrt_xp_wctype(p)
#undef iswctype
#define iswctype(c, d)            ucrt_xp_iswctype(c, d)


#undef memcpy_s
#define memcpy_s(d, ds, s, n)     ucrt_xp_memcpy_s(d, ds, s, n)
#undef memmove_s
#define memmove_s(d, ds, s, n)    ucrt_xp_memmove_s(d, ds, s, n)
#undef strcpy_s
#define strcpy_s(d, n, s)         ucrt_xp_strcpy_s(d, n, s)
#undef strncpy_s
#define strncpy_s(d, n, s, c)     ucrt_xp_strncpy_s(d, n, s, c)
#undef strcat_s
#define strcat_s(d, n, s)         ucrt_xp_strcat_s(d, n, s)
#undef strncat_s
#define strncat_s(d, n, s, c)     ucrt_xp_strncat_s(d, n, s, c)
#undef strnlen_s
#define strnlen_s(s, n)           ucrt_xp_strnlen_s(s, n)
#undef strtok_s
#define strtok_s(s, d, c)         ucrt_xp_strtok_s(s, d, c)
#undef _strlwr_s
#define _strlwr_s(s, n)           ucrt_xp_strlwr_s(s, n)
#undef _strupr_s
#define _strupr_s(s, n)           ucrt_xp_strupr_s(s, n)
#undef wcscpy_s
#define wcscpy_s(d, n, s)         ucrt_xp_wcscpy_s(d, n, s)
#undef wcsncpy_s
#define wcsncpy_s(d, n, s, c)     ucrt_xp_wcsncpy_s(d, n, s, c)
#undef wcscat_s
#define wcscat_s(d, n, s)         ucrt_xp_wcscat_s(d, n, s)
#undef wcsncat_s
#define wcsncat_s(d, n, s, c)     ucrt_xp_wcsncat_s(d, n, s, c)
#undef wcsnlen_s
#define wcsnlen_s(s, n)           ucrt_xp_wcsnlen_s(s, n)
#undef wcstok_s
#define wcstok_s(s, d, c)         ucrt_xp_wcstok_s(s, d, c)
#undef _wcslwr_s
#define _wcslwr_s(s, n)           ucrt_xp_wcslwr_s(s, n)
#undef _wcsupr_s
#define _wcsupr_s(s, n)           ucrt_xp_wcsupr_s(s, n)
#undef sprintf_s
#define sprintf_s                 ucrt_xp_sprintf_s
#undef vsprintf_s
#define vsprintf_s                ucrt_xp_vsprintf_s
#undef snprintf_s
#define snprintf_s                ucrt_xp_snprintf_s
#undef vsnprintf_s
#define vsnprintf_s               ucrt_xp_vsnprintf_s
#undef swprintf_s
#define swprintf_s                ucrt_xp_swprintf_s
#undef vswprintf_s
#define vswprintf_s               ucrt_xp_vswprintf_s
#undef fopen_s
#define fopen_s(p, f, m)          ucrt_xp_fopen_s(p, f, m)
#undef _wfopen_s
#define _wfopen_s(p, f, m)        ucrt_xp_wfopen_s(p, f, m)
#undef getenv_s
#define getenv_s(r, b, n, v)      ucrt_xp_getenv_s(r, b, n, v)
#undef tmpnam_s
#define tmpnam_s(s, n)            ucrt_xp_tmpnam_s(s, n)
#undef gets_s
#define gets_s(b, n)              ucrt_xp_gets_s(b, n)
#undef localtime_s
#define localtime_s(t, tm)        ucrt_xp_localtime_s(t, tm)
#undef gmtime_s
#define gmtime_s(t, tm)           ucrt_xp_gmtime_s(t, tm)
#undef asctime_s
#define asctime_s(b, n, t)        ucrt_xp_asctime_s(b, n, t)
#undef ctime_s
#define ctime_s(b, n, t)          ucrt_xp_ctime_s(b, n, t)
#undef mbstowcs_s
#define mbstowcs_s                ucrt_xp_mbstowcs_s
#undef wcstombs_s
#define wcstombs_s                ucrt_xp_wcstombs_s
#undef qsort_s
#define qsort_s                   ucrt_xp_qsort_s
#undef bsearch_s
#define bsearch_s                 ucrt_xp_bsearch_s

#undef freopen_s
#define freopen_s(p, path, m, s)  ucrt_xp_freopen_s(p, path, m, s)
#undef _itoa_s
#define _itoa_s(v, b, n, r)       ucrt_xp_itoa_s(v, b, n, r)
#undef _ltoa_s
#define _ltoa_s(v, b, n, r)       ucrt_xp_ltoa_s(v, b, n, r)
#undef _ultoa_s
#define _ultoa_s(v, b, n, r)      ucrt_xp_ultoa_s(v, b, n, r)
#undef _i64toa_s
#define _i64toa_s(v, b, n, r)     ucrt_xp_i64toa_s(v, b, n, r)
#undef _ui64toa_s
#define _ui64toa_s(v, b, n, r)    ucrt_xp_ui64toa_s(v, b, n, r)
#undef _splitpath_s
#define _splitpath_s              ucrt_xp_splitpath_s
#undef _makepath_s
#define _makepath_s               ucrt_xp_makepath_s
#undef sscanf_s
#define sscanf_s                  ucrt_xp_sscanf_s
#undef vsscanf_s
#define vsscanf_s                 ucrt_xp_vsscanf_s
#undef fscanf_s
#define fscanf_s                  ucrt_xp_fscanf_s
#undef vfscanf_s
#define vfscanf_s                 ucrt_xp_vfscanf_s
#undef scanf_s
#define scanf_s                   ucrt_xp_scanf_s

#undef swscanf_s
#define swscanf_s                 ucrt_xp_swscanf_s
#undef vswscanf_s
#define vswscanf_s                ucrt_xp_vswscanf_s
#undef fwscanf_s
#define fwscanf_s                 ucrt_xp_fwscanf_s
#undef vfwscanf_s
#define vfwscanf_s                ucrt_xp_vfwscanf_s
#undef wscanf_s
#define wscanf_s                  ucrt_xp_wscanf_s
#undef _snwprintf_s
#define _snwprintf_s              ucrt_xp_snwprintf_s

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
