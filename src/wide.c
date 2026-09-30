/*
 * wide.c - the minimal wide-character (wchar_t) layer: wide-path variants
 * of open()/fopen() (MSVC names: _wopen / _wfopen; exported here as
 * ucrt_xp_wopen / ucrt_xp_wfopen) so filenames outside the process's ANSI
 * code page are reachable, plus the handful of wcs* primitives needed to
 * build on top of that (including _wcsicmp / wcsicmp_l).
 *
 * Explicitly NOT here (see README): a parallel wide printf/scanf stack,
 * and general Unicode text-processing (normalization, case folding
 * beyond the OS's CharUpperW/CharLowerW, etc). XP's own wide-char
 * support is itself UTF-16 but code-page-centric in a lot of its APIs;
 * this layer stays consistent with that rather than pretending to be a
 * modern ICU-grade Unicode stack.
 */
#include "internal.h"
#include <string.h>
#include <stdarg.h>

UCRT_XP_API int __cdecl ucrt_xp_wopen(const wchar_t *path, int oflag, int pmode)
{
    DWORD access = 0, creation = OPEN_EXISTING;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    HANDLE h;
    int fd;

    (void)pmode;

    if ((oflag & UCRT_XP_O_RDWR) == UCRT_XP_O_RDWR) {
        access = GENERIC_READ | GENERIC_WRITE;
    } else if (oflag & UCRT_XP_O_WRONLY) {
        access = GENERIC_WRITE;
    } else {
        access = GENERIC_READ;
    }

    if (oflag & UCRT_XP_O_CREAT) {
        if (oflag & UCRT_XP_O_EXCL) creation = CREATE_NEW;
        else if (oflag & UCRT_XP_O_TRUNC) creation = CREATE_ALWAYS;
        else creation = OPEN_ALWAYS;
    } else if (oflag & UCRT_XP_O_TRUNC) {
        creation = TRUNCATE_EXISTING;
    }

    h = CreateFileW(path, access, share, NULL, creation,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;

    if (oflag & UCRT_XP_O_APPEND) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(h, zero, NULL, FILE_END);
    }

    fd = ucrt_xp__register_fd(h, (oflag & UCRT_XP_O_TEXT) ? 1 : 0,
                               (oflag & UCRT_XP_O_APPEND) ? 1 : 0);
    if (fd < 0) { CloseHandle(h); return -1; }
    return fd;
}

static BOOL parse_wide_mode(const wchar_t *mode, int *oflag)
{
    int has_plus, f = 0;
    const wchar_t *p;
    if (!mode || !*mode) return FALSE;

    has_plus = FALSE;
    for (p = mode; *p; p++) if (*p == L'+') has_plus = TRUE;

    switch (mode[0]) {
    case L'r': f = has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_RDONLY; break;
    case L'w': f = (has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_WRONLY)
                   | UCRT_XP_O_CREAT | UCRT_XP_O_TRUNC; break;
    case L'a': f = (has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_WRONLY)
                   | UCRT_XP_O_CREAT | UCRT_XP_O_APPEND; break;
    default: return FALSE;
    }

    {
        int has_b = FALSE;
        for (p = mode; *p; p++) if (*p == L'b') has_b = TRUE;
        f |= has_b ? UCRT_XP_O_BINARY : UCRT_XP_O_TEXT;
    }

    *oflag = f;
    return TRUE;
}

UCRT_XP_API UCRT_XP_FILE* __cdecl ucrt_xp_wfopen(const wchar_t *path, const wchar_t *mode)
{
    int oflag, fd;

    if (!parse_wide_mode(mode, &oflag)) return NULL;

    fd = ucrt_xp_wopen(path, oflag, 0);
    if (fd < 0) return NULL;

    return ucrt_xp__file_from_fd(fd);
}

UCRT_XP_API size_t __cdecl ucrt_xp_wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    if (!s) return 0;
    while (*p) p++;
    return (size_t)(p - s);
}

UCRT_XP_API int __cdecl ucrt_xp_wcscmp(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && (*a == *b)) { a++; b++; }
    return (int)*a - (int)*b;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    if (!dst || !src) return dst;
    while ((*d++ = *src++) != 0) { /* copy including terminating NUL */ }
    return dst;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsncpy(
    wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i;
    if (!dst || !src) return dst;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0; /* pad with NULs like strncpy */
    return dst;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcscat(
    wchar_t *dst, const wchar_t *src)
{
    wchar_t *d;
    if (!dst || !src) return dst;
    d = dst + ucrt_xp_wcslen(dst);
    while ((*d++ = *src++) != 0) { /* copy including terminating NUL */ }
    return dst;
}

UCRT_XP_API int __cdecl ucrt_xp_wcsncmp(
    const wchar_t *a, const wchar_t *b, size_t n)
{
    size_t i;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

UCRT_XP_API size_t __cdecl ucrt_xp_wcsnlen(const wchar_t *s, size_t maxlen)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < maxlen; i++) {
        if (s[i] == 0) return i;
    }
    return maxlen;
}

UCRT_XP_API int __cdecl ucrt_xp_wcsicmp_l(
    const wchar_t *a, const wchar_t *b, ucrt_xp_locale_t loc)
{
    /* CompareStringW is the correct primitive here (locale-aware,
     * Unicode-aware case folding) rather than hand-rolled towupper() on
     * each character, which breaks for anything outside ASCII. */
    LCID lcid = ucrt_xp__locale_lcid(loc);
    int r = CompareStringW(lcid, NORM_IGNORECASE, a, -1, b, -1);
    return r - CSTR_EQUAL;
}

/* Non-locale variant: uses the calling thread's current locale
 * (same semantics as MSVC's _wcsicmp). */
UCRT_XP_API int __cdecl ucrt_xp_wcsicmp(
    const wchar_t *a, const wchar_t *b)
{
    return ucrt_xp_wcsicmp_l(a, b, NULL);
}

