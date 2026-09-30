/*
 * convert.c - numeric string conversions (<stdlib.h>'s atoi/atol/atof/
 * strtol/strtoul/strtod family), plus rand()/srand() and the qsort()/
 * bsearch() generic algorithms.
 *
 * rand()/srand() are the one place here that deliberately differs from
 * the classic CRT: real CRT rand() state is process-global (or at best
 * TLS-based depending on /MT vs /MD and CRT version - this inconsistency
 * was itself a real XP-era portability trap), which either races across
 * threads or gives every thread the exact same sequence depending on
 * which CRT you linked. ucrt_xp_rand() is always per-thread, always,
 * with no ifdef-dependent behavior to be surprised by.
 */
#include "internal.h"

/* ------------------------------------------------------------------ */
/* String -> integer                                                   */
/* ------------------------------------------------------------------ */

__declspec(dllexport) long __cdecl ucrt_xp_strtol(const char *s, char **endptr, int base)
{
    const char *p = s;
    int neg = 0;
    unsigned long acc = 0;
    int any = 0;

    if (!s) { if (endptr) *endptr = (char *)s; return 0; }

    while (ucrt_xp_isspace((unsigned char)*p)) p++;

    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2; base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    for (; *p; p++) {
        int digit;
        char c = *p;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;

        acc = acc * (unsigned long)base + (unsigned long)digit;
        any = 1;
    }

    if (endptr) *endptr = (char *)(any ? p : s);
    return neg ? -(long)acc : (long)acc;
}

__declspec(dllexport) unsigned long __cdecl ucrt_xp_strtoul(const char *s, char **endptr, int base)
{
    /* Shares strtol's digit-scanning logic; sign handling for unsigned
     * conversion follows the standard's own (slightly odd) rule that a
     * leading '-' is honored by negating the accumulated magnitude. */
    const char *p = s;
    int neg = 0;
    unsigned long acc = 0;
    int any = 0;

    if (!s) { if (endptr) *endptr = (char *)s; return 0; }
    while (ucrt_xp_isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2; base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }

    for (; *p; p++) {
        int digit;
        char c = *p;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * (unsigned long)base + (unsigned long)digit;
        any = 1;
    }

    if (endptr) *endptr = (char *)(any ? p : s);
    return neg ? (unsigned long)(-(long)acc) : acc;
}

__declspec(dllexport) int __cdecl ucrt_xp_atoi(const char *s)
{
    return (int)ucrt_xp_strtol(s, NULL, 10);
}

__declspec(dllexport) long __cdecl ucrt_xp_atol(const char *s)
{
    return ucrt_xp_strtol(s, NULL, 10);
}

/* ------------------------------------------------------------------ */
/* String -> double                                                    */
/* ------------------------------------------------------------------ */

/*
 * A straightforward accumulate-and-scale strtod, adequate for config
 * files/protocol text/UI input. Like format.c's float formatter, this
 * is not a correctly-rounded (Clinger/David Gay-class) implementation -
 * don't rely on it for numerically sensitive parsing of edge-case
 * values near a double's representable-precision boundary.
 */
__declspec(dllexport) double __cdecl ucrt_xp_strtod(const char *s, char **endptr)
{
    const char *p = s;
    int neg = 0;
    double result = 0.0;
    int any = 0;

    if (!s) { if (endptr) *endptr = (char *)s; return 0.0; }
    while (ucrt_xp_isspace((unsigned char)*p)) p++;

    if (*p == '+') p++;
    else if (*p == '-') { neg = 1; p++; }

    while (ucrt_xp_isdigit((unsigned char)*p)) {
        result = result * 10.0 + (double)(*p - '0');
        p++; any = 1;
    }

    if (*p == '.') {
        double scale = 0.1;
        p++;
        while (ucrt_xp_isdigit((unsigned char)*p)) {
            result += (double)(*p - '0') * scale;
            scale *= 0.1;
            p++; any = 1;
        }
    }

    if (any && (*p == 'e' || *p == 'E')) {
        const char *exp_start = p;
        int exp_neg = 0;
        int exp_val = 0;
        int exp_any = 0;
        p++;
        if (*p == '+') p++;
        else if (*p == '-') { exp_neg = 1; p++; }
        while (ucrt_xp_isdigit((unsigned char)*p)) {
            exp_val = exp_val * 10 + (*p - '0');
            p++; exp_any = 1;
        }
        if (exp_any) {
            int i;
            double factor = 1.0;
            for (i = 0; i < exp_val; i++) factor *= 10.0;
            result = exp_neg ? result / factor : result * factor;
        } else {
            p = exp_start; /* no digits after 'e' - not part of the number */
        }
    }

    if (endptr) *endptr = (char *)(any ? p : s);
    return neg ? -result : result;
}

__declspec(dllexport) double __cdecl ucrt_xp_atof(const char *s)
{
    return ucrt_xp_strtod(s, NULL);
}

/* ------------------------------------------------------------------ */
/* abs/labs                                                             */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_abs(int v) { return v < 0 ? -v : v; }
__declspec(dllexport) long __cdecl ucrt_xp_labs(long v) { return v < 0 ? -v : v; }

/* ------------------------------------------------------------------ */
/* rand/srand - always per-thread (see file header rationale)          */
/* ------------------------------------------------------------------ */

static DWORD g_rand_tls = TLS_OUT_OF_INDEXES;
static UCRT_XP_ONCE g_rand_tls_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_rand_tls(void *param)
{
    (void)param;
    g_rand_tls = TlsAlloc();
    return g_rand_tls != TLS_OUT_OF_INDEXES;
}

