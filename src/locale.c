/*
 * locale.c - locale objects as immutable, reference-counted values instead
 * of process-global mutable state. This sidesteps the classic XP-era CRT
 * bug class where one thread's setlocale() races another thread's
 * strcoll()/toupper() and both observe a half-updated LC_* table.
 *
 * A ucrt_xp_locale_t, once created, never changes. "Changing locale" means
 * creating a new object and pointing the current thread's slot at it;
 * anyone still holding a reference to the old object keeps working
 * against a consistent snapshot.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

struct UCRT_XP_LOCALE {
    LONG refcount;
    char name[64];
    /* Real implementation would cache LCID, code page, LC_* tables etc.
     * resolved once at creation time via GetLocaleInfoA / setlocale. */
    LCID lcid;
    UINT codepage;
    UCRT_XP_LCONV lconv; /* resolved once here, never touched again */
};

static DWORD g_locale_tls = TLS_OUT_OF_INDEXES;
static UCRT_XP_ONCE g_locale_tls_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_locale_tls(void *param)
{
    (void)param;
    g_locale_tls = TlsAlloc();
    return g_locale_tls != TLS_OUT_OF_INDEXES;
}

static const struct { const char *name; LANGID langid; } g_locale_table[] = {
    /* XP predates LocaleNameToLCID (that's Vista+), so common BCP-47-ish
     * names are resolved through a small static table built from
     * MAKELANGID(primary, sublang) instead - this is exactly what real
     * XP-era CRTs did internally (see setlocale()'s XP implementation).
     * Not exhaustive; add entries as needed rather than trying to cover
     * every LCID XP knows about. */
    { "en-US", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) },
    { "en-GB", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK) },
    { "ja-JP", MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT) },
    { "de-DE", MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN) },
    { "fr-FR", MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH) },
    { "es-ES", MAKELANGID(LANG_SPANISH, SUBLANG_SPANISH) },
    { "it-IT", MAKELANGID(LANG_ITALIAN, SUBLANG_ITALIAN) },
    { "pt-BR", MAKELANGID(LANG_PORTUGUESE, SUBLANG_PORTUGUESE_BRAZILIAN) },
    { "ru-RU", MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT) },
    { "zh-CN", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED) },
    { "zh-TW", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL) },
    { "ko-KR", MAKELANGID(LANG_KOREAN, SUBLANG_DEFAULT) },
};
#define UCRT_XP_LOCALE_TABLE_COUNT \
    (sizeof(g_locale_table) / sizeof(g_locale_table[0]))

static LCID resolve_lcid_by_name(const char *name)
{
    size_t i;
    for (i = 0; i < UCRT_XP_LOCALE_TABLE_COUNT; i++) {
        if (lstrcmpiA(name, g_locale_table[i].name) == 0) {
            return MAKELCID(g_locale_table[i].langid, SORT_DEFAULT);
        }
    }
    return 0; /* not found */
}