UCRT_XP_API int __cdecl ucrt_xp_ansi_to_wide(
    const char *in, wchar_t *out, int outcap)
{
    int needed;
    if (!in) return -1;

    needed = MultiByteToWideChar(CP_ACP, 0, in, -1, NULL, 0);
    if (needed <= 0) return -1;
    if (!out || outcap == 0) return needed - 1; /* exclude NUL, matches wcslen() convention */

    if (MultiByteToWideChar(CP_ACP, 0, in, -1, out, outcap) == 0) return -1;
    return needed - 1;
}

UCRT_XP_API int __cdecl ucrt_xp_wide_to_ansi(
    const wchar_t *in, char *out, int outcap)
{
    int needed;
    if (!in) return -1;

    needed = WideCharToMultiByte(CP_ACP, 0, in, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return -1;
    if (!out || outcap == 0) return needed - 1;

    if (WideCharToMultiByte(CP_ACP, 0, in, -1, out, outcap, NULL, NULL) == 0) return -1;
    return needed - 1;
}

/* ------------------------------------------------------------------ */
/* Wide printf family                                                  */
/* ------------------------------------------------------------------ */

typedef struct WSink {
    wchar_t *buf;    /* NULL means "counting only" */
    size_t   cap;     /* capacity in wchar_t units, including the NUL */
    size_t   written;
} WSink;

static void wsink_putc(WSink *s, wchar_t c)
{
    if (s->buf && s->written + 1 < s->cap) s->buf[s->written] = c;
    s->written++;
}

static void wsink_puts_ascii(WSink *s, const char *ascii, size_t len)
{
    /* ASCII (0..127) widens 1:1 to UTF-16 code units - always safe for
     * the digit/sign/exponent/"nan"/"inf" text this is used for. */
    size_t i;
    for (i = 0; i < len; i++) wsink_putc(s, (wchar_t)(unsigned char)ascii[i]);
}

static void wsink_puts_wide(WSink *s, const wchar_t *str, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) wsink_putc(s, str[i]);
}

static void wsink_pad(WSink *s, wchar_t padchar, int count)
{
    while (count-- > 0) wsink_putc(s, padchar);
}

UCRT_XP_API int __cdecl ucrt_xp_vswprintf(
    wchar_t *buf, size_t bufsize_chars, const wchar_t *fmt, va_list args)
{
    WSink sink;
    const wchar_t *p = fmt;

    sink.buf = buf;
    sink.cap = bufsize_chars;
    sink.written = 0;

    while (*p) {
        if (*p != L'%') { wsink_putc(&sink, *p++); continue; }
        p++;

        {
            int left_align = 0, force_sign = 0, space_sign = 0, zero_pad = 0;
            int width = 0, precision = -1;
            int is_long = 0, is_longlong = 0;
            wchar_t conv;

            for (;;) {
                if (*p == L'-') { left_align = 1; p++; }
                else if (*p == L'+') { force_sign = 1; p++; }
                else if (*p == L' ') { space_sign = 1; p++; }
                else if (*p == L'0') { zero_pad = 1; p++; }
                else if (*p == L'#') { p++; }
                else break;
            }

            if (*p == L'*') { width = va_arg(args, int); p++; }
            else { while (*p >= L'0' && *p <= L'9') { width = width * 10 + (int)(*p - L'0'); p++; } }

            if (*p == L'.') {
                p++;
                if (*p == L'*') { precision = va_arg(args, int); p++; }
                else { precision = 0; while (*p >= L'0' && *p <= L'9') { precision = precision * 10 + (int)(*p - L'0'); p++; } }
            }

            if (*p == L'h') { p++; if (*p == L'h') p++; }
            else if (*p == L'l') { is_long = 1; p++; if (*p == L'l') { is_longlong = 1; p++; } }
            else if (*p == L'I' && p[1] == L'6' && p[2] == L'4') { is_longlong = 1; p += 3; }
            else if (*p == L'z') { is_long = 1; p++; }

            conv = *p ? *p++ : 0;

            switch (conv) {
            case L'd': case L'i': {
                __int64 v = is_longlong ? va_arg(args, __int64)
                            : is_long ? (__int64)va_arg(args, long)
                            : (__int64)va_arg(args, int);
                int neg = v < 0;
                unsigned __int64 uv = neg ? (unsigned __int64)(-v) : (unsigned __int64)v;
                char tmp[32];
                char *digits = ucrt_xp__utoa_generic(uv, 10, 0, tmp + sizeof(tmp));
                int dlen = (int)strlen(digits);
                int signlen = neg || force_sign || space_sign;
                int pad = width - dlen - signlen;
                if (!left_align && zero_pad && precision < 0) {
                    if (signlen) wsink_putc(&sink, neg ? L'-' : (force_sign ? L'+' : L' '));
                    wsink_pad(&sink, L'0', pad);
                    wsink_puts_ascii(&sink, digits, (size_t)dlen);
                } else {
                    if (!left_align) wsink_pad(&sink, L' ', pad);
                    if (signlen) wsink_putc(&sink, neg ? L'-' : (force_sign ? L'+' : L' '));
                    wsink_puts_ascii(&sink, digits, (size_t)dlen);
                    if (left_align) wsink_pad(&sink, L' ', pad);
                }
                break;
            }
            case L'u': case L'o': case L'x': case L'X': {
                unsigned __int64 v = is_longlong ? va_arg(args, unsigned __int64)
                                    : is_long ? (unsigned __int64)va_arg(args, unsigned long)
                                    : (unsigned __int64)va_arg(args, unsigned int);
                int base = (conv == L'o') ? 8 : (conv == L'u') ? 10 : 16;
                char tmp[32];
                char *digits = ucrt_xp__utoa_generic(v, base, conv == L'X', tmp + sizeof(tmp));
                int dlen = (int)strlen(digits);
                int pad = width - dlen;
                if (!left_align && zero_pad) {
                    wsink_pad(&sink, L'0', pad);
                    wsink_puts_ascii(&sink, digits, (size_t)dlen);
                } else {
                    if (!left_align) wsink_pad(&sink, L' ', pad);
                    wsink_puts_ascii(&sink, digits, (size_t)dlen);
                    if (left_align) wsink_pad(&sink, L' ', pad);
                }
                break;
            }
            case L'c': {
                wchar_t c = (wchar_t)va_arg(args, int);
                int pad = width - 1;
                if (!left_align) wsink_pad(&sink, L' ', pad);
                wsink_putc(&sink, c);
                if (left_align) wsink_pad(&sink, L' ', pad);
                break;
            }
            case L's': {
                const wchar_t *str = va_arg(args, const wchar_t *);
                int len;
                if (!str) str = L"(null)";
                len = (int)ucrt_xp_wcslen(str);
                if (precision >= 0 && precision < len) len = precision;
                {
                    int pad = width - len;
                    if (!left_align) wsink_pad(&sink, L' ', pad);
                    wsink_puts_wide(&sink, str, (size_t)len);
                    if (left_align) wsink_pad(&sink, L' ', pad);
                }
                break;
            }
            case L'p': {
                void *ptr = va_arg(args, void *);
                char tmp[32];
                char *digits = ucrt_xp__utoa_generic((unsigned __int64)(ULONG_PTR)ptr, 16, 0, tmp + sizeof(tmp));
                wsink_putc(&sink, L'0'); wsink_putc(&sink, L'x');
                wsink_puts_ascii(&sink, digits, strlen(digits));
                break;
            }
            case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': {
                double v = va_arg(args, double);
                char tmp[64];
                int len = ucrt_xp__format_double_ascii(
                    tmp, sizeof(tmp), v, precision, (char)conv, force_sign, space_sign);
                if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
                wsink_puts_ascii(&sink, tmp, (size_t)len);
                break;
            }
            case L'%':
                wsink_putc(&sink, L'%');
                break;
            case L'n':
                break; /* disabled, same rationale as format.c */
            default:
                wsink_putc(&sink, L'%');
                if (conv) wsink_putc(&sink, conv);
                break;
            }
        }
    }

    if (sink.buf && sink.cap > 0) {
        size_t term = (sink.written < sink.cap) ? sink.written : sink.cap - 1;
        sink.buf[term] = 0;
    }
    return (int)sink.written;
}