static unsigned long *get_rand_state(void)
{
    unsigned long *state;
    ucrt_xp_once(&g_rand_tls_once, init_rand_tls, NULL);
    if (g_rand_tls == TLS_OUT_OF_INDEXES) return NULL;

    state = (unsigned long *)TlsGetValue(g_rand_tls);
    if (!state) {
        state = (unsigned long *)ucrt_xp_malloc(sizeof(unsigned long));
        if (state) {
            /* Seeded from the thread ID + tick count by default, so two
             * threads that never call srand() still don't produce the
             * exact same sequence (a real gotcha with naive per-thread
             * PRNG designs seeded from a shared default). */
            *state = (unsigned long)GetCurrentThreadId() ^ GetTickCount();
            TlsSetValue(g_rand_tls, state);
        }
    }
    return state;
}

__declspec(dllexport) void __cdecl ucrt_xp_srand(unsigned int seed)
{
    unsigned long *state = get_rand_state();
    if (state) *state = seed;
}

__declspec(dllexport) int __cdecl ucrt_xp_rand(void)
{
    /* Classic linear congruential generator (same constants as the
     * historical Microsoft CRT rand()), so output ranges/behavior feel
     * familiar to anyone porting existing code, while being genuinely
     * per-thread rather than relying on whichever CRT's global-state
     * behavior happened to be linked in. Not cryptographically secure -
     * same caveat as the CRT rand() it mirrors. */
    unsigned long *state = get_rand_state();
    if (!state) return 0;
    *state = (*state * 214013UL + 2531011UL);
    return (int)((*state >> 16) & 0x7fff);
}

/* ------------------------------------------------------------------ */
/* qsort / bsearch                                                     */
/* ------------------------------------------------------------------ */

static void swap_bytes(char *a, char *b, size_t size)
{
    while (size--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Textbook median-of-three quicksort with insertion-sort cutover for
 * small partitions - not claiming to match any particular CRT's exact
 * comparison-count behavior, only the standard's contract (stable it is
 * NOT, same as the real qsort()). */
static void quicksort(char *base, size_t n, size_t size, UCRT_XP_COMPARE_FN cmp)
{
    if (n < 2) return;

    if (n <= 8) {
        size_t i;
        for (i = 1; i < n; i++) {
            size_t j = i;
            while (j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0) {
                swap_bytes(base + (j - 1) * size, base + j * size, size);
                j--;
            }
        }
        return;
    }

    {
        size_t mid = n / 2;
        char *pivot_pos = base + mid * size;
        size_t lo = 0, hi = n - 1;

        swap_bytes(pivot_pos, base + hi * size, size); /* pivot to end */
        {
            char *pivot = base + hi * size;
            size_t store = 0;
            size_t i;
            for (i = 0; i < hi; i++) {
                if (cmp(base + i * size, pivot) < 0) {
                    swap_bytes(base + i * size, base + store * size, size);
                    store++;
                }
            }
            swap_bytes(base + store * size, pivot, size);
            lo = store;
        }

        quicksort(base, lo, size, cmp);
        quicksort(base + (lo + 1) * size, n - lo - 1, size, cmp);
    }
}

__declspec(dllexport) void __cdecl ucrt_xp_qsort(
    void *base, size_t count, size_t size, UCRT_XP_COMPARE_FN cmp)
{
    if (!base || !cmp || count == 0 || size == 0) return;
    quicksort((char *)base, count, size, cmp);
}

__declspec(dllexport) void* __cdecl ucrt_xp_bsearch(
    const void *key, const void *base, size_t count, size_t size, UCRT_XP_COMPARE_FN cmp)
{
    size_t lo = 0, hi = count;
    if (!key || !base || !cmp || size == 0) return NULL;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *elem = (const char *)base + mid * size;
        int c = cmp(key, elem);
        if (c == 0) return (void *)elem;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Integer -> string (MSVC _itoa / _ltoa / _ultoa)                     */
/* ------------------------------------------------------------------ */

static char *xtoa_unsigned(unsigned long value, char *str, int radix, int is_neg)
{
    char tmp[36];
    int i = 0, j = 0;
    const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    if (!str || radix < 2 || radix > 36) return NULL;

    if (value == 0) {
        str[0] = '0';
        str[1] = 0;
        return str;
    }

    while (value) {
        tmp[i++] = digits[value % (unsigned)radix];
        value /= (unsigned)radix;
    }
    if (is_neg && radix == 10) tmp[i++] = '-';

    while (i > 0) str[j++] = tmp[--i];
    str[j] = 0;
    return str;
}

__declspec(dllexport) char* __cdecl ucrt_xp_itoa(int value, char *str, int radix)
{
    if (value < 0 && radix == 10)
        return xtoa_unsigned((unsigned long)(-(long)value), str, radix, 1);
    return xtoa_unsigned((unsigned long)(unsigned int)value, str, radix, 0);
}

__declspec(dllexport) char* __cdecl ucrt_xp_ltoa(long value, char *str, int radix)
{
    if (value < 0 && radix == 10)
        return xtoa_unsigned((unsigned long)(-value), str, radix, 1);
    return xtoa_unsigned((unsigned long)value, str, radix, 0);
}

__declspec(dllexport) char* __cdecl ucrt_xp_ultoa(unsigned long value, char *str, int radix)
{
    return xtoa_unsigned(value, str, radix, 0);
}
