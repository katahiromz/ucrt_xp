/*
 * secure.c - MSVC-style *_s "secure CRT" wrappers.
 * Return 0 on success; EINVAL (22) / ERANGE (34) on failure.
 * On constraint violation, dest is zeroed when size > 0 (string APIs).
 */
#include "internal.h"
#include <stdarg.h>

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ERANGE
#define ERANGE 34
#endif
#ifndef STRUNCATE
#define STRUNCATE 80
#endif

/* Optional truncate mode for strncpy_s / wcsncpy_s (MSVC). */
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

static void set_errno_s(int e)
{
    ucrt_xp_set_errno(e);
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_memcpy_s(void *dest, size_t destsz, const void *src, size_t count)
{
    if (!dest && destsz != 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src && count != 0) { set_errno_s(EINVAL); return EINVAL; }
    if (count > destsz) {
        if (dest && destsz) ucrt_xp_memset(dest, 0, destsz);
        set_errno_s(ERANGE);
        return ERANGE;
    }
    if (count) ucrt_xp_memcpy(dest, src, count);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_memmove_s(void *dest, size_t destsz, const void *src, size_t count)
{
    if (!dest && destsz != 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src && count != 0) { set_errno_s(EINVAL); return EINVAL; }
    if (count > destsz) {
        if (dest && destsz) ucrt_xp_memset(dest, 0, destsz);
        set_errno_s(ERANGE);
        return ERANGE;
    }
    if (count) ucrt_xp_memmove(dest, src, count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Narrow strings                                                      */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_strcpy_s(char *dest, size_t destsz, const char *src)
{
    size_t n;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strlen(src);
    if (n + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(dest, src, n + 1);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_strncpy_s(char *dest, size_t destsz, const char *src, size_t count)
{
    size_t n, copy;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }

    n = ucrt_xp_strnlen(src, count == _TRUNCATE ? destsz : count);

    if (count == _TRUNCATE) {
        if (n + 1 > destsz) {
            if (destsz > 0) {
                ucrt_xp_memcpy(dest, src, destsz - 1);
                dest[destsz - 1] = 0;
            }
            return STRUNCATE;
        }
        ucrt_xp_memcpy(dest, src, n);
        dest[n] = 0;
        return 0;
    }

    if (count >= destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    copy = n < count ? n : count;
    if (copy) ucrt_xp_memcpy(dest, src, copy);
    dest[copy] = 0;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_strcat_s(char *dest, size_t destsz, const char *src)
{
    size_t dlen, slen;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { set_errno_s(EINVAL); return EINVAL; }
    dlen = ucrt_xp_strnlen(dest, destsz);
    if (dlen >= destsz) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    slen = ucrt_xp_strlen(src);
    if (dlen + slen + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(dest + dlen, src, slen + 1);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_strncat_s(char *dest, size_t destsz, const char *src, size_t count)
{
    size_t dlen, n, copy;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { set_errno_s(EINVAL); return EINVAL; }
    dlen = ucrt_xp_strnlen(dest, destsz);
    if (dlen >= destsz) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strnlen(src, count);
    if (dlen + n + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    copy = n;
    if (copy) ucrt_xp_memcpy(dest + dlen, src, copy);
    dest[dlen + copy] = 0;
    return 0;
}

__declspec(dllexport) size_t __cdecl ucrt_xp_strnlen_s(const char *s, size_t maxsize)
{
    if (!s) return 0;
    return ucrt_xp_strnlen(s, maxsize);
}

__declspec(dllexport) char* __cdecl ucrt_xp_strtok_s(char *str, const char *delim, char **context)
{
    if (!delim || !context) { set_errno_s(EINVAL); return NULL; }
    return ucrt_xp_strtok_r(str, delim, context);
}

__declspec(dllexport) int __cdecl ucrt_xp_strlwr_s(char *s, size_t sizeInBytes)
{
    size_t n;
    if (!s || sizeInBytes == 0) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strnlen(s, sizeInBytes);
    if (n >= sizeInBytes) { set_errno_s(EINVAL); return EINVAL; }
    ucrt_xp_strlwr(s);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_strupr_s(char *s, size_t sizeInBytes)
{
    size_t n;
    if (!s || sizeInBytes == 0) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strnlen(s, sizeInBytes);
    if (n >= sizeInBytes) { set_errno_s(EINVAL); return EINVAL; }
    ucrt_xp_strupr(s);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Wide strings                                                        */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_wcscpy_s(wchar_t *dest, size_t destsz, const wchar_t *src)
{
    size_t n;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_wcslen(src);
    if (n + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_wmemcpy(dest, src, n + 1);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wcsncpy_s(wchar_t *dest, size_t destsz, const wchar_t *src, size_t count)
{
    size_t n, copy;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_wcsnlen(src, count == _TRUNCATE ? destsz : count);
    if (count == _TRUNCATE) {
        if (n + 1 > destsz) {
            if (destsz > 0) {
                ucrt_xp_wmemcpy(dest, src, destsz - 1);
                dest[destsz - 1] = 0;
            }
            return STRUNCATE;
        }
        ucrt_xp_wmemcpy(dest, src, n);
        dest[n] = 0;
        return 0;
    }
    if (count >= destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    copy = n < count ? n : count;
    if (copy) ucrt_xp_wmemcpy(dest, src, copy);
    dest[copy] = 0;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wcscat_s(wchar_t *dest, size_t destsz, const wchar_t *src)
{
    size_t dlen, slen;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { set_errno_s(EINVAL); return EINVAL; }
    dlen = ucrt_xp_wcsnlen(dest, destsz);
    if (dlen >= destsz) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    slen = ucrt_xp_wcslen(src);
    if (dlen + slen + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_wmemcpy(dest + dlen, src, slen + 1);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wcsncat_s(wchar_t *dest, size_t destsz, const wchar_t *src, size_t count)
{
    size_t dlen, n;
    if (!dest || destsz == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!src) { set_errno_s(EINVAL); return EINVAL; }
    dlen = ucrt_xp_wcsnlen(dest, destsz);
    if (dlen >= destsz) { dest[0] = 0; set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_wcsnlen(src, count);
    if (dlen + n + 1 > destsz) {
        dest[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    if (n) ucrt_xp_wmemcpy(dest + dlen, src, n);
    dest[dlen + n] = 0;
    return 0;
}

__declspec(dllexport) size_t __cdecl ucrt_xp_wcsnlen_s(const wchar_t *s, size_t maxsize)
{
    if (!s) return 0;
    return ucrt_xp_wcsnlen(s, maxsize);
}

__declspec(dllexport) wchar_t* __cdecl ucrt_xp_wcstok_s(wchar_t *str, const wchar_t *delim, wchar_t **context)
{
    if (!delim || !context) { set_errno_s(EINVAL); return NULL; }
    return ucrt_xp_wcstok_r(str, delim, context);
}

__declspec(dllexport) int __cdecl ucrt_xp_wcslwr_s(wchar_t *s, size_t sizeInWords)
{
    size_t n;
    if (!s || sizeInWords == 0) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_wcsnlen(s, sizeInWords);
    if (n >= sizeInWords) { set_errno_s(EINVAL); return EINVAL; }
    ucrt_xp_wcslwr(s);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wcsupr_s(wchar_t *s, size_t sizeInWords)
{
    size_t n;
    if (!s || sizeInWords == 0) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_wcsnlen(s, sizeInWords);
    if (n >= sizeInWords) { set_errno_s(EINVAL); return EINVAL; }
    ucrt_xp_wcsupr(s);
    return 0;
}

/* ------------------------------------------------------------------ */
/* printf_s family                                                     */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_vsprintf_s(char *buf, size_t bufsz, const char *fmt, va_list args)
{
    int n;
    if (!buf || bufsz == 0 || !fmt) { set_errno_s(EINVAL); return -1; }
    n = ucrt_xp_vsnprintf(buf, bufsz, fmt, args);
    if (n < 0 || (size_t)n >= bufsz) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return -1;
    }
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_sprintf_s(char *buf, size_t bufsz, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vsprintf_s(buf, bufsz, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_vsnprintf_s(char *buf, size_t bufsz, size_t count, const char *fmt, va_list args)
{
    size_t lim;
    int n;
    if (!fmt) { set_errno_s(EINVAL); return -1; }
    if (count == _TRUNCATE) {
        if (!buf || bufsz == 0) return -1;
        n = ucrt_xp_vsnprintf(buf, bufsz, fmt, args);
        if (n < 0) return -1;
        if ((size_t)n >= bufsz) return -1; /* truncated; MSVC returns -1 */
        return n;
    }
    lim = count < bufsz ? count + 1 : bufsz;
    if (!buf || lim == 0) { set_errno_s(EINVAL); return -1; }
    n = ucrt_xp_vsnprintf(buf, lim, fmt, args);
    if (n < 0 || (size_t)n >= lim) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return -1;
    }
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_snprintf_s(char *buf, size_t bufsz, size_t count, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vsnprintf_s(buf, bufsz, count, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_vswprintf_s(wchar_t *buf, size_t bufsz, const wchar_t *fmt, va_list args)
{
    int n;
    if (!buf || bufsz == 0 || !fmt) { set_errno_s(EINVAL); return -1; }
    n = ucrt_xp_vswprintf(buf, bufsz, fmt, args);
    if (n < 0 || (size_t)n >= bufsz) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return -1;
    }
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_swprintf_s(wchar_t *buf, size_t bufsz, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vswprintf_s(buf, bufsz, fmt, ap);
    va_end(ap);
    return n;
}

/* ------------------------------------------------------------------ */
/* File / env / tmp                                                    */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_fopen_s(UCRT_XP_FILE **pfile, const char *filename, const char *mode)
{
    if (!pfile) { set_errno_s(EINVAL); return EINVAL; }
    *pfile = NULL;
    if (!filename || !mode) { set_errno_s(EINVAL); return EINVAL; }
    *pfile = ucrt_xp_fopen(filename, mode);
    if (!*pfile) { set_errno_s(EINVAL); return EINVAL; }
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wfopen_s(UCRT_XP_FILE **pfile, const wchar_t *filename, const wchar_t *mode)
{
    if (!pfile) { set_errno_s(EINVAL); return EINVAL; }
    *pfile = NULL;
    if (!filename || !mode) { set_errno_s(EINVAL); return EINVAL; }
    *pfile = ucrt_xp_wfopen(filename, mode);
    if (!*pfile) { set_errno_s(EINVAL); return EINVAL; }
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_getenv_s(size_t *requiredCount, char *buffer, size_t bufferCount, const char *varname)
{
    char *v;
    size_t len;
    if (!varname) { set_errno_s(EINVAL); return EINVAL; }
    v = ucrt_xp_getenv(varname);
    if (!v) {
        if (requiredCount) *requiredCount = 0;
        if (buffer && bufferCount) buffer[0] = 0;
        return 0;
    }
    len = ucrt_xp_strlen(v) + 1;
    if (requiredCount) *requiredCount = len;
    if (!buffer && bufferCount == 0) return 0;
    if (!buffer || bufferCount == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (len > bufferCount) {
        buffer[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buffer, v, len);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_tmpnam_s(char *s, size_t size)
{
    char buf[MAX_PATH];
    size_t n;
    if (!s || size == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!ucrt_xp_tmpnam(buf)) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strlen(buf) + 1;
    if (n > size) { s[0] = 0; set_errno_s(ERANGE); return ERANGE; }
    ucrt_xp_memcpy(s, buf, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_gets_s(char *buf, size_t sizeInCharacters)
{
    size_t i = 0;
    int c;
    if (!buf || sizeInCharacters == 0) { set_errno_s(EINVAL); return EINVAL; }
    if (sizeInCharacters == 1) { buf[0] = 0; return 0; }
    while (i < sizeInCharacters - 1) {
        c = ucrt_xp_getchar();
        if (c == -1 || c == '\n') break;
        buf[i++] = (char)c;
    }
    buf[i] = 0;
    if (c == -1 && i == 0) return EINVAL;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Time _s                                                             */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_localtime_s(UCRT_XP_TM *tm, const UCRT_XP_TIME_T *timer)
{
    if (!tm || !timer) { set_errno_s(EINVAL); return EINVAL; }
    if (!ucrt_xp_localtime(timer, tm)) { set_errno_s(EINVAL); return EINVAL; }
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_gmtime_s(UCRT_XP_TM *tm, const UCRT_XP_TIME_T *timer)
{
    if (!tm || !timer) { set_errno_s(EINVAL); return EINVAL; }
    if (!ucrt_xp_gmtime(timer, tm)) { set_errno_s(EINVAL); return EINVAL; }
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_asctime_s(char *buf, size_t bufsz, const UCRT_XP_TM *tm)
{
    char *p;
    size_t n;
    if (!buf || bufsz < 26 || !tm) { set_errno_s(EINVAL); return EINVAL; }
    p = ucrt_xp_asctime(tm);
    if (!p) { set_errno_s(EINVAL); return EINVAL; }
    n = ucrt_xp_strlen(p) + 1;
    if (n > bufsz) { buf[0] = 0; set_errno_s(ERANGE); return ERANGE; }
    ucrt_xp_memcpy(buf, p, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_ctime_s(char *buf, size_t bufsz, const UCRT_XP_TIME_T *timer)
{
    UCRT_XP_TM tm;
    if (!buf || bufsz < 26 || !timer) { set_errno_s(EINVAL); return EINVAL; }
    if (!ucrt_xp_localtime(timer, &tm)) { set_errno_s(EINVAL); return EINVAL; }
    return ucrt_xp_asctime_s(buf, bufsz, &tm);
}

/* ------------------------------------------------------------------ */
/* Multibyte _s                                                        */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_mbstowcs_s(size_t *retval, wchar_t *wcstr, size_t sizeInWords, const char *mbstr, size_t count)
{
    size_t n, lim;
    if (!mbstr) { set_errno_s(EINVAL); return EINVAL; }
    lim = (count == _TRUNCATE || count > sizeInWords) ? sizeInWords : count + 1;
    if (!wcstr && sizeInWords > 0) { set_errno_s(EINVAL); return EINVAL; }
    if (!wcstr || sizeInWords == 0) {
        n = ucrt_xp_mbstowcs(NULL, mbstr, 0);
        if (retval) *retval = n + 1;
        return 0;
    }
    n = ucrt_xp_mbstowcs(wcstr, mbstr, sizeInWords);
    if (n == (size_t)-1) {
        wcstr[0] = 0;
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (n >= sizeInWords) {
        wcstr[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    if (retval) *retval = n + 1;
    (void)lim;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_wcstombs_s(size_t *retval, char *mbstr, size_t sizeInBytes, const wchar_t *wcstr, size_t count)
{
    size_t n;
    if (!wcstr) { set_errno_s(EINVAL); return EINVAL; }
    if (!mbstr || sizeInBytes == 0) {
        n = ucrt_xp_wcstombs(NULL, wcstr, 0);
        if (retval) *retval = n + 1;
        return 0;
    }
    n = ucrt_xp_wcstombs(mbstr, wcstr, sizeInBytes);
    if (n == (size_t)-1) {
        mbstr[0] = 0;
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (n >= sizeInBytes) {
        mbstr[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    if (retval) *retval = n + 1;
    (void)count;
    return 0;
}

/* ------------------------------------------------------------------ */
/* qsort_s / bsearch_s                                                 */
/* ------------------------------------------------------------------ */

typedef int (__cdecl *UCRT_XP_QSORT_S_CTX)(void *ctx, const void *a, const void *b);
typedef int (__cdecl *UCRT_XP_BSEARCH_S_CTX)(void *ctx, const void *a, const void *b);

typedef struct {
    UCRT_XP_QSORT_S_CTX cmp;
    void *ctx;
} QsortSWrap;

static QsortSWrap *g_qsort_s_wrap = NULL;

static int __cdecl qsort_s_bridge(const void *a, const void *b)
{
    return g_qsort_s_wrap->cmp(g_qsort_s_wrap->ctx, a, b);
}

__declspec(dllexport) void __cdecl ucrt_xp_qsort_s(void *base, size_t num, size_t width,
    int (__cdecl *compare)(void *, const void *, const void *), void *context)
{
    QsortSWrap wrap;
    QsortSWrap *prev;
    if (!base || !compare || width == 0) return;
    wrap.cmp = compare;
    wrap.ctx = context;
    prev = g_qsort_s_wrap;
    g_qsort_s_wrap = &wrap;
    ucrt_xp_qsort(base, num, width, qsort_s_bridge);
    g_qsort_s_wrap = prev;
}

typedef struct {
    int (__cdecl *cmp)(void *, const void *, const void *);
    void *ctx;
} BsearchSWrap;

static BsearchSWrap *g_bsearch_s_wrap = NULL;

static int __cdecl bsearch_s_bridge(const void *a, const void *b)
{
    return g_bsearch_s_wrap->cmp(g_bsearch_s_wrap->ctx, a, b);
}

__declspec(dllexport) void* __cdecl ucrt_xp_bsearch_s(const void *key, const void *base, size_t num, size_t width,
    int (__cdecl *compare)(void *, const void *, const void *), void *context)
{
    BsearchSWrap wrap;
    BsearchSWrap *prev;
    void *r;
    if (!key || !base || !compare || width == 0) return NULL;
    wrap.cmp = compare;
    wrap.ctx = context;
    prev = g_bsearch_s_wrap;
    g_bsearch_s_wrap = &wrap;
    r = ucrt_xp_bsearch(key, base, num, width, bsearch_s_bridge);
    g_bsearch_s_wrap = prev;
    return r;
}

/* ------------------------------------------------------------------ */
/* freopen_s                                                           */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_freopen_s(
    UCRT_XP_FILE **pfile, const char *path, const char *mode, UCRT_XP_FILE *stream)
{
    UCRT_XP_FILE *f;
    if (!pfile || !path || !mode || !stream) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    *pfile = NULL;
    f = ucrt_xp_freopen(path, mode, stream);
    if (!f) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    *pfile = f;
    return 0;
}

/* ------------------------------------------------------------------ */
/* itoa_s family                                                       */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_itoa_s(int value, char *buf, size_t size, int radix)
{
    char tmp[72];
    size_t n;
    if (!buf || size == 0 || radix < 2 || radix > 36) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (!ucrt_xp_itoa(value, tmp, radix)) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > size) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buf, tmp, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_ltoa_s(long value, char *buf, size_t size, int radix)
{
    char tmp[72];
    size_t n;
    if (!buf || size == 0 || radix < 2 || radix > 36) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (!ucrt_xp_ltoa(value, tmp, radix)) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > size) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buf, tmp, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_ultoa_s(unsigned long value, char *buf, size_t size, int radix)
{
    char tmp[72];
    size_t n;
    if (!buf || size == 0 || radix < 2 || radix > 36) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (!ucrt_xp_ultoa(value, tmp, radix)) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > size) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buf, tmp, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_i64toa_s(__int64 value, char *buf, size_t size, int radix)
{
    char tmp[80];
    size_t n;
    if (!buf || size == 0 || radix < 2 || radix > 36) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (!ucrt_xp_i64toa(value, tmp, radix)) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > size) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buf, tmp, n);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_ui64toa_s(unsigned __int64 value, char *buf, size_t size, int radix)
{
    char tmp[80];
    size_t n;
    if (!buf || size == 0 || radix < 2 || radix > 36) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    if (!ucrt_xp_ui64toa(value, tmp, radix)) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > size) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(buf, tmp, n);
    return 0;
}

/* ------------------------------------------------------------------ */
/* _splitpath_s / _makepath_s                                          */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_splitpath_s(
    const char *path,
    char *drive, size_t driveSize,
    char *dir, size_t dirSize,
    char *fname, size_t fnameSize,
    char *ext, size_t extSize)
{
    char d[4], di[256], f[256], e[256];
    if (!path) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    ucrt_xp_splitpath(path, d, di, f, e);
    if (drive) {
        if (driveSize == 0) { set_errno_s(EINVAL); return EINVAL; }
        if (ucrt_xp_strlen(d) + 1 > driveSize) {
            drive[0] = 0;
            set_errno_s(ERANGE);
            return ERANGE;
        }
        ucrt_xp_strcpy(drive, d);
    }
    if (dir) {
        if (dirSize == 0) { set_errno_s(EINVAL); return EINVAL; }
        if (ucrt_xp_strlen(di) + 1 > dirSize) {
            dir[0] = 0;
            set_errno_s(ERANGE);
            return ERANGE;
        }
        ucrt_xp_strcpy(dir, di);
    }
    if (fname) {
        if (fnameSize == 0) { set_errno_s(EINVAL); return EINVAL; }
        if (ucrt_xp_strlen(f) + 1 > fnameSize) {
            fname[0] = 0;
            set_errno_s(ERANGE);
            return ERANGE;
        }
        ucrt_xp_strcpy(fname, f);
    }
    if (ext) {
        if (extSize == 0) { set_errno_s(EINVAL); return EINVAL; }
        if (ucrt_xp_strlen(e) + 1 > extSize) {
            ext[0] = 0;
            set_errno_s(ERANGE);
            return ERANGE;
        }
        ucrt_xp_strcpy(ext, e);
    }
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_makepath_s(
    char *path, size_t sizeInBytes,
    const char *drive, const char *dir, const char *fname, const char *ext)
{
    char tmp[MAX_PATH * 2];
    size_t n;
    if (!path || sizeInBytes == 0) {
        set_errno_s(EINVAL);
        return EINVAL;
    }
    ucrt_xp_makepath(tmp, drive, dir, fname, ext);
    n = ucrt_xp_strlen(tmp) + 1;
    if (n > sizeInBytes) {
        path[0] = 0;
        set_errno_s(ERANGE);
        return ERANGE;
    }
    ucrt_xp_memcpy(path, tmp, n);
    return 0;
}

/* ------------------------------------------------------------------ */
/* scanf_s family (size args after %c / %s / %[])                      */
/* ------------------------------------------------------------------ */

/*
 * Strategy: walk fmt; for conversions that need a size in the secure
 * API, pull (unsigned) size from va_list after the pointer, then invoke
 * a constrained read into a temp or directly with width limit.
 * For other conversions, pass through to the existing vfscanf/vsscanf.
 *
 * Full re-implementation of scanf is avoided: we rewrite the format to
 * inject max field widths for %s/%[/%c and call the existing engine.
 */

static int scan_s_rewrite_and_run(
    int (*engine)(void *ctx, const char *fmt, va_list ap),
    void *ctx,
    const char *fmt,
    va_list ap)
{
    /* Simpler reliable path: use existing vsscanf/vfscanf but require
     * callers to pass sizes; we copy args into a new va-style buffer by
     * parsing fmt ourselves for the secure conversions only.
     *
     * Implementation: build a modified format with explicit widths and
     * an argv array of pointers only (sizes consumed from ap).
     */
    char newfmt[1024];
    void *args[64];
    int nargs = 0;
    const char *p = fmt;
    char *o = newfmt;
    char *oend = newfmt + sizeof(newfmt) - 1;
    va_list ap2;
    int i;

    if (!fmt) {
        set_errno_s(EINVAL);
        return EOF;
    }

    while (*p && o < oend) {
        if (*p != '%') {
            *o++ = *p++;
            continue;
        }
        *o++ = *p++; /* '%' */
        if (*p == '%') {
            *o++ = *p++;
            continue;
        }
        /* flags / width / suppress */
        while (*p == '*' || *p == ' ' || (*p >= '0' && *p <= '9')) {
            /* keep suppression; numeric width kept if present */
            if (*p == '*') {
                *o++ = *p++;
            } else if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9' && o < oend)
                    *o++ = *p++;
            } else {
                p++; /* skip odd space */
            }
        }
        /* length modifiers */
        if (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z' || *p == 't' || *p == 'j') {
            char len = *p++;
            *o++ = len;
            if ((len == 'h' || len == 'l') && *p == len)
                *o++ = *p++;
        }
        if (!*p) break;

        if (*p == 's' || *p == 'c' || *p == '[') {
            void *ptr;
            unsigned sz;
            char conv = *p;
            int suppress = 0;
            /* detect suppression already written - check last chars */
            /* pull pointer + size from ap */
            ptr = va_arg(ap, void *);
            sz = va_arg(ap, unsigned);
            if (!ptr || sz == 0) {
                set_errno_s(EINVAL);
                return EOF;
            }
            if (nargs >= 64) {
                set_errno_s(EINVAL);
                return EOF;
            }
            args[nargs++] = ptr;
            /* inject width = sz-1 for %s/%[  (room for NUL), sz for %c */
            if (conv == 'c') {
                /* width sz */
                char wbuf[16];
                int wi = 0;
                unsigned v = sz;
                if (v == 0) v = 1;
                /* itoa width */
                {
                    char tb[16];
                    int ti = 0;
                    if (v == 0) tb[ti++] = '0';
                    while (v) {
                        tb[ti++] = (char)('0' + (v % 10));
                        v /= 10;
                    }
                    while (ti > 0) wbuf[wi++] = tb[--ti];
                    wbuf[wi] = 0;
                }
                for (i = 0; wbuf[i] && o < oend; i++)
                    *o++ = wbuf[i];
            } else {
                unsigned v = (sz > 0) ? sz - 1 : 0;
                char tb[16];
                int ti = 0, wi;
                if (v == 0) {
                    /* size 1: only NUL — invalid for %s */
                    set_errno_s(EINVAL);
                    return EOF;
                }
                while (v) {
                    tb[ti++] = (char)('0' + (v % 10));
                    v /= 10;
                }
                for (wi = ti - 1; wi >= 0 && o < oend; wi--)
                    *o++ = tb[wi];
            }
            if (conv == '[') {
                *o++ = '[';
                p++; /* skip [ */
                if (*p == '^') {
                    *o++ = *p++;
                }
                while (*p && *p != ']' && o < oend)
                    *o++ = *p++;
                if (*p == ']' && o < oend)
                    *o++ = *p++;
            } else {
                *o++ = conv;
                p++;
            }
            (void)suppress;
        } else {
            /* ordinary conversion: one pointer arg */
            if (*p != 'n') {
                void *ptr = va_arg(ap, void *);
                if (nargs >= 64) {
                    set_errno_s(EINVAL);
                    return EOF;
                }
                args[nargs++] = ptr;
            } else {
                void *ptr = va_arg(ap, void *);
                if (nargs >= 64) {
                    set_errno_s(EINVAL);
                    return EOF;
                }
                args[nargs++] = ptr;
            }
            *o++ = *p++;
        }
    }
    *o = 0;

    /*
     * Call existing engine with rewritten format. We cannot reconstruct
     * a va_list portably from args[], so fall back to a constrained
     * approach: for string-only secure scans use direct width injection
     * via snprintf into a format that matches single-buffer common cases,
     * and for general cases invoke non-_s with the original ap (best effort).
     *
     * Practical compromise used here: run original fmt through the
     * non-secure engine when no %s/%c/%[ present; otherwise use a
     * dedicated narrow path with the rewritten format and sequential
     * assignment via sscanf on a copy — not applicable for fscanf.
     *
     * Final approach: call the non-_s engine with original ap for full
     * conversion support, after validating that every %s/%c/%[ had a
     * non-null pointer and positive size (already pulled above). The
     * width injection in newfmt is applied by re-scanning with
     * ucrt_xp_vsscanf on string sources only when we can rebuild args.
     */
    (void)newfmt;
    (void)args;
    (void)engine;
    (void)ctx;
    (void)ap2;

    /* Best-effort: validate sizes already; delegate to non-_s. */
    return -2; /* signal caller to use fallback */
}

/* Dedicated secure string scan from memory */
static int vsscanf_s_impl(const char *str, const char *fmt, va_list ap)
{
    /* Rewrite format with widths and collect pointer-only va into
     * temporary buffer using platform-specific approach.
     * Use recursive field parsing with existing character sources. */
    const char *p = fmt;
    const char *sp = str;
    int assigned = 0;

    if (!str || !fmt) {
        set_errno_s(EINVAL);
        return EOF;
    }

    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n') {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
            while (*sp == ' ' || *sp == '\t' || *sp == '\n' || *sp == '\r')
                sp++;
            continue;
        }
        if (*p != '%') {
            if (*sp != *p) return assigned;
            sp++;
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            if (*sp != '%') return assigned;
            sp++;
            p++;
            continue;
        }
        {
            int suppress = 0;
            int width = -1;
            char len = 0;
            if (*p == '*') {
                suppress = 1;
                p++;
            }
            if (*p >= '0' && *p <= '9') {
                width = 0;
                while (*p >= '0' && *p <= '9')
                    width = width * 10 + (*p++ - '0');
            }
            if (*p == 'h' || *p == 'l' || *p == 'L') {
                len = *p++;
                if ((*p == 'h' || *p == 'l') && *p == len)
                    p++;
            }
            if (*p == 's' || *p == '[') {
                char *out = NULL;
                unsigned sz = 0;
                int i = 0;
                int invert = 0;
                char set[256];
                if (!suppress) {
                    out = va_arg(ap, char *);
                    sz = va_arg(ap, unsigned);
                    if (!out || sz == 0) {
                        set_errno_s(EINVAL);
                        return EOF;
                    }
                }
                if (*p == '[') {
                    p++;
                    if (*p == '^') {
                        invert = 1;
                        p++;
                    }
                    ucrt_xp_memset(set, 0, sizeof(set));
                    if (*p == ']') {
                        set[(unsigned char)']'] = 1;
                        p++;
                    }
                    while (*p && *p != ']') {
                        if (p[1] == '-' && p[2] && p[2] != ']') {
                            unsigned char a = (unsigned char)p[0];
                            unsigned char b = (unsigned char)p[2];
                            unsigned char c;
                            if (a > b) {
                                unsigned char t = a;
                                a = b;
                                b = t;
                            }
                            for (c = a; c <= b; c++)
                                set[c] = 1;
                            p += 3;
                        } else {
                            set[(unsigned char)*p++] = 1;
                        }
                    }
                    if (*p == ']') p++;
                    while (*sp) {
                        unsigned char ch = (unsigned char)*sp;
                        int in = set[ch] ? 1 : 0;
                        if (invert) in = !in;
                        if (!in) break;
                        if (width >= 0 && i >= width) break;
                        if (!suppress && (unsigned)i + 1 >= sz) break;
                        if (!suppress) out[i] = (char)ch;
                        i++;
                        sp++;
                    }
                } else {
                    /* %s */
                    p++;
                    while (*sp == ' ' || *sp == '\t' || *sp == '\n' || *sp == '\r')
                        sp++;
                    while (*sp && *sp != ' ' && *sp != '\t' && *sp != '\n' && *sp != '\r') {
                        if (width >= 0 && i >= width) break;
                        if (!suppress && (unsigned)i + 1 >= sz) break;
                        if (!suppress) out[i] = *sp;
                        i++;
                        sp++;
                    }
                }
                if (!suppress) {
                    out[i] = 0;
                    if (i == 0) return assigned;
                    assigned++;
                }
            } else if (*p == 'c') {
                char *out = NULL;
                unsigned sz = 1;
                int i = 0;
                p++;
                if (!suppress) {
                    out = va_arg(ap, char *);
                    sz = va_arg(ap, unsigned);
                    if (!out || sz == 0) {
                        set_errno_s(EINVAL);
                        return EOF;
                    }
                }
                if (width < 0) width = (int)sz;
                while (i < width && *sp) {
                    if (!suppress) out[i] = *sp;
                    i++;
                    sp++;
                }
                if (!suppress) {
                    if (i == 0) return assigned;
                    assigned++;
                }
            } else {
                /* Fallback: build a one-conversion format and use vsscanf */
                char one[32];
                char *op = one;
                const char *start = sp;
                int n;
                *op++ = '%';
                if (suppress) *op++ = '*';
                if (width >= 0) {
                    char tb[12];
                    int ti = 0;
                    int w = width;
                    if (w == 0) tb[ti++] = '0';
                    while (w) {
                        tb[ti++] = (char)('0' + (w % 10));
                        w /= 10;
                    }
                    while (ti > 0)
                        *op++ = tb[--ti];
                }
                if (len) *op++ = len;
                *op++ = *p++;
                *op = 0;
                if (suppress) {
                    n = ucrt_xp_sscanf(sp, one);
                } else {
                    void *arg = va_arg(ap, void *);
                    n = ucrt_xp_sscanf(sp, one, arg);
                }
                if (n <= 0 && !suppress) return assigned;
                if (!suppress) assigned += n;
                /* advance sp by matching one conversion length — approximate */
                {
                    /* re-scan: use scanset-less advance via sscanf %n */
                    int consumed = 0;
                    char adv[40];
                    ucrt_xp_strcpy(adv, one);
                    ucrt_xp_strcat(adv, "%n");
                    if (suppress)
                        ucrt_xp_sscanf(start, adv, &consumed);
                    else {
                        /* can't easily re-parse; skip leading ws + token heuristically */
                        const char *q = start;
                        while (*q == ' ' || *q == '\t')
                            q++;
                        while (*q && *q != ' ' && *q != '\t' && *q != '\n')
                            q++;
                        consumed = (int)(q - start);
                    }
                    if (consumed > 0) sp = start + consumed;
                }
                (void)n;
            }
        }
    }
    return assigned;
}

__declspec(dllexport) int __cdecl ucrt_xp_vsscanf_s(const char *str, const char *fmt, va_list args)
{
    return vsscanf_s_impl(str, fmt, args);
}

__declspec(dllexport) int __cdecl ucrt_xp_sscanf_s(const char *str, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vsscanf_s(str, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_vfscanf_s(UCRT_XP_FILE *f, const char *fmt, va_list args)
{
    /* Growable read of the rest of the logical line(s) then vsscanf_s.
     * Stops at EOF. Cap at 1MB to avoid unbounded memory use. */
    size_t cap = 4096, n = 0;
    char *buf;
    int c;
    int r;
    if (!f || !fmt) {
        set_errno_s(EINVAL);
        return EOF;
    }
    buf = (char *)ucrt_xp_malloc(cap);
    if (!buf) {
        set_errno_s(EINVAL);
        return EOF;
    }
    while (n + 1 < 1024 * 1024) {
        c = ucrt_xp_fgetc(f);
        if (c == -1) break;
        if (n + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)ucrt_xp_realloc(buf, ncap);
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
        buf[n++] = (char)c;
    }
    buf[n] = 0;
    r = vsscanf_s_impl(buf, fmt, args);
    ucrt_xp_free(buf);
    return r;
}

__declspec(dllexport) int __cdecl ucrt_xp_fscanf_s(UCRT_XP_FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vfscanf_s(f, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_scanf_s(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vfscanf_s(ucrt_xp_stdin(), fmt, ap);
    va_end(ap);
    return n;
}

/* ------------------------------------------------------------------ */
/* Wide scanf_s family                                                 */
/* ------------------------------------------------------------------ */

static int vswscanf_s_impl(const wchar_t *str, const wchar_t *fmt, va_list ap)
{
    const wchar_t *p = fmt;
    const wchar_t *sp = str;
    int assigned = 0;

    if (!str || !fmt) {
        set_errno_s(EINVAL);
        return EOF;
    }

    while (*p) {
        if (*p == L' ' || *p == L'\t' || *p == L'\n') {
            while (*p == L' ' || *p == L'\t' || *p == L'\n')
                p++;
            while (*sp == L' ' || *sp == L'\t' || *sp == L'\n' || *sp == L'\r')
                sp++;
            continue;
        }
        if (*p != L'%') {
            if (*sp != *p) return assigned;
            sp++;
            p++;
            continue;
        }
        p++;
        if (*p == L'%') {
            if (*sp != L'%') return assigned;
            sp++;
            p++;
            continue;
        }
        {
            int suppress = 0;
            int width = -1;
            wchar_t len = 0;

            if (*p == L'*') {
                suppress = 1;
                p++;
            }
            if (*p >= L'0' && *p <= L'9') {
                width = 0;
                while (*p >= L'0' && *p <= L'9')
                    width = width * 10 + (int)(*p++ - L'0');
            }
            if (*p == L'h' || *p == L'l' || *p == L'L') {
                len = *p++;
                if ((*p == L'h' || *p == L'l') && *p == len)
                    p++;
            }

            if (*p == L's' || *p == L'[') {
                wchar_t *out = NULL;
                unsigned sz = 0;
                int i = 0;
                int invert = 0;
                unsigned char set[256];

                if (!suppress) {
                    out = va_arg(ap, wchar_t *);
                    sz = va_arg(ap, unsigned);
                    if (!out || sz == 0) {
                        set_errno_s(EINVAL);
                        return EOF;
                    }
                }

                if (*p == L'[') {
                    p++;
                    if (*p == L'^') {
                        invert = 1;
                        p++;
                    }
                    ucrt_xp_memset(set, 0, sizeof(set));
                    if (*p == L']') {
                        set[(unsigned char)L']'] = 1;
                        p++;
                    }
                    while (*p && *p != L']') {
                        if (p[1] == L'-' && p[2] && p[2] != L']') {
                            unsigned char a = (unsigned char)p[0];
                            unsigned char b = (unsigned char)p[2];
                            unsigned char c;
                            if (a > b) {
                                unsigned char t = a;
                                a = b;
                                b = t;
                            }
                            for (c = a; c <= b; c++)
                                set[c] = 1;
                            p += 3;
                        } else {
                            if ((unsigned)*p < 256)
                                set[(unsigned char)*p] = 1;
                            p++;
                        }
                    }
                    if (*p == L']') p++;
                    while (*sp) {
                        unsigned char ch = (unsigned char)*sp;
                        int in = (ch < 256 && set[ch]) ? 1 : 0;
                        if (invert) in = !in;
                        if (!in) break;
                        if (width >= 0 && i >= width) break;
                        if (!suppress && (unsigned)i + 1 >= sz) break;
                        if (!suppress) out[i] = *sp;
                        i++;
                        sp++;
                    }
                } else {
                    p++; /* s */
                    while (*sp == L' ' || *sp == L'\t' || *sp == L'\n' || *sp == L'\r')
                        sp++;
                    while (*sp && *sp != L' ' && *sp != L'\t' && *sp != L'\n' && *sp != L'\r') {
                        if (width >= 0 && i >= width) break;
                        if (!suppress && (unsigned)i + 1 >= sz) break;
                        if (!suppress) out[i] = *sp;
                        i++;
                        sp++;
                    }
                }
                if (!suppress) {
                    out[i] = 0;
                    if (i == 0) return assigned;
                    assigned++;
                }
            } else if (*p == L'c') {
                wchar_t *out = NULL;
                unsigned sz = 1;
                int i = 0;
                p++;
                if (!suppress) {
                    out = va_arg(ap, wchar_t *);
                    sz = va_arg(ap, unsigned);
                    if (!out || sz == 0) {
                        set_errno_s(EINVAL);
                        return EOF;
                    }
                }
                if (width < 0) width = (int)sz;
                while (i < width && *sp) {
                    if (!suppress) out[i] = *sp;
                    i++;
                    sp++;
                }
                if (!suppress) {
                    if (i == 0) return assigned;
                    assigned++;
                }
            } else {
                /* Numeric / other: one conversion via swscanf */
                wchar_t one[32];
                wchar_t *op = one;
                const wchar_t *start = sp;
                int n;
                *op++ = L'%';
                if (suppress) *op++ = L'*';
                if (width >= 0) {
                    wchar_t tb[12];
                    int ti = 0;
                    int w = width;
                    if (w == 0) tb[ti++] = L'0';
                    while (w) {
                        tb[ti++] = (wchar_t)(L'0' + (w % 10));
                        w /= 10;
                    }
                    while (ti > 0)
                        *op++ = tb[--ti];
                }
                if (len) *op++ = len;
                *op++ = *p++;
                *op = 0;
                if (suppress) {
                    n = ucrt_xp_swscanf(sp, one);
                } else {
                    void *arg = va_arg(ap, void *);
                    n = ucrt_xp_swscanf(sp, one, arg);
                }
                if (n <= 0 && !suppress) return assigned;
                if (!suppress) assigned += n;
                {
                    const wchar_t *q = start;
                    while (*q == L' ' || *q == L'\t')
                        q++;
                    while (*q && *q != L' ' && *q != L'\t' && *q != L'\n')
                        q++;
                    sp = q;
                }
                (void)n;
            }
        }
    }
    return assigned;
}

__declspec(dllexport) int __cdecl ucrt_xp_vswscanf_s(const wchar_t *str, const wchar_t *fmt, va_list args)
{
    return vswscanf_s_impl(str, fmt, args);
}

__declspec(dllexport) int __cdecl ucrt_xp_swscanf_s(const wchar_t *str, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vswscanf_s(str, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_vfwscanf_s(UCRT_XP_FILE *f, const wchar_t *fmt, va_list args)
{
    size_t cap = 4096, n = 0;
    wchar_t *buf;
    int c;
    int r;
    if (!f || !fmt) {
        set_errno_s(EINVAL);
        return EOF;
    }
    buf = (wchar_t *)ucrt_xp_malloc(cap * sizeof(wchar_t));
    if (!buf) {
        set_errno_s(EINVAL);
        return EOF;
    }
    /* Read narrow bytes as wchar codes (same model as non-_s wide file scan). */
    while (n + 1 < 1024 * 1024) {
        c = ucrt_xp_fgetc(f);
        if (c == -1) break;
        if (n + 1 >= cap) {
            size_t ncap = cap * 2;
            wchar_t *nb = (wchar_t *)ucrt_xp_realloc(buf, ncap * sizeof(wchar_t));
            if (!nb) break;
            buf = nb;
            cap = ncap;
        }
        buf[n++] = (wchar_t)(unsigned char)c;
    }
    buf[n] = 0;
    r = vswscanf_s_impl(buf, fmt, args);
    ucrt_xp_free(buf);
    return r;
}

__declspec(dllexport) int __cdecl ucrt_xp_fwscanf_s(UCRT_XP_FILE *f, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vfwscanf_s(f, fmt, ap);
    va_end(ap);
    return n;
}

__declspec(dllexport) int __cdecl ucrt_xp_wscanf_s(const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = ucrt_xp_vfwscanf_s(ucrt_xp_stdin(), fmt, ap);
    va_end(ap);
    return n;
}

/* Wide printf_s already has swprintf_s/vswprintf_s; add snwprintf_s alias style */
__declspec(dllexport) int __cdecl ucrt_xp_snwprintf_s(wchar_t *buf, size_t bufsz, size_t count, const wchar_t *fmt, ...)
{
    va_list ap;
    int n;
    size_t lim;
    if (!fmt) {
        set_errno_s(EINVAL);
        return -1;
    }
    va_start(ap, fmt);
    if (count == _TRUNCATE) {
        if (!buf || bufsz == 0) {
            va_end(ap);
            return -1;
        }
        n = ucrt_xp_vswprintf(buf, bufsz, fmt, ap);
        va_end(ap);
        if (n < 0) return -1;
        if ((size_t)n >= bufsz) return -1;
        return n;
    }
    lim = count < bufsz ? count + 1 : bufsz;
    if (!buf || lim == 0) {
        set_errno_s(EINVAL);
        va_end(ap);
        return -1;
    }
    n = ucrt_xp_vswprintf(buf, lim, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= lim) {
        buf[0] = 0;
        set_errno_s(ERANGE);
        return -1;
    }
    return n;
}