UCRT_XP_API int __cdecl ucrt_xp_swprintf(
    wchar_t *buf, size_t bufsize_chars, const wchar_t *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vswprintf(buf, bufsize_chars, fmt, args);
    va_end(args);
    return r;
}

UCRT_XP_API int __cdecl ucrt_xp_fwprintf(UCRT_XP_FILE *f, const wchar_t *fmt, ...)
{
    wchar_t stackbuf[512];
    wchar_t *heapbuf = NULL;
    wchar_t *outbuf = stackbuf;
    char ansi_stack[1024];
    char *ansi_buf = ansi_stack;
    int wneeded, aneeded;
    int written;
    va_list args;

    if (!f) return -1;

    va_start(args, fmt);
    wneeded = ucrt_xp_vswprintf(stackbuf, sizeof(stackbuf) / sizeof(wchar_t), fmt, args);
    va_end(args);
    if (wneeded < 0) return -1;

    if ((size_t)wneeded >= sizeof(stackbuf) / sizeof(wchar_t)) {
        heapbuf = (wchar_t *)ucrt_xp_malloc(((size_t)wneeded + 1) * sizeof(wchar_t));
        if (!heapbuf) return -1;
        va_start(args, fmt);
        ucrt_xp_vswprintf(heapbuf, (size_t)wneeded + 1, fmt, args);
        va_end(args);
        outbuf = heapbuf;
    }

    /* Widen-then-narrow round trip to the current code page, matching
     * classic text-mode wide-stream behavior - see the header comment on
     * ucrt_xp_fwprintf for why this isn't raw UTF-16 output. */
    aneeded = ucrt_xp_wide_to_ansi(outbuf, NULL, 0);
    if (aneeded < 0) { if (heapbuf) ucrt_xp_free(heapbuf); return -1; }
    if (aneeded >= (int)sizeof(ansi_stack)) {
        ansi_buf = (char *)ucrt_xp_malloc((size_t)aneeded + 1);
        if (!ansi_buf) { if (heapbuf) ucrt_xp_free(heapbuf); return -1; }
    }
    ucrt_xp_wide_to_ansi(outbuf, ansi_buf, aneeded + 1);

    written = (int)ucrt_xp_fwrite(ansi_buf, 1, (size_t)aneeded, f);

    if (heapbuf) ucrt_xp_free(heapbuf);
    if (ansi_buf != ansi_stack) ucrt_xp_free(ansi_buf);
    return (written == aneeded) ? written : -1;
}

/* ------------------------------------------------------------------ */
/* Wide scanf family (vswscanf / swscanf / vfwscanf / fwscanf / wscanf) */
/* ------------------------------------------------------------------ */

typedef struct WScanSrc {
    const wchar_t *str;
    UCRT_XP_FILE  *file;   /* for fwscanf: narrow stream, chars via fgetc */
    int chars_consumed;
} WScanSrc;

static int wsrc_get(WScanSrc *s)
{
    int c;
    if (s->str) {
        if (!*s->str) return -1;
        c = (int)(unsigned short)*s->str++;
        s->chars_consumed++;
        return c;
    }
    /* File path: read one narrow byte and treat as wchar (ASCII subset).
     * Full UTF-16 file scanning would need a wide stream abstraction;
     * this matches the project's "text mode narrows wide output" model. */
    c = ucrt_xp_fgetc(s->file);
    if (c != -1) s->chars_consumed++;
    return c;
}

static void wsrc_unget(WScanSrc *s, int c)
{
    if (c == -1) return;
    if (s->str) {
        s->str--;
        s->chars_consumed--;
        return;
    }
    ucrt_xp_ungetc(c, s->file);
    s->chars_consumed--;
}

static int is_wspace(int c)
{
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' ||
           c == L'\f' || c == L'\v';
}

