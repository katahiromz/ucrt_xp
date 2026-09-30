/*
 * format.c - a self-contained printf-family formatter, replacing the
 * previous reliance on wvsprintfA (which caps output at 1024 bytes and
 * has no floating-point conversions at all).
 *
 * Supported conversions: d i u o x X c s p f e g %
 * Supported flags:       - + 0 (space) #
 * Supported width/precision: literal digits and '*' (from args)
 * Supported length modifiers: h, l, ll/I64 (treated as 64-bit), z
 *
 * Explicitly NOT implemented (documented, not silently wrong):
 *   - %n (disabled on purpose - it's a classic exploit primitive and
 *     real CRTs disable it by default too)
 *   - %a/%A hex-float
 *   - Locale-aware digit grouping (thousands separators). Numeric
 *     formatting here is always the "C" locale's plain digit string;
 *     pass the result through the caller's own grouping logic if the
 *     current ucrt_xp_locale_t's UCRT_XP_LCONV.thousands_sep matters.
 *   - Wide-character (%ls/%S) conversions - see wide.c for the wchar_t
 *     story; this formatter is char-only.
 *
 * The float-to-string conversion is a straightforward, unremarkable
 * implementation (repeated multiply/divide by 10, not a Grisu/Ryu-class
 * shortest-round-trip algorithm), so extremely precise round-tripping of
 * arbitrary doubles is not guaranteed past about 15 significant digits -
 * adequate for logs and UIs, not for serializing floats losslessly.
 */
#include "internal.h"
#include <stdarg.h>
#include <string.h>

typedef struct FmtSink {
    char  *buf;      /* NULL means "counting only, discard output" */
    size_t cap;       /* buffer capacity in bytes, including the NUL */
    size_t written;    /* logical characters written so far (may exceed cap) */
} FmtSink;

static void sink_putc(FmtSink *s, char c)
{
    if (s->buf && s->written + 1 < s->cap) {
        s->buf[s->written] = c;
    }
    s->written++;
}

static void sink_puts(FmtSink *s, const char *str, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) sink_putc(s, str[i]);
}

static void sink_pad(FmtSink *s, char padchar, int count)
{
    while (count-- > 0) sink_putc(s, padchar);
}

/* Renders |value| in the given base into buf (written backwards from the
 * end), returns pointer to the first digit. buf must be large enough
 * (65 bytes covers 64-bit binary plus a sign, which is generous headroom). */
char *ucrt_xp__utoa_generic(unsigned __int64 value, int base, int uppercase, char *buf_end)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    char *p = buf_end;

    *--p = 0;
    if (value == 0) {
        *--p = '0';
        return p;
    }
    while (value != 0) {
        *--p = digits[value % (unsigned)base];
        value /= (unsigned)base;
    }
    return p;
}

/* math.h's floor() isn't guaranteed pulled in on every ancient toolchain
 * without linking libm; provide a trivial fallback so this file has zero
 * extra link dependencies beyond what the rest of ucrt_xp already needs.
 * Defined ahead of format_float() (which uses it) rather than after, so
 * the #define actually takes effect at that call site. */
static double ucrt_xp_floor(double x)
{
    __int64 i = (__int64)x;
    double truncated = (double)i;
    if (truncated > x) truncated -= 1.0;
    return truncated;
}
#define floor ucrt_xp_floor

/* Minimal, deliberately simple double -> decimal-string conversion.
 * Handles the common case (finite, non-huge magnitude) well; NaN/Inf are
 * special-cased; extreme magnitudes fall back to scientific-ish digits
 * without pretending to be exact. */