UCRT_XP_API ucrt_xp_locale_t __cdecl ucrt_xp_locale_create(const char *name)
{
    struct UCRT_XP_LOCALE *loc = (struct UCRT_XP_LOCALE *)
        ucrt_xp_malloc(sizeof(struct UCRT_XP_LOCALE));
    if (!loc) return NULL;

    loc->refcount = 1;
    if (name) {
        lstrcpynA(loc->name, name, sizeof(loc->name));
    } else {
        lstrcpynA(loc->name, "C", sizeof(loc->name));
    }

    /* "C"/"POSIX" map to the invariant locale; named locales are looked
     * up in the static table above (period-correct for XP, which has no
     * LocaleNameToLCID); anything else falls back to the user's own
     * Control-Panel-configured default rather than failing outright. */
    if (lstrcmpiA(loc->name, "C") == 0 || lstrcmpiA(loc->name, "POSIX") == 0) {
        loc->lcid = LOCALE_INVARIANT;
        loc->codepage = CP_ACP;
    } else {
        LCID found = resolve_lcid_by_name(loc->name);
        loc->lcid = found ? found : LOCALE_USER_DEFAULT;
        loc->codepage = CP_ACP;
    }

    /* Resolve LC_NUMERIC/LC_MONETARY-ish fields exactly once, here, so
     * that every other function just reads an immutable struct instead
     * of calling into the OS (and instead of touching any process-global
     * CRT locale state, which is the bug class this design avoids). */
    if (GetLocaleInfoA(loc->lcid, LOCALE_SDECIMAL, loc->lconv.decimal_point,
                        sizeof(loc->lconv.decimal_point)) == 0) {
        lstrcpynA(loc->lconv.decimal_point, ".", sizeof(loc->lconv.decimal_point));
    }
    if (GetLocaleInfoA(loc->lcid, LOCALE_STHOUSAND, loc->lconv.thousands_sep,
                        sizeof(loc->lconv.thousands_sep)) == 0) {
        lstrcpynA(loc->lconv.thousands_sep, ",", sizeof(loc->lconv.thousands_sep));
    }
    if (GetLocaleInfoA(loc->lcid, LOCALE_SCURRENCY, loc->lconv.currency_symbol,
                        sizeof(loc->lconv.currency_symbol)) == 0) {
        lstrcpynA(loc->lconv.currency_symbol, "$", sizeof(loc->lconv.currency_symbol));
    }

    return (ucrt_xp_locale_t)loc;
}

UCRT_XP_API ucrt_xp_locale_t __cdecl ucrt_xp_locale_addref(ucrt_xp_locale_t loc)
{
    if (loc) InterlockedIncrement(&loc->refcount);
    return loc;
}

UCRT_XP_API void __cdecl ucrt_xp_locale_release(ucrt_xp_locale_t loc)
{
    if (!loc) return;
    if (InterlockedDecrement(&loc->refcount) == 0) {
        ucrt_xp_free(loc);
    }
}

LCID ucrt_xp__locale_lcid(ucrt_xp_locale_t loc)
{
    if (!loc) loc = ucrt_xp_locale_get_thread();
    return loc ? loc->lcid : LOCALE_USER_DEFAULT;
}

const char *ucrt_xp__locale_name(ucrt_xp_locale_t loc)
{
    if (!loc) loc = ucrt_xp_locale_get_thread();
    return loc ? loc->name : "C";
}


UCRT_XP_API ucrt_xp_locale_t __cdecl ucrt_xp_locale_get_thread(void)
{
    ucrt_xp_locale_t loc;
    ucrt_xp_once(&g_locale_tls_once, init_locale_tls, NULL);

    loc = (ucrt_xp_locale_t)TlsGetValue(g_locale_tls);
    if (!loc) {
        /* Lazily create the default "C" locale for this thread the first
         * time it's asked, rather than requiring explicit setup. */
        loc = ucrt_xp_locale_create("C");
        if (loc) TlsSetValue(g_locale_tls, loc);
    }
    return loc;
}

UCRT_XP_API void __cdecl ucrt_xp_locale_set_thread(ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t old;
    ucrt_xp_once(&g_locale_tls_once, init_locale_tls, NULL);

    old = (ucrt_xp_locale_t)TlsGetValue(g_locale_tls);
    ucrt_xp_locale_addref(loc);
    TlsSetValue(g_locale_tls, loc);
    if (old) ucrt_xp_locale_release(old);
}

UCRT_XP_API int __cdecl ucrt_xp_stricmp_l(
    const char *a, const char *b, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    /* CompareStringA gives locale-correct case folding without touching
     * any process-global CRT locale state. */
    int r = CompareStringA(use_loc->lcid, NORM_IGNORECASE, a, -1, b, -1);
    /* CompareStringA returns 1/2/3 for </==/>; normalize to -1/0/1. */
    return r - CSTR_EQUAL;
}

/* Non-locale variant: uses the calling thread's current locale
 * (same semantics as MSVC's _stricmp). */
