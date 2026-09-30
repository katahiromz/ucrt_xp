/*
 * string.c - the mem-family and str-family functions from <string.h>,
 * implemented directly (byte-loop or Win32 RtlMoveMemory-backed) rather
 * than calling into whichever msvcrtXX.dll a module happens to be linked against. These
 * are individually simple, but centralizing them here means every
 * module sharing ucrt_xp.dll gets identical semantics - e.g. one
 * well-tested strtok that's actually thread-safe (context-based, not
 * static-buffer-based like the classic single-argument strtok()).
 */
#include "internal.h"
#include <string.h> /* for size_t only in some toolchains' <windows.h> setups */

UCRT_XP_API void* __cdecl ucrt_xp_memcpy(void *dst, const void *src, size_t n)
{
    /* CopyMemory/RtlCopyMemory assumes non-overlapping regions, exactly
     * like the standard's memcpy() contract - callers with overlap must
     * use ucrt_xp_memmove instead, same as always. */
    CopyMemory(dst, src, n);
    return dst;
}

UCRT_XP_API void* __cdecl ucrt_xp_memmove(void *dst, const void *src, size_t n)
{
    MoveMemory(dst, src, n); /* MoveMemory is overlap-safe on Win32 */
    return dst;
}

UCRT_XP_API void* __cdecl ucrt_xp_memset(void *dst, int c, size_t n)
{
    FillMemory(dst, n, (BYTE)c);
    return dst;
}

UCRT_XP_API int __cdecl ucrt_xp_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

UCRT_XP_API void* __cdecl ucrt_xp_memchr(const void *buf, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return NULL;
}

UCRT_XP_API size_t __cdecl ucrt_xp_strlen(const char *s)
{
    const char *p = s;
    if (!s) return 0;
    while (*p) p++;
    return (size_t)(p - s);
}

UCRT_XP_API char* __cdecl ucrt_xp_strcpy(char *dst, const char *src)
{
    char *d = dst;
    if (!dst || !src) return dst;
    while ((*d++ = *src++) != 0) { /* copy incl. NUL */ }
    return dst;
}

UCRT_XP_API char* __cdecl ucrt_xp_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (!dst) return dst;
    if (src) {
        for (; i < n && src[i]; i++) dst[i] = src[i];
    }
    for (; i < n; i++) dst[i] = 0; /* strncpy's classic zero-pad behavior */
    return dst;
}

UCRT_XP_API char* __cdecl ucrt_xp_strcat(char *dst, const char *src)
{
    char *d = dst;
    if (!dst || !src) return dst;
    while (*d) d++;
    while ((*d++ = *src++) != 0) { /* append incl. NUL */ }
    return dst;
}

UCRT_XP_API char* __cdecl ucrt_xp_strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    size_t i = 0;
    if (!dst) return dst;
    while (*d) d++;
    if (src) {
        for (; i < n && src[i]; i++) d[i] = src[i];
    }
    d[i] = 0;
    return dst;
}

UCRT_XP_API int __cdecl ucrt_xp_strcmp(const char *a, const char *b)
{
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

UCRT_XP_API int __cdecl ucrt_xp_strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}

UCRT_XP_API char* __cdecl ucrt_xp_strchr(const char *s, int c)
{
    if (!s) return NULL;
    for (; *s; s++) {
        if (*s == (char)c) return (char *)s;
    }
    return (c == 0) ? (char *)s : NULL; /* strchr(s, '\0') finds the NUL */
}

UCRT_XP_API char* __cdecl ucrt_xp_strrchr(const char *s, int c)
{
    const char *found = NULL;
    if (!s) return NULL;
    for (; *s; s++) {
        if (*s == (char)c) found = s;
    }
    if (c == 0) return (char *)s;
    return (char *)found;
}

UCRT_XP_API char* __cdecl ucrt_xp_strstr(const char *haystack, const char *needle)
{
    size_t nlen;
    if (!haystack || !needle) return NULL;
    nlen = ucrt_xp_strlen(needle);
    if (nlen == 0) return (char *)haystack;

    for (; *haystack; haystack++) {
        if (ucrt_xp_strncmp(haystack, needle, nlen) == 0) return (char *)haystack;
    }
    return NULL;
}

UCRT_XP_API char* __cdecl ucrt_xp_strdup(const char *s)
{
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = ucrt_xp_strlen(s) + 1;
    copy = (char *)ucrt_xp_malloc(len);
    if (copy) CopyMemory(copy, s, len);
    return copy;
}

/*
 * ucrt_xp_strtok_r - a thread-safe/reentrant strtok, taking an explicit
 * saveptr the way POSIX's strtok_r (and C11's strtok_s) do, instead of
 * the classic CRT strtok()'s hidden static/TLS buffer. Callers wanting
 * the old single-argument ergonomics can keep their own char* saveptr
 * local variable across calls - that's the whole difference.
 */