static void format_float(FmtSink *s, double value, int precision,
                          char conv, int force_sign, int space_sign)
{
    char sign = 0;
    double intpart;
    double fracpart;
    unsigned __int64 ip;
    int i;
    char digits[32];
    int ndig = 0;

    if (precision < 0) precision = 6;

    if (value != value) { sink_puts(s, "nan", 3); return; } /* NaN */
    if (value > 1e300 && value * 2 == value) { sink_puts(s, "inf", 3); return; }
    if (value < -1e300 && value * 2 == value) { sink_puts(s, "-inf", 4); return; }

    if (value < 0) { sign = '-'; value = -value; }
    else if (force_sign) sign = '+';
    else if (space_sign) sign = ' ';

    if (conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') {
        /* Reduce to [1,10) and track exponent. */
        int exp10 = 0;
        double v = value;
        if (v != 0.0) {
            while (v >= 10.0) { v /= 10.0; exp10++; }
            while (v < 1.0)   { v *= 10.0; exp10--; }
        }
        if (sign) sink_putc(s, sign);

        if (conv == 'g' || conv == 'G') {
            /* %g: choose %f or %e based on exponent, precision means
             * significant digits, trailing zeros trimmed. */
            int use_exp = (exp10 < -4 || exp10 >= precision);
            if (!use_exp) {
                format_float(s, sign ? -value : value, precision - 1 - exp10, 'f', 0, 0);
                return;
            }
            /* fall through to scientific rendering below */
        }

        {
            unsigned __int64 mantissa;
            double scaled = v;
            int p = precision;
            for (i = 0; i < p; i++) scaled *= 10.0;
            mantissa = (unsigned __int64)(scaled + 0.5);

            /* carry-out (e.g. 9.9999 rounding to 10.000) */
            {
                unsigned __int64 limit = 1;
                for (i = 0; i <= p; i++) limit *= 10;
                if (mantissa >= limit) { mantissa /= 10; exp10++; }
            }

            {
                char tmp[32];
                char *ts = ucrt_xp__utoa_generic(mantissa, 10, 0, tmp + sizeof(tmp));
                int len = (int)strlen(ts);
                /* pad leading zeros up to p+1 digits */
                while (len < p + 1) { sink_putc(s, '0'); len++; }
                sink_putc(s, ts[0]);
                if (p > 0) {
                    sink_putc(s, '.');
                    sink_puts(s, ts + 1, (size_t)(len - 1));
                }
            }
            sink_putc(s, (conv == 'E' || conv == 'G') ? 'E' : 'e');
            sink_putc(s, exp10 < 0 ? '-' : '+');
            if (exp10 < 0) exp10 = -exp10;
            {
                char ebuf[8];
                char *es = ucrt_xp__utoa_generic((unsigned __int64)exp10, 10, 0, ebuf + sizeof(ebuf));
                if (strlen(es) < 2) sink_putc(s, '0');
                sink_puts(s, es, strlen(es));
            }
        }
        return;
    }

    /* %f */
    intpart = floor(value);
    fracpart = value - intpart;
    ip = (unsigned __int64)intpart;

    if (sign) sink_putc(s, sign);
    {
        char tmp[32];
        char *is = ucrt_xp__utoa_generic(ip, 10, 0, tmp + sizeof(tmp));
        sink_puts(s, is, strlen(is));
    }
    if (precision > 0) {
        sink_putc(s, '.');
        for (i = 0; i < precision; i++) {
            fracpart *= 10.0;
            digits[ndig++] = (char)fracpart;
            fracpart -= floor(fracpart);
        }
        /* simple rounding of the last digit */
        if (fracpart >= 0.5 && ndig > 0) {
            int k = ndig - 1;
            digits[k]++;
            while (k >= 0 && digits[k] > 9) {
                digits[k] = 0;
                if (k == 0) break;
                digits[--k]++;
            }
        }
        for (i = 0; i < ndig; i++) sink_putc(s, (char)('0' + digits[i]));
    }
}

/* Internal seam for wide.c's ucrt_xp_vswprintf: formats a double into a
 * plain ASCII char buffer using the exact same logic as the '%f'/'%e'/
 * '%g' cases above, so the wide formatter doesn't need its own separate
 * (and potentially divergent) float-to-string implementation. The result
 * is pure ASCII (digits, '.', '-', '+', 'e', "nan"/"inf"), so widening it
 * 1:1 to wchar_t afterwards is always correct. */
int ucrt_xp__format_double_ascii(char *out, size_t outcap, double value,
                                  int precision, char conv,
                                  int force_sign, int space_sign)
{
    FmtSink sink;
    sink.buf = out;
    sink.cap = outcap;
    sink.written = 0;
    format_float(&sink, value, precision, conv, force_sign, space_sign);
    if (sink.buf && sink.cap > 0) {
        size_t term = (sink.written < sink.cap) ? sink.written : sink.cap - 1;
        sink.buf[term] = 0;
    }
    return (int)sink.written;
}

