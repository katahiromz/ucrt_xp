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