UCRT_XP_API char* __cdecl ucrt_xp_strtok_r(char *str, const char *delim, char **saveptr)
{
    char *start;
    char *p;

    if (!str) {
        str = *saveptr;
        if (!str) return NULL;
    }

    /* skip leading delimiters */
    for (; *str; str++) {
        if (!ucrt_xp_strchr(delim, (unsigned char)*str)) break;
    }
    if (*str == 0) { *saveptr = str; return NULL; }

    start = str;
    for (p = str; *p; p++) {
        if (ucrt_xp_strchr(delim, (unsigned char)*p)) {
            *p = 0;
            *saveptr = p + 1;
            return start;
        }
    }
    *saveptr = p; /* points at the terminating NUL - next call returns NULL */
    return start;
}

/* Case-insensitive compare of at most n characters (MSVC _strnicmp).
 * Stops at the first NUL in either string, same as the real CRT. */
UCRT_XP_API int __cdecl ucrt_xp_strnicmp(
    const char *a, const char *b, size_t n)
{
    size_t la, lb;
    int r;

    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);

    la = 0;
    while (la < n && a[la]) la++;
    lb = 0;
    while (lb < n && b[lb]) lb++;

    r = CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                       a, (int)la, b, (int)lb);
    return r - CSTR_EQUAL;
}

/* ------------------------------------------------------------------ */
/* Span / search helpers: strspn, strcspn, strpbrk, strnlen            */
/* ------------------------------------------------------------------ */

/* Length of the initial segment of s consisting only of bytes in accept. */
UCRT_XP_API size_t __cdecl ucrt_xp_strspn(const char *s, const char *accept)
{
    const char *p = s;
    if (!s || !accept) return 0;
    for (; *p; p++) {
        if (!ucrt_xp_strchr(accept, (unsigned char)*p)) break;
    }
    return (size_t)(p - s);
}

/* Length of the initial segment of s containing no bytes from reject. */
UCRT_XP_API size_t __cdecl ucrt_xp_strcspn(const char *s, const char *reject)
{
    const char *p = s;
    if (!s) return 0;
    if (!reject) return ucrt_xp_strlen(s);
    for (; *p; p++) {
        if (ucrt_xp_strchr(reject, (unsigned char)*p)) break;
    }
    return (size_t)(p - s);
}

/* First byte in s that matches any byte in accept, or NULL. */
UCRT_XP_API char* __cdecl ucrt_xp_strpbrk(const char *s, const char *accept)
{
    if (!s || !accept) return NULL;
    for (; *s; s++) {
        if (ucrt_xp_strchr(accept, (unsigned char)*s)) return (char *)s;
    }
    return NULL;
}

UCRT_XP_API size_t __cdecl ucrt_xp_strnlen(const char *s, size_t maxlen)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < maxlen; i++) {
        if (s[i] == 0) return i;
    }
    return maxlen;
}

/* ------------------------------------------------------------------ */
/* memccpy, strlwr, strupr                                              */
/* ------------------------------------------------------------------ */

/* Copy bytes from src to dst, stopping after the first occurrence of c
 * (included) or after n bytes, whichever comes first. Returns a pointer
 * to the byte after the copy of c, or NULL if c was not found within n.
 * Classic MSVC/Unix memccpy semantics. */
UCRT_XP_API void* __cdecl ucrt_xp_memccpy(void *dst, const void *src, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char uc = (unsigned char)c;
    size_t i;

    if (!dst || !src || n == 0) return NULL;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
        if (s[i] == uc) return (void *)(d + i + 1);
    }
    return NULL;
}

/* In-place lowercase conversion (MSVC _strlwr). Uses CharLowerA which
 * respects the system ANSI code page; for full locale control prefer
 * the _l variant or LCMapString. Returns the same pointer. */
UCRT_XP_API char* __cdecl ucrt_xp_strlwr(char *s)
{
    if (!s) return s;
    CharLowerA(s);
    return s;
}

/* In-place uppercase conversion (MSVC _strupr). */
UCRT_XP_API char* __cdecl ucrt_xp_strupr(char *s)
{
    if (!s) return s;
    CharUpperA(s);
    return s;
}

/* ------------------------------------------------------------------ */
/* _memicmp, _strrev, strcasecmp / strncasecmp                          */
/* ------------------------------------------------------------------ */

/* Case-insensitive memcmp of at most n bytes (MSVC _memicmp).
 * Uses locale-independent ASCII folding for speed and predictability;
 * for full locale folding use CompareString on the relevant spans. */
UCRT_XP_API int __cdecl ucrt_xp_memicmp(const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    for (i = 0; i < n; i++) {
        unsigned char ca = pa[i];
        unsigned char cb = pb[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

/* Reverse a string in place (MSVC _strrev). Returns the same pointer. */
UCRT_XP_API char* __cdecl ucrt_xp_strrev(char *s)
{
    char *lo, *hi;
    if (!s || !*s) return s;
    lo = s;
    hi = s + ucrt_xp_strlen(s) - 1;
    while (lo < hi) {
        char t = *lo;
        *lo++ = *hi;
        *hi-- = t;
    }
    return s;
}

/* POSIX strcasecmp / strncasecmp - aliases to the MSVC-style implementations. */
UCRT_XP_API int __cdecl ucrt_xp_strcasecmp(const char *a, const char *b)
{
    return ucrt_xp_stricmp(a, b);
}

UCRT_XP_API int __cdecl ucrt_xp_strncasecmp(const char *a, const char *b, size_t n)
{
    return ucrt_xp_strnicmp(a, b, n);
}
