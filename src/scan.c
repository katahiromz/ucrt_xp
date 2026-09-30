/*
 * scan.c - a practical scanf/sscanf subset for ported CRT code.
 *
 * Supported conversions: d i u o x X c s f e g n p [  %
 * Supported width: digits
 * Supported length: h, l, ll (treated as 64-bit for integers)
 * Scansets: %[abc], %[^abc], ranges %[a-z], and combinations.
 *
 * Whitespace in the format string matches any amount of input whitespace.
 * Literal characters must match exactly.
 *
 * Locale-aware numeric parsing is still not implemented (uses C locale
 * digit rules). Wide scanf lives in wide.c.
 */
#include "internal.h"
#include <stdarg.h>

typedef struct ScanSrc {
    const char *str;       /* for sscanf: pointer into the string */
    UCRT_XP_FILE *file;    /* for scanf/fscanf: stream, or NULL */
    int chars_consumed;
} ScanSrc;

static int src_peek(ScanSrc *s)
{
    int c;
    if (s->str) {
        return (unsigned char)*s->str;
    }
    c = ucrt_xp_fgetc(s->file);
    if (c != -1) ucrt_xp_ungetc(c, s->file);
    return c;
}

static int src_get(ScanSrc *s)
{
    int c;
    if (s->str) {
        if (!*s->str) return -1;
        c = (unsigned char)*s->str++;
        s->chars_consumed++;
        return c;
    }
    c = ucrt_xp_fgetc(s->file);
    if (c != -1) s->chars_consumed++;
    return c;
}

static void src_unget(ScanSrc *s, int c)
{
    if (c == -1) return;
    if (s->str) {
        if (s->str > (const char *)0) { /* always true; just step back */
            s->str--;
            s->chars_consumed--;
        }
        return;
    }
    ucrt_xp_ungetc(c, s->file);
    s->chars_consumed--;
}