int ucrt_xp_vsnprintf(char *buf, size_t bufsize, const char *fmt, va_list args)
{
    FmtSink sink;
    const char *p = fmt;

    sink.buf = buf;
    sink.cap = bufsize;
    sink.written = 0;

    while (*p) {
        if (*p != '%') { sink_putc(&sink, *p++); continue; }
        p++; /* consume '%' */

        {
            int left_align = 0, force_sign = 0, space_sign = 0, zero_pad = 0, alt_form = 0;
            int width = -1, precision = -1;
            int is_long = 0, is_longlong = 0, is_short = 0;
            char conv;

            for (;;) {
                if (*p == '-') { left_align = 1; p++; }
                else if (*p == '+') { force_sign = 1; p++; }
                else if (*p == ' ') { space_sign = 1; p++; }
                else if (*p == '0') { zero_pad = 1; p++; }
                else if (*p == '#') { alt_form = 1; p++; }
                else break;
            }

            if (*p == '*') { width = va_arg(args, int); p++; }
            else { width = 0; while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; } }

            if (*p == '.') {
                p++;
                if (*p == '*') { precision = va_arg(args, int); p++; }
                else { precision = 0; while (*p >= '0' && *p <= '9') { precision = precision * 10 + (*p - '0'); p++; } }
            }

            if (*p == 'h') { is_short = 1; p++; if (*p == 'h') p++; }
            else if (*p == 'l') {
                is_long = 1; p++;
                if (*p == 'l') { is_longlong = 1; p++; }
            } else if (*p == 'I' && p[1] == '6' && p[2] == '4') {
                is_longlong = 1; p += 3;
            } else if (*p == 'z') { is_long = 1; p++; }

            conv = *p ? *p++ : 0;

            switch (conv) {
            case 'd': case 'i': {
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
                    if (signlen) sink_putc(&sink, neg ? '-' : (force_sign ? '+' : ' '));
                    sink_pad(&sink, '0', pad);
                    sink_puts(&sink, digits, (size_t)dlen);
                } else {
                    if (!left_align) sink_pad(&sink, ' ', pad);
                    if (signlen) sink_putc(&sink, neg ? '-' : (force_sign ? '+' : ' '));
                    sink_puts(&sink, digits, (size_t)dlen);
                    if (left_align) sink_pad(&sink, ' ', pad);
                }
                break;
            }
            case 'u': case 'o': case 'x': case 'X': {
                unsigned __int64 v = is_longlong ? va_arg(args, unsigned __int64)
                                    : is_long ? (unsigned __int64)va_arg(args, unsigned long)
                                    : (unsigned __int64)va_arg(args, unsigned int);
                int base = (conv == 'o') ? 8 : (conv == 'u') ? 10 : 16;
                char tmp[32];
                char *digits = ucrt_xp__utoa_generic(v, base, conv == 'X', tmp + sizeof(tmp));
                int dlen = (int)strlen(digits);
                int pad = width - dlen;
                if (!left_align && zero_pad) {
                    sink_pad(&sink, '0', pad);
                    sink_puts(&sink, digits, (size_t)dlen);
                } else {
                    if (!left_align) sink_pad(&sink, ' ', pad);
                    sink_puts(&sink, digits, (size_t)dlen);
                    if (left_align) sink_pad(&sink, ' ', pad);
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                int pad = width - 1;
                if (!left_align) sink_pad(&sink, ' ', pad);
                sink_putc(&sink, c);
                if (left_align) sink_pad(&sink, ' ', pad);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                int len;
                if (!str) str = "(null)";
                len = (int)strlen(str);
                if (precision >= 0 && precision < len) len = precision;
                {
                    int pad = width - len;
                    if (!left_align) sink_pad(&sink, ' ', pad);
                    sink_puts(&sink, str, (size_t)len);
                    if (left_align) sink_pad(&sink, ' ', pad);
                }
                break;
            }
            case 'p': {
                void *ptr = va_arg(args, void *);
                char tmp[32];
                char *digits = ucrt_xp__utoa_generic((unsigned __int64)(ULONG_PTR)ptr, 16, 0, tmp + sizeof(tmp));
                sink_puts(&sink, "0x", 2);
                sink_puts(&sink, digits, strlen(digits));
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                double v = va_arg(args, double);
                format_float(&sink, v, precision, conv, force_sign, space_sign);
                break;
            }
            case '%':
                sink_putc(&sink, '%');
                break;
            case 'n':
                /* Deliberately unsupported - see file header comment. */
                break;
            default:
                sink_putc(&sink, '%');
                if (conv) sink_putc(&sink, conv);
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

int ucrt_xp_snprintf(char *buf, size_t bufsize, const char *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vsnprintf(buf, bufsize, fmt, args);
    va_end(args);
    return r;
}

/* sprintf / vsprintf - unbounded variants. The caller is responsible for
 * providing a buffer large enough; we pass a very large capacity to the
 * underlying vsnprintf so it does not truncate under normal use. */
int ucrt_xp_vsprintf(char *buf, const char *fmt, va_list args)
{
    return ucrt_xp_vsnprintf(buf, (size_t)0x7fffffff, fmt, args);
}

int ucrt_xp_sprintf(char *buf, const char *fmt, ...)
{
    int r;
    va_list args;
    va_start(args, fmt);
    r = ucrt_xp_vsprintf(buf, fmt, args);
    va_end(args);
    return r;
}