static void wskip_ws(WScanSrc *s)
{
    int c;
    for (;;) {
        c = wsrc_get(s);
        if (c == -1) return;
        if (!is_wspace(c)) {
            wsrc_unget(s, c);
            return;
        }
    }
}

static int wdigit_val(int c, int base)
{
    int v = -1;
    if (c >= L'0' && c <= L'9') v = c - L'0';
    else if (c >= L'a' && c <= L'z') v = c - L'a' + 10;
    else if (c >= L'A' && c <= L'Z') v = c - L'A' + 10;
    if (v < 0 || v >= base) return -1;
    return v;
}

static int wscan_int(WScanSrc *s, int base, int width, int *is_neg, unsigned __int64 *out)
{
    int c, d, any = 0, got_sign = 0;
    unsigned __int64 acc = 0;

    *is_neg = 0;
    if (width == 0) width = 0x7fffffff;

    c = wsrc_get(s);
    if (c == L'+' || c == L'-') {
        if (c == L'-') *is_neg = 1;
        got_sign = 1;
        width--;
        if (width <= 0) { wsrc_unget(s, c); return 0; }
        c = wsrc_get(s);
    }

    if (base == 0) {
        if (c == L'0') {
            int c2 = wsrc_get(s);
            if (c2 == L'x' || c2 == L'X') {
                base = 16;
                width -= 2;
                if (width <= 0) return 0;
                c = wsrc_get(s);
            } else {
                base = 8;
                wsrc_unget(s, c2);
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && c == L'0') {
        int c2 = wsrc_get(s);
        if (c2 == L'x' || c2 == L'X') {
            width -= 2;
            if (width <= 0) return 0;
            c = wsrc_get(s);
        } else {
            wsrc_unget(s, c2);
        }
    }

    while (c != -1 && width > 0) {
        d = wdigit_val(c, base);
        if (d < 0) break;
        acc = acc * (unsigned)base + (unsigned)d;
        any = 1;
        width--;
        c = wsrc_get(s);
    }
    if (c != -1) wsrc_unget(s, c);
    (void)got_sign;
    if (!any) return 0;
    *out = acc;
    return 1;
}

static int wscan_float(WScanSrc *s, int width, double *out)
{
    char tmp[128];
    int i = 0, c;
    char *end = NULL;
    double v;

    if (width == 0 || width > (int)sizeof(tmp) - 1) width = (int)sizeof(tmp) - 1;

    c = wsrc_get(s);
    if (c == L'+' || c == L'-') {
        tmp[i++] = (char)c;
        width--;
        c = wsrc_get(s);
    }
    while (c != -1 && width > 0 &&
           ((c >= L'0' && c <= L'9') || c == L'.' ||
            c == L'e' || c == L'E' || c == L'+' || c == L'-')) {
        if ((c == L'+' || c == L'-') && i > 0 &&
            tmp[i-1] != 'e' && tmp[i-1] != 'E')
            break;
        if (c > 127) break;
        tmp[i++] = (char)c;
        width--;
        c = wsrc_get(s);
    }
    if (c != -1) wsrc_unget(s, c);
    tmp[i] = 0;
    if (i == 0) return 0;
    v = ucrt_xp_strtod(tmp, &end);
    if (end == tmp) return 0;
    *out = v;
    return 1;
}

static int parse_wscanset(const wchar_t **fmt_inout, unsigned char table[32], int *invert)
{
    const wchar_t *f = *fmt_inout;
    int i;
    for (i = 0; i < 32; i++) table[i] = 0;
    *invert = 0;

    if (*f == L'^') {
        *invert = 1;
        f++;
    }
    if (*f == L']') {
        table[']' >> 3] |= (unsigned char)(1u << (']' & 7));
        f++;
    }
    while (*f && *f != L']') {
        unsigned char c;
        if (*f > 255) { f++; continue; }
        c = (unsigned char)*f++;
        if (*f == L'-' && f[1] && f[1] != L']' && f[1] <= 255) {
            unsigned char hi = (unsigned char)f[1];
            unsigned char lo = c;
            f += 2;
            if (lo > hi) { unsigned char t = lo; lo = hi; hi = t; }
            for (c = lo; ; c++) {
                table[c >> 3] |= (unsigned char)(1u << (c & 7));
                if (c == hi) break;
            }
        } else {
            table[c >> 3] |= (unsigned char)(1u << (c & 7));
        }
    }
    if (*f != L']') return 0;
    f++;
    *fmt_inout = f;
    return 1;
}

static int wscanset_member(const unsigned char table[32], int invert, int c)
{
    int in;
    if (c < 0 || c > 255) return invert; /* non-ASCII: in inverted set only */
    in = (table[c >> 3] & (1u << (c & 7))) != 0;
    return invert ? !in : in;
}

static int do_vwscanf(WScanSrc *s, const wchar_t *fmt, va_list args)
{
    int assigned = 0;
    const wchar_t *f = fmt;

    while (*f) {
        if (is_wspace((int)*f)) {
            while (is_wspace((int)*f)) f++;
            wskip_ws(s);
            continue;
        }

        if (*f != L'%') {
            int c = wsrc_get(s);
            if (c != (int)*f) {
                if (c != -1) wsrc_unget(s, c);
                return assigned;
            }
            f++;
            continue;
        }

        f++;
        {
            int suppress = 0;
            int width = 0;
            int lenmod = 0;
            int conv;
            int is_neg = 0;
            unsigned __int64 uval;
            double dval;

            if (*f == L'*') { suppress = 1; f++; }

            while (*f >= L'0' && *f <= L'9') {
                width = width * 10 + (*f - L'0');
                f++;
            }

            if (*f == L'h') { lenmod = 1; f++; }
            else if (*f == L'l') {
                f++;
                if (*f == L'l') { lenmod = 3; f++; }
                else lenmod = 2; /* %lc / %ls → wchar */
            }

            conv = (int)*f;
            if (!conv) return assigned;
            f++;

            if (conv == L'%') {
                int c = wsrc_get(s);
                if (c != L'%') {
                    if (c != -1) wsrc_unget(s, c);
                    return assigned;
                }
                continue;
            }

            if (conv == L'n') {
                if (!suppress) {
                    int *p = va_arg(args, int *);
                    *p = s->chars_consumed;
                }
                continue;
            }

            if (conv == L'c') {
                int n = width ? width : 1;
                int i, c;
                if (lenmod == 2) {
                    wchar_t *p = suppress ? NULL : va_arg(args, wchar_t *);
                    for (i = 0; i < n; i++) {
                        c = wsrc_get(s);
                        if (c == -1) return assigned;
                        if (p) p[i] = (wchar_t)c;
                    }
                } else {
                    char *p = suppress ? NULL : va_arg(args, char *);
                    for (i = 0; i < n; i++) {
                        c = wsrc_get(s);
                        if (c == -1) return assigned;
                        if (p) p[i] = (char)c;
                    }
                }
                if (!suppress) assigned++;
                continue;
            }

            if (conv == L's') {
                int n = width ? width : 0x7fffffff;
                int i = 0, c;
                wskip_ws(s);
                if (lenmod == 2) {
                    wchar_t *p = suppress ? NULL : va_arg(args, wchar_t *);
                    for (;;) {
                        if (i >= n) break;
                        c = wsrc_get(s);
                        if (c == -1 || is_wspace(c)) {
                            if (c != -1) wsrc_unget(s, c);
                            break;
                        }
                        if (p) p[i] = (wchar_t)c;
                        i++;
                    }
                    if (i == 0) return assigned;
                    if (p) p[i] = 0;
                } else {
                    char *p = suppress ? NULL : va_arg(args, char *);
                    for (;;) {
                        if (i >= n) break;
                        c = wsrc_get(s);
                        if (c == -1 || is_wspace(c)) {
                            if (c != -1) wsrc_unget(s, c);
                            break;
                        }
                        if (p) p[i] = (char)c;
                        i++;
                    }
                    if (i == 0) return assigned;
                    if (p) p[i] = 0;
                }
                if (!suppress) assigned++;
                continue;
            }

            if (conv == L'[') {
                unsigned char table[32];
                int invert = 0;
                int n = width ? width : 0x7fffffff;
                int i = 0, c;
                const wchar_t *set_fmt = f;

                if (!parse_wscanset(&set_fmt, table, &invert))
                    return assigned;
                f = set_fmt;

                if (lenmod == 2) {
                    wchar_t *p = suppress ? NULL : va_arg(args, wchar_t *);
                    for (;;) {
                        if (i >= n) break;
                        c = wsrc_get(s);
                        if (c == -1 || !wscanset_member(table, invert, c)) {
                            if (c != -1) wsrc_unget(s, c);
                            break;
                        }
                        if (p) p[i] = (wchar_t)c;
                        i++;
                    }
                    if (i == 0) return assigned;
                    if (p) p[i] = 0;
                } else {
                    char *p = suppress ? NULL : va_arg(args, char *);
                    for (;;) {
                        if (i >= n) break;
                        c = wsrc_get(s);
                        if (c == -1 || !wscanset_member(table, invert, c)) {
                            if (c != -1) wsrc_unget(s, c);
                            break;
                        }
                        if (p) p[i] = (char)c;
                        i++;
                    }
                    if (i == 0) return assigned;
                    if (p) p[i] = 0;
                }
                if (!suppress) assigned++;
                continue;
            }

            wskip_ws(s);

            if (conv == L'd' || conv == L'i' || conv == L'u' ||
                conv == L'o' || conv == L'x' || conv == L'X') {
                int base = 10;
                if (conv == L'i') base = 0;
                else if (conv == L'u') base = 10;
                else if (conv == L'o') base = 8;
                else if (conv == L'x' || conv == L'X') base = 16;

                if (!wscan_int(s, base, width, &is_neg, &uval))
                    return assigned;

                if (!suppress) {
                    if (conv == L'u' || conv == L'o' || conv == L'x' || conv == L'X') {
                        if (lenmod == 3) {
                            unsigned __int64 *p = va_arg(args, unsigned __int64 *);
                            *p = uval;
                        } else if (lenmod == 2) {
                            unsigned long *p = va_arg(args, unsigned long *);
                            *p = (unsigned long)uval;
                        } else if (lenmod == 1) {
                            unsigned short *p = va_arg(args, unsigned short *);
                            *p = (unsigned short)uval;
                        } else {
                            unsigned int *p = va_arg(args, unsigned int *);
                            *p = (unsigned int)uval;
                        }
                    } else {
                        __int64 sval = is_neg ? -(__int64)uval : (__int64)uval;
                        if (lenmod == 3) {
                            __int64 *p = va_arg(args, __int64 *);
                            *p = sval;
                        } else if (lenmod == 2) {
                            long *p = va_arg(args, long *);
                            *p = (long)sval;
                        } else if (lenmod == 1) {
                            short *p = va_arg(args, short *);
                            *p = (short)sval;
                        } else {
                            int *p = va_arg(args, int *);
                            *p = (int)sval;
                        }
                    }
                    assigned++;
                }
                continue;
            }

            if (conv == L'f' || conv == L'e' || conv == L'g' ||
                conv == L'F' || conv == L'E' || conv == L'G') {
                if (!wscan_float(s, width, &dval))
                    return assigned;
                if (!suppress) {
                    if (lenmod == 2) {
                        double *p = va_arg(args, double *);
                        *p = dval;
                    } else {
                        float *p = va_arg(args, float *);
                        *p = (float)dval;
                    }
                    assigned++;
                }
                continue;
            }

            if (conv == L'p') {
                void **pp;
                if (!wscan_int(s, 16, width, &is_neg, &uval))
                    return assigned;
                if (!suppress) {
                    pp = va_arg(args, void **);
                    *pp = (void *)(size_t)uval;
                    assigned++;
                }
                continue;
            }

            return assigned;
        }
    }
    return assigned;
}

UCRT_XP_API int __cdecl ucrt_xp_vswscanf(
    const wchar_t *str, const wchar_t *fmt, va_list args)
{
    WScanSrc s;
    if (!str || !fmt) return -1;
    s.str = str;
    s.file = NULL;
    s.chars_consumed = 0;
    return do_vwscanf(&s, fmt, args);
}

UCRT_XP_API int __cdecl ucrt_xp_swscanf(
    const wchar_t *str, const wchar_t *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vswscanf(str, fmt, args);
    va_end(args);
    return r;
}

UCRT_XP_API int __cdecl ucrt_xp_vfwscanf(
    UCRT_XP_FILE *f, const wchar_t *fmt, va_list args)
{
    WScanSrc s;
    if (!f || !fmt) return -1;
    s.str = NULL;
    s.file = f;
    s.chars_consumed = 0;
    return do_vwscanf(&s, fmt, args);
}

UCRT_XP_API int __cdecl ucrt_xp_fwscanf(
    UCRT_XP_FILE *f, const wchar_t *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vfwscanf(f, fmt, args);
    va_end(args);
    return r;
}

UCRT_XP_API int __cdecl ucrt_xp_wscanf(const wchar_t *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vfwscanf(ucrt_xp_stdin(), fmt, args);
    va_end(args);
    return r;
}

/* ------------------------------------------------------------------ */
/* Additional wcs* / wmem* primitives (search, span, tokenize, dup)    */
/* ------------------------------------------------------------------ */

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcschr(const wchar_t *s, wchar_t c)
{
    if (!s) return NULL;
    for (; *s; s++) {
        if (*s == c) return (wchar_t *)s;
    }
    return (c == 0) ? (wchar_t *)s : NULL; /* wcschr(s, L'\0') finds the NUL */
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *found = NULL;
    if (!s) return NULL;
    for (; *s; s++) {
        if (*s == c) found = s;
    }
    if (c == 0) return (wchar_t *)s;
    return (wchar_t *)found;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsstr(
    const wchar_t *haystack, const wchar_t *needle)
{
    size_t nlen;
    if (!haystack || !needle) return NULL;
    nlen = ucrt_xp_wcslen(needle);
    if (nlen == 0) return (wchar_t *)haystack;

    for (; *haystack; haystack++) {
        if (ucrt_xp_wcsncmp(haystack, needle, nlen) == 0) return (wchar_t *)haystack;
    }
    return NULL;
}

UCRT_XP_API size_t __cdecl ucrt_xp_wcsspn(const wchar_t *s, const wchar_t *accept)
{
    const wchar_t *p = s;
    if (!s || !accept) return 0;
    for (; *p; p++) {
        if (!ucrt_xp_wcschr(accept, *p)) break;
    }
    return (size_t)(p - s);
}

UCRT_XP_API size_t __cdecl ucrt_xp_wcscspn(const wchar_t *s, const wchar_t *reject)
{
    const wchar_t *p = s;
    if (!s) return 0;
    if (!reject) return ucrt_xp_wcslen(s);
    for (; *p; p++) {
        if (ucrt_xp_wcschr(reject, *p)) break;
    }
    return (size_t)(p - s);
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    if (!s || !accept) return NULL;
    for (; *s; s++) {
        if (ucrt_xp_wcschr(accept, *s)) return (wchar_t *)s;
    }
    return NULL;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsncat(
    wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d;
    size_t i = 0;
    if (!dst) return dst;
    d = dst + ucrt_xp_wcslen(dst);
    if (src) {
        for (; i < n && src[i]; i++) d[i] = src[i];
    }
    d[i] = 0; /* always terminated, like strncat */
    return dst;
}

/* Case-insensitive compare of at most n characters (MSVC _wcsnicmp). */
UCRT_XP_API int __cdecl ucrt_xp_wcsnicmp(
    const wchar_t *a, const wchar_t *b, size_t n)
{
    size_t la, lb;
    int r;

    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);

    la = ucrt_xp_wcsnlen(a, n);
    lb = ucrt_xp_wcsnlen(b, n);
    r = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                       a, (int)la, b, (int)lb);
    return r - CSTR_EQUAL;
}

/* Result is allocated with ucrt_xp_malloc; release with ucrt_xp_free. */
UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsdup(const wchar_t *s)
{
    size_t len;
    wchar_t *copy;
    if (!s) return NULL;
    len = ucrt_xp_wcslen(s) + 1;
    copy = (wchar_t *)ucrt_xp_malloc(len * sizeof(wchar_t));
    if (copy) CopyMemory(copy, s, len * sizeof(wchar_t));
    return copy;
}

/* Reentrant wcstok with explicit saveptr (same design as strtok_r). */
UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcstok_r(
    wchar_t *str, const wchar_t *delim, wchar_t **saveptr)
{
    wchar_t *start;
    wchar_t *p;

    if (!saveptr || !delim) return NULL;
    if (!str) {
        str = *saveptr;
        if (!str) return NULL;
    }

    for (; *str; str++) {
        if (!ucrt_xp_wcschr(delim, *str)) break;
    }
    if (*str == 0) { *saveptr = str; return NULL; }

    start = str;
    for (p = str; *p; p++) {
        if (ucrt_xp_wcschr(delim, *p)) {
            *p = 0;
            *saveptr = p + 1;
            return start;
        }
    }
    *saveptr = p;
    return start;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    CopyMemory(dst, src, n * sizeof(wchar_t));
    return dst;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
    MoveMemory(dst, src, n * sizeof(wchar_t));
    return dst;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wmemset(wchar_t *dst, wchar_t c, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) dst[i] = c;
    return dst;
}

UCRT_XP_API int __cdecl ucrt_xp_wmemcmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wmemchr(const wchar_t *buf, wchar_t c, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (buf[i] == c) return (wchar_t *)(buf + i);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* wcsxfrm / wcsxfrm_l (wide collation transform)                      */
/* ------------------------------------------------------------------ */

/* Wide analogue of strxfrm. Uses LCMapStringW(LCMAP_SORTKEY). Sort keys
 * are sequences of bytes, but the classic CRT stores them as wchar_t
 * units (each byte zero-extended). We follow that convention so that
 * wcscmp on the results matches wcscoll ordering. */
UCRT_XP_API size_t __cdecl ucrt_xp_wcsxfrm_l(
    wchar_t *dest, const wchar_t *src, size_t n, ucrt_xp_locale_t loc)
{
    LCID lcid = ucrt_xp__locale_lcid(loc);
    int needed_bytes;
    size_t needed_wchars;
    size_t i;

    if (!src) {
        if (dest && n > 0) dest[0] = 0;
        return 0;
    }

    needed_bytes = LCMapStringW(lcid, LCMAP_SORTKEY, src, -1, NULL, 0);
    if (needed_bytes <= 0) {
        size_t len = ucrt_xp_wcslen(src);
        if (dest && n > 0) {
            size_t copy = (len < n - 1) ? len : n - 1;
            if (copy) CopyMemory(dest, src, copy * sizeof(wchar_t));
            dest[copy] = 0;
        }
        return len;
    }

    /* needed_bytes includes the terminating 0 byte of the sort key. */
    needed_wchars = (size_t)needed_bytes; /* each byte -> one wchar_t */

    if (!dest || n == 0) {
        return needed_wchars - 1;
    }

    if (needed_wchars > n) {
        /* Partial key: map into a temporary byte buffer then widen. */
        char *tmp = (char *)ucrt_xp_malloc(needed_bytes);
        if (tmp) {
            LCMapStringW(lcid, LCMAP_SORTKEY, src, -1, (LPWSTR)tmp, needed_bytes);
            for (i = 0; i < n - 1 && i < (size_t)needed_bytes - 1; i++)
                dest[i] = (wchar_t)(unsigned char)tmp[i];
            dest[n - 1] = 0;
            ucrt_xp_free(tmp);
        } else if (n > 0) {
            dest[0] = 0;
        }
        return needed_wchars - 1;
    }

    {
        char *tmp = (char *)ucrt_xp_malloc(needed_bytes);
        if (!tmp) {
            if (n > 0) dest[0] = 0;
            return needed_wchars - 1;
        }
        LCMapStringW(lcid, LCMAP_SORTKEY, src, -1, (LPWSTR)tmp, needed_bytes);
        for (i = 0; i < (size_t)needed_bytes; i++)
            dest[i] = (wchar_t)(unsigned char)tmp[i];
        ucrt_xp_free(tmp);
    }
    return needed_wchars - 1;
}

UCRT_XP_API size_t __cdecl ucrt_xp_wcsxfrm(
    wchar_t *dest, const wchar_t *src, size_t n)
{
    return ucrt_xp_wcsxfrm_l(dest, src, n, NULL);
}

/* In-place wide lowercase / uppercase (MSVC _wcslwr / _wcsupr). */
UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcslwr(wchar_t *s)
{
    if (!s) return s;
    CharLowerW(s);
    return s;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsupr(wchar_t *s)
{
    if (!s) return s;
    CharUpperW(s);
    return s;
}

/* ------------------------------------------------------------------ */
/* wcscoll / _wcsicoll / _wcsnicoll / _wcsrev                           */
/* ------------------------------------------------------------------ */

/* Locale-aware wide string comparison (C89 wcscoll). */
UCRT_XP_API int __cdecl ucrt_xp_wcscoll_l(
    const wchar_t *a, const wchar_t *b, ucrt_xp_locale_t loc)
{
    LCID lcid = ucrt_xp__locale_lcid(loc);
    int r;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    r = CompareStringW(lcid, 0, a, -1, b, -1);
    if (r == 0) return ucrt_xp_wcscmp(a, b);
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_wcscoll(const wchar_t *a, const wchar_t *b)
{
    return ucrt_xp_wcscoll_l(a, b, NULL);
}

/* Locale-aware case-insensitive wide comparison (MSVC _wcsicoll). */
UCRT_XP_API int __cdecl ucrt_xp_wcsicoll_l(
    const wchar_t *a, const wchar_t *b, ucrt_xp_locale_t loc)
{
    LCID lcid = ucrt_xp__locale_lcid(loc);
    int r;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    r = CompareStringW(lcid, NORM_IGNORECASE, a, -1, b, -1);
    if (r == 0) return ucrt_xp_wcsicmp(a, b);
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_wcsicoll(const wchar_t *a, const wchar_t *b)
{
    return ucrt_xp_wcsicoll_l(a, b, NULL);
}

/* Case-insensitive locale collation of at most n wide characters
 * (MSVC _wcsnicoll). */
UCRT_XP_API int __cdecl ucrt_xp_wcsnicoll_l(
    const wchar_t *a, const wchar_t *b, size_t n, ucrt_xp_locale_t loc)
{
    LCID lcid = ucrt_xp__locale_lcid(loc);
    size_t la, lb;
    int r;

    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);

    la = ucrt_xp_wcsnlen(a, n);
    lb = ucrt_xp_wcsnlen(b, n);

    r = CompareStringW(lcid, NORM_IGNORECASE, a, (int)la, b, (int)lb);
    if (r == 0) return ucrt_xp_wcsnicmp(a, b, n);
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_wcsnicoll(
    const wchar_t *a, const wchar_t *b, size_t n)
{
    return ucrt_xp_wcsnicoll_l(a, b, n, NULL);
}

/* Reverse a wide string in place (MSVC _wcsrev). */
UCRT_XP_API wchar_t* __cdecl ucrt_xp_wcsrev(wchar_t *s)
{
    wchar_t *lo, *hi;
    if (!s || !*s) return s;
    lo = s;
    hi = s + ucrt_xp_wcslen(s) - 1;
    while (lo < hi) {
        wchar_t t = *lo;
        *lo++ = *hi;
        *hi-- = t;
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* Wide filesystem / process helpers                                   */
/* ------------------------------------------------------------------ */

UCRT_XP_API int __cdecl ucrt_xp_waccess(const wchar_t *path, int mode)
{
    DWORD attrs;
    if (!path) return -1;
    attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return -1;
    if ((mode & 2) && (attrs & FILE_ATTRIBUTE_READONLY)) return -1;
    (void)mode;
    return 0;
}

UCRT_XP_API int __cdecl ucrt_xp_wmkdir(const wchar_t *path)
{
    if (!path) return -1;
    return CreateDirectoryW(path, NULL) ? 0 : -1;
}

UCRT_XP_API int __cdecl ucrt_xp_wchdir(const wchar_t *path)
{
    if (!path) return -1;
    return SetCurrentDirectoryW(path) ? 0 : -1;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wgetcwd(wchar_t *buf, int maxlen)
{
    DWORD n;
    if (!buf || maxlen <= 0) return NULL;
    n = GetCurrentDirectoryW((DWORD)maxlen, buf);
    if (n == 0 || n >= (DWORD)maxlen) return NULL;
    return buf;
}

UCRT_XP_API int __cdecl ucrt_xp_wremove(const wchar_t *path)
{
    if (!path) return -1;
    if (DeleteFileW(path)) return 0;
    if (RemoveDirectoryW(path)) return 0;
    return -1;
}

UCRT_XP_API int __cdecl ucrt_xp_wrename(const wchar_t *oldpath, const wchar_t *newpath)
{
    if (!oldpath || !newpath) return -1;
    return MoveFileW(oldpath, newpath) ? 0 : -1;
}

UCRT_XP_API wchar_t* __cdecl ucrt_xp_wgetenv(const wchar_t *name)
{
    /* Non-reentrant: static buffer, classic CRT style. */
    static wchar_t buf[32768];
    DWORD n;
    if (!name) return NULL;
    n = GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768) return NULL;
    return buf;
}

UCRT_XP_API int __cdecl ucrt_xp_wputenv(const wchar_t *envstring)
{
    wchar_t *copy, *eq;
    size_t len;
    BOOL ok;
    if (!envstring || !*envstring) return -1;
    len = ucrt_xp_wcslen(envstring);
    copy = (wchar_t *)ucrt_xp_malloc((len + 1) * sizeof(wchar_t));
    if (!copy) return -1;
    CopyMemory(copy, envstring, (len + 1) * sizeof(wchar_t));
    eq = ucrt_xp_wcschr(copy, L'=');
    if (!eq) {
        ucrt_xp_free(copy);
        return -1;
    }
    *eq = 0;
    ok = SetEnvironmentVariableW(copy, (eq[1] == 0) ? NULL : (eq + 1));
    ucrt_xp_free(copy);
    return ok ? 0 : -1;
}

UCRT_XP_API int __cdecl ucrt_xp_wsystem(const wchar_t *command)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    wchar_t *cmd_copy;
    size_t len;

    if (!command) {
        return GetEnvironmentVariableW(L"COMSPEC", NULL, 0) > 0 ? 1 : 0;
    }
    len = ucrt_xp_wcslen(command);
    cmd_copy = (wchar_t *)ucrt_xp_malloc((len + 1) * sizeof(wchar_t));
    if (!cmd_copy) return -1;
    CopyMemory(cmd_copy, command, (len + 1) * sizeof(wchar_t));

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmd_copy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
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
/* _wpopen / _wfindfirst family                                        */
/* ------------------------------------------------------------------ */

UCRT_XP_API UCRT_XP_FILE* __cdecl ucrt_xp_wpopen(const wchar_t *command, const wchar_t *mode)
{
    char cmd_a[32768];
    char mode_a[8];
    if (!command || !mode) return NULL;
    if (ucrt_xp_wide_to_ansi(command, cmd_a, (int)sizeof(cmd_a)) < 0) return NULL;
    if (ucrt_xp_wide_to_ansi(mode, mode_a, (int)sizeof(mode_a)) < 0) return NULL;
    return ucrt_xp_popen(cmd_a, mode_a);
}

typedef struct WFindCtx {
    HANDLE h;
    WIN32_FIND_DATAW fd;
} WFindCtx;

UCRT_XP_API intptr_t __cdecl ucrt_xp_wfindfirst(const wchar_t *filespec, UCRT_XP_WFINDDATA *data)
{
    WFindCtx *ctx;
    if (!filespec || !data) return -1;
    ctx = (WFindCtx *)ucrt_xp_malloc(sizeof(WFindCtx));
    if (!ctx) return -1;
    ctx->h = FindFirstFileW(filespec, &ctx->fd);
    if (ctx->h == INVALID_HANDLE_VALUE) {
        ucrt_xp_free(ctx);
        return -1;
    }
    lstrcpynW(data->name, ctx->fd.cFileName, 260);
    data->attrib = ctx->fd.dwFileAttributes;
    data->size = ((__int64)ctx->fd.nFileSizeHigh << 32) | ctx->fd.nFileSizeLow;
    data->time_write = 0;
    return (intptr_t)ctx;
}

UCRT_XP_API int __cdecl ucrt_xp_wfindnext(intptr_t handle, UCRT_XP_WFINDDATA *data)
{
    WFindCtx *ctx = (WFindCtx *)handle;
    if (!ctx || !data || ctx->h == INVALID_HANDLE_VALUE) return -1;
    if (!FindNextFileW(ctx->h, &ctx->fd)) return -1;
    lstrcpynW(data->name, ctx->fd.cFileName, 260);
    data->attrib = ctx->fd.dwFileAttributes;
    data->size = ((__int64)ctx->fd.nFileSizeHigh << 32) | ctx->fd.nFileSizeLow;
    data->time_write = 0;
    return 0;
}

UCRT_XP_API int __cdecl ucrt_xp_wfindclose(intptr_t handle)
{
    WFindCtx *ctx = (WFindCtx *)handle;
    if (!ctx) return -1;
    if (ctx->h != INVALID_HANDLE_VALUE) FindClose(ctx->h);
    ucrt_xp_free(ctx);
    return 0;
}