UCRT_XP_API int __cdecl ucrt_xp_stricmp(
    const char *a, const char *b)
{
    return ucrt_xp_stricmp_l(a, b, NULL);
}

UCRT_XP_API BOOL __cdecl ucrt_xp_locale_get_lconv(
    ucrt_xp_locale_t loc, UCRT_XP_LCONV *out)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    if (!use_loc || !out) return FALSE;
    /* Struct copy of the cached, immutable snapshot - safe to hand back
     * to the caller with no lifetime coupling to the locale object. */
    *out = use_loc->lconv;
    return TRUE;
}

UCRT_XP_API int __cdecl ucrt_xp_toupper_l(int c, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    char ch = (char)c;
    char buf[2];
    buf[0] = ch;
    buf[1] = 0;
    if (CharUpperBuffA(buf, 1) == 0) return c;
    (void)use_loc; /* LCMapStringA(use_loc->lcid, ...) would be used for a
                     * fully locale-correct mapping; CharUpperBuffA covers
                     * the common single-byte-codepage case adequately for
                     * this reference implementation. */
    return (unsigned char)buf[0];
}

UCRT_XP_API int __cdecl ucrt_xp_tolower_l(int c, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    char ch = (char)c;
    char buf[2];
    buf[0] = ch;
    buf[1] = 0;
    if (CharLowerBuffA(buf, 1) == 0) return c;
    (void)use_loc;
    return (unsigned char)buf[0];
}

/* ------------------------------------------------------------------ */
/* strcoll / strxfrm (locale-aware collation)                          */
/* ------------------------------------------------------------------ */

/* Locale-aware string comparison (C89 strcoll). Uses CompareStringA
 * without NORM_IGNORECASE so diacritics / code-page ordering match the
 * locale's SORTKEY rules. Returns <0 / 0 / >0 like strcmp. */