static int is_space_char(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static void skip_ws(ScanSrc *s)
{
    int c;
    for (;;) {
        c = src_get(s);
        if (c == -1) return;
        if (!is_space_char(c)) {
            src_unget(s, c);
            return;
        }
    }
}

static int digit_val(int c, int base)
{
    int v = -1;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
    if (v < 0 || v >= base) return -1;
    return v;
}

/* Returns 1 on success, 0 on failure. Writes via *out (unsigned long long). */
static int scan_int(ScanSrc *s, int base, int width, int *is_neg, unsigned __int64 *out)
{
    int c, d, any = 0, got_sign = 0;
    unsigned __int64 acc = 0;

    *is_neg = 0;
    if (width == 0) width = 0x7fffffff;

    c = src_get(s);
    if (c == '+' || c == '-') {
        if (c == '-') *is_neg = 1;
        got_sign = 1;
        width--;
        if (width <= 0) { src_unget(s, c); return 0; }
        c = src_get(s);
    }

    if (base == 0) {
        if (c == '0') {
            int c2 = src_get(s);
            if (c2 == 'x' || c2 == 'X') {
                base = 16;
                width -= 2;
                if (width <= 0) return 0;
                c = src_get(s);
            } else {
                base = 8;
                src_unget(s, c2);
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && c == '0') {
        int c2 = src_get(s);
        if (c2 == 'x' || c2 == 'X') {
            width -= 2;
            if (width <= 0) return 0;
            c = src_get(s);
        } else {
            src_unget(s, c2);
        }
    }

    while (c != -1 && width > 0) {
        d = digit_val(c, base);
        if (d < 0) break;
        acc = acc * (unsigned)base + (unsigned)d;
        any = 1;
        width--;
        c = src_get(s);
    }
    if (c != -1) src_unget(s, c);

    if (!any) {
        if (got_sign) { /* consumed sign only - leave it? already consumed */
        }
        return 0;
    }
    *out = acc;
    return 1;
}

static int scan_float(ScanSrc *s, int width, double *out)
{
    char tmp[128];
    int i = 0, c;
    char *end = NULL;
    double v;

    if (width == 0 || width > (int)sizeof(tmp) - 1) width = (int)sizeof(tmp) - 1;

    c = src_get(s);
    if (c == '+' || c == '-') {
        tmp[i++] = (char)c;
        width--;
        c = src_get(s);
    }
    while (c != -1 && width > 0 && ((c >= '0' && c <= '9') || c == '.' ||
           c == 'e' || c == 'E' || c == '+' || c == '-')) {
        /* allow sign only right after e/E */
        if ((c == '+' || c == '-') && i > 0 && tmp[i-1] != 'e' && tmp[i-1] != 'E')
            break;
        tmp[i++] = (char)c;
        width--;
        c = src_get(s);
    }
    if (c != -1) src_unget(s, c);
    tmp[i] = 0;
    if (i == 0) return 0;

    v = ucrt_xp_strtod(tmp, &end);
    if (end == tmp) return 0;
    *out = v;
    return 1;
}

/* Build a 256-bit membership table for a scanset. Advances *fmt_inout
 * past the closing ']'. Returns 1 on success, 0 if the format is malformed
 * (no closing ']'). invert=1 means %[^...] (match chars NOT in the set). */
static int parse_scanset(const char **fmt_inout, unsigned char table[32], int *invert)
{
    const char *f = *fmt_inout;
    int i;
    for (i = 0; i < 32; i++) table[i] = 0;
    *invert = 0;

    if (*f == '^') {
        *invert = 1;
        f++;
    }

    /* First character after '[' (or after '^') may be ']' itself and
     * still belongs to the set - classic scanf rule. */
    if (*f == ']') {
        table[']' >> 3] |= (unsigned char)(1u << (']' & 7));
        f++;
    }

    while (*f && *f != ']') {
        unsigned char c = (unsigned char)*f++;
        /* Range: a-z where a <= z. '-' as first/last char is literal. */
        if (*f == '-' && f[1] && f[1] != ']') {
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
    if (*f != ']') return 0;
    f++; /* skip ']' */
    *fmt_inout = f;
    return 1;
}

static int scanset_member(const unsigned char table[32], int invert, int c)
{
    int in;
    if (c < 0 || c > 255) return 0;
    in = (table[c >> 3] & (1u << (c & 7))) != 0;
    return invert ? !in : in;
}

static int do_vscanf(ScanSrc *s, const char *fmt, va_list args)
{
    int assigned = 0;
    const char *f = fmt;

    while (*f) {
        if (is_space_char((unsigned char)*f)) {
            while (is_space_char((unsigned char)*f)) f++;
            skip_ws(s);
            continue;
        }

        if (*f != '%') {
            int c = src_get(s);
            if (c != (unsigned char)*f) {
                if (c != -1) src_unget(s, c);
                return assigned;
            }
            f++;
            continue;
        }

        f++; /* skip '%' */
        {
            int suppress = 0;
            int width = 0;
            int lenmod = 0; /* 0=default, 1=h, 2=l, 3=ll */
            int conv;
            int is_neg = 0;
            unsigned __int64 uval;
            double dval;

            if (*f == '*') { suppress = 1; f++; }

            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }

            if (*f == 'h') { lenmod = 1; f++; }
            else if (*f == 'l') {
                f++;
                if (*f == 'l') { lenmod = 3; f++; }
                else lenmod = 2;
            }

            conv = (unsigned char)*f;
            if (!conv) return assigned;
            f++;

            if (conv == '%') {
                int c = src_get(s);
                if (c != '%') {
                    if (c != -1) src_unget(s, c);
                    return assigned;
                }
                continue;
            }

            if (conv == 'n') {
                if (!suppress) {
                    int *p = va_arg(args, int *);
                    *p = s->chars_consumed;
                }
                continue;
            }

            if (conv == 'c') {
                int n = width ? width : 1;
                int i, c;
                char *p = suppress ? NULL : va_arg(args, char *);
                for (i = 0; i < n; i++) {
                    c = src_get(s);
                    if (c == -1) return assigned;
                    if (p) p[i] = (char)c;
                }
                if (!suppress) assigned++;
                continue;
            }

            if (conv == 's') {
                int n = width ? width : 0x7fffffff;
                int i = 0, c;
                char *p = suppress ? NULL : va_arg(args, char *);
                skip_ws(s);
                for (;;) {
                    if (i >= n) break;
                    c = src_get(s);
                    if (c == -1 || is_space_char(c)) {
                        if (c != -1) src_unget(s, c);
                        break;
                    }
                    if (p) p[i] = (char)c;
                    i++;
                }
                if (i == 0) return assigned;
                if (p) p[i] = 0;
                if (!suppress) assigned++;
                continue;
            }

            if (conv == '[') {
                /* Scanset: %[...] or %[^...] — must NOT skip leading whitespace. */
                unsigned char table[32];
                int invert = 0;
                int n = width ? width : 0x7fffffff;
                int i = 0, c;
                char *p;
                const char *set_fmt = f; /* f already past the '[' */

                if (!parse_scanset(&set_fmt, table, &invert))
                    return assigned;
                f = set_fmt; /* advanced past closing ']' */

                p = suppress ? NULL : va_arg(args, char *);
                for (;;) {
                    if (i >= n) break;
                    c = src_get(s);
                    if (c == -1 || !scanset_member(table, invert, c)) {
                        if (c != -1) src_unget(s, c);
                        break;
                    }
                    if (p) p[i] = (char)c;
                    i++;
                }
                if (i == 0) return assigned;
                if (p) p[i] = 0;
                if (!suppress) assigned++;
                continue;
            }

            /* numeric conversions skip leading whitespace */
            skip_ws(s);

            if (conv == 'd' || conv == 'i' || conv == 'u' ||
                conv == 'o' || conv == 'x' || conv == 'X') {
                int base = 10;
                if (conv == 'i') base = 0;
                else if (conv == 'u') base = 10;
                else if (conv == 'o') base = 8;
                else if (conv == 'x' || conv == 'X') base = 16;

                if (!scan_int(s, base, width, &is_neg, &uval))
                    return assigned;

                if (!suppress) {
                    if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
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

            if (conv == 'f' || conv == 'e' || conv == 'g' ||
                conv == 'F' || conv == 'E' || conv == 'G') {
                if (!scan_float(s, width, &dval))
                    return assigned;
                if (!suppress) {
                    if (lenmod == 2) { /* double via %lf */
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

            if (conv == 'p') {
                /* Pointer: accept optional 0x/0X prefix, then hex digits.
                 * Written as void* (same size as size_t on Win32/Win64). */
                void **pp;
                if (!scan_int(s, 16, width, &is_neg, &uval))
                    return assigned;
                if (!suppress) {
                    pp = va_arg(args, void **);
                    *pp = (void *)(size_t)uval;
                    assigned++;
                }
                continue;
            }

            /* unknown conversion */
            return assigned;
        }
    }
    return assigned;
}

UCRT_XP_API int __cdecl ucrt_xp_vsscanf(const char *str, const char *fmt, va_list args)
{
    ScanSrc s;
    if (!str || !fmt) return -1;
    s.str = str;
    s.file = NULL;
    s.chars_consumed = 0;
    return do_vscanf(&s, fmt, args);
}

UCRT_XP_API int __cdecl ucrt_xp_sscanf(const char *str, const char *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vsscanf(str, fmt, args);
    va_end(args);
    return r;
}

UCRT_XP_API int __cdecl ucrt_xp_vfscanf(UCRT_XP_FILE *f, const char *fmt, va_list args)
{
    ScanSrc s;
    if (!f || !fmt) return -1;
    s.str = NULL;
    s.file = f;
    s.chars_consumed = 0;
    return do_vscanf(&s, fmt, args);
}

UCRT_XP_API int __cdecl ucrt_xp_fscanf(UCRT_XP_FILE *f, const char *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vfscanf(f, fmt, args);
    va_end(args);
    return r;
}

UCRT_XP_API int __cdecl ucrt_xp_scanf(const char *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vfscanf(ucrt_xp_stdin(), fmt, args);
    va_end(args);
    return r;
}