UCRT_XP_API int __cdecl ucrt_xp_strcoll_l(
    const char *a, const char *b, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    LCID lcid = use_loc ? use_loc->lcid : LOCALE_USER_DEFAULT;
    int r;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    r = CompareStringA(lcid, 0, a, -1, b, -1);
    if (r == 0) {
        /* CompareString failure (rare) - fall back to byte compare. */
        return ucrt_xp_strcmp(a, b);
    }
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_strcoll(const char *a, const char *b)
{
    return ucrt_xp_strcoll_l(a, b, NULL);
}

/* Transform src into a form that can be compared with strcmp to yield
 * the same ordering as strcoll. Uses LCMapStringA(LCMAP_SORTKEY).
 * If n == 0 or dest == NULL, returns the number of bytes needed
 * (including the terminating 0). Otherwise copies at most n bytes and
 * returns the length that would have been written (excluding the
 * terminating 0 when the result fitted, matching classic CRT). */
UCRT_XP_API size_t __cdecl ucrt_xp_strxfrm_l(
    char *dest, const char *src, size_t n, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    LCID lcid = use_loc ? use_loc->lcid : LOCALE_USER_DEFAULT;
    int needed;

    if (!src) {
        if (dest && n > 0) dest[0] = 0;
        return 0;
    }

    /* Query required size first (includes terminating 0 for SORTKEY). */
    needed = LCMapStringA(lcid, LCMAP_SORTKEY, src, -1, NULL, 0);
    if (needed <= 0) {
        /* Fallback: identity transform via strcpy semantics. */
        size_t len = ucrt_xp_strlen(src);
        if (dest && n > 0) {
            size_t copy = (len < n - 1) ? len : n - 1;
            if (copy) CopyMemory(dest, src, copy);
            dest[copy] = 0;
        }
        return len;
    }

    if (!dest || n == 0) {
        return (size_t)(needed - 1); /* exclude the terminating 0 */
    }

    if ((size_t)needed > n) {
        /* Not enough room - still produce a partial key so strcmp is
         * defined, then return the full required length. */
        LCMapStringA(lcid, LCMAP_SORTKEY, src, -1, dest, (int)n);
        if (n > 0) dest[n - 1] = 0;
        return (size_t)(needed - 1);
    }

    LCMapStringA(lcid, LCMAP_SORTKEY, src, -1, dest, (int)n);
    return (size_t)(needed - 1);
}

UCRT_XP_API size_t __cdecl ucrt_xp_strxfrm(
    char *dest, const char *src, size_t n)
{
    return ucrt_xp_strxfrm_l(dest, src, n, NULL);
}

/* ------------------------------------------------------------------ */
/* _stricoll / _strnicoll (case-insensitive locale collation)          */
/* ------------------------------------------------------------------ */

/* Locale-aware case-insensitive string comparison (MSVC _stricoll). */
UCRT_XP_API int __cdecl ucrt_xp_stricoll_l(
    const char *a, const char *b, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    LCID lcid = use_loc ? use_loc->lcid : LOCALE_USER_DEFAULT;
    int r;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    r = CompareStringA(lcid, NORM_IGNORECASE, a, -1, b, -1);
    if (r == 0) return ucrt_xp_stricmp(a, b);
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_stricoll(const char *a, const char *b)
{
    return ucrt_xp_stricoll_l(a, b, NULL);
}

/* Case-insensitive locale collation of at most n characters
 * (MSVC _strnicoll). Stops at the first NUL in either string. */
UCRT_XP_API int __cdecl ucrt_xp_strnicoll_l(
    const char *a, const char *b, size_t n, ucrt_xp_locale_t loc)
{
    ucrt_xp_locale_t use_loc = loc ? loc : ucrt_xp_locale_get_thread();
    LCID lcid = use_loc ? use_loc->lcid : LOCALE_USER_DEFAULT;
    size_t la, lb;
    int r;

    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);

    la = 0;
    while (la < n && a[la]) la++;
    lb = 0;
    while (lb < n && b[lb]) lb++;

    r = CompareStringA(lcid, NORM_IGNORECASE, a, (int)la, b, (int)lb);
    if (r == 0) return ucrt_xp_strnicmp(a, b, n);
    return r - CSTR_EQUAL;
}

UCRT_XP_API int __cdecl ucrt_xp_strnicoll(
    const char *a, const char *b, size_t n)
{
    return ucrt_xp_strnicoll_l(a, b, n, NULL);
}

/* ------------------------------------------------------------------ */
/* Classic setlocale / localeconv (thin layer over locale objects)     */
/* ------------------------------------------------------------------ */

UCRT_XP_API char* __cdecl ucrt_xp_setlocale(int category, const char *locale)
{
    static char name_buf[64];
    ucrt_xp_locale_t loc;
    (void)category; /* LC_* ignored: one process/thread locale object */

    if (!locale) {
        /* Query: return current thread locale name. */
        loc = ucrt_xp_locale_get_thread();
        if (!loc) return NULL;
        lstrcpynA(name_buf, ucrt_xp__locale_name(loc), sizeof(name_buf));
        return name_buf;
    }
    loc = ucrt_xp_locale_create(locale);
    if (!loc) return NULL;
    ucrt_xp_locale_set_thread(loc);
    ucrt_xp_locale_release(loc); /* set_thread addref'd */
    lstrcpynA(name_buf, locale, sizeof(name_buf));
    return name_buf;
}

UCRT_XP_API UCRT_XP_LCONV* __cdecl ucrt_xp_localeconv(void)
{
    static UCRT_XP_LCONV cached;
    if (!ucrt_xp_locale_get_lconv(NULL, &cached)) {
        lstrcpynA(cached.decimal_point, ".", sizeof(cached.decimal_point));
        lstrcpynA(cached.thousands_sep, ",", sizeof(cached.thousands_sep));
        lstrcpynA(cached.currency_symbol, "$", sizeof(cached.currency_symbol));
    }
    return &cached;
}
