/*
 * ctype.c - the classic <ctype.h> classification/case functions, fixed
 * to the "C" locale (plain ASCII 0-127) by definition - exactly what
 * the real CRT's is-family/toupper/tolower do for the default "C" locale, and
 * exactly what most code actually wants when it calls isalpha() on
 * protocol/format text rather than user-facing strings. For anything
 * that needs to be locale-aware, use ucrt_xp_toupper_l/tolower_l from
 * locale.c instead - that's the one that consults a ucrt_xp_locale_t.
 *
 * Table-driven rather than branch-driven: one 256-entry flag table,
 * built once from compile-time constants, then every is*() call is a
 * single array lookup. Matches how real CRTs implement this table
 * (historically exposed even as the public _pctype table).
 */
#include "internal.h"

#ifndef _WINT_T_DEFINED
typedef unsigned short wint_t;
#define _WINT_T_DEFINED
#endif
#ifndef WEOF
#define WEOF ((wint_t)0xFFFF)
#endif

enum {
    UCRT_XP_CT_UPPER  = 0x01,
    UCRT_XP_CT_LOWER  = 0x02,
    UCRT_XP_CT_DIGIT  = 0x04,
    UCRT_XP_CT_SPACE  = 0x08,
    UCRT_XP_CT_PUNCT  = 0x10,
    UCRT_XP_CT_CNTRL  = 0x20,
    UCRT_XP_CT_XDIGIT = 0x40
};

static unsigned char g_ctype_table[256];
static UCRT_XP_ONCE g_ctype_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl build_ctype_table(void *param)
{
    int c;
    (void)param;

    for (c = 0; c < 256; c++) {
        unsigned char flags = 0;
        if (c >= 'A' && c <= 'Z') flags |= UCRT_XP_CT_UPPER;
        if (c >= 'a' && c <= 'z') flags |= UCRT_XP_CT_LOWER;
        if (c >= '0' && c <= '9') flags |= UCRT_XP_CT_DIGIT;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r')
            flags |= UCRT_XP_CT_SPACE;
        if (c < 0x20 || c == 0x7f) flags |= UCRT_XP_CT_CNTRL;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            flags |= UCRT_XP_CT_XDIGIT;
        if (c >= 0x21 && c <= 0x7e &&
            !(flags & (UCRT_XP_CT_UPPER | UCRT_XP_CT_LOWER | UCRT_XP_CT_DIGIT)))
            flags |= UCRT_XP_CT_PUNCT;
        g_ctype_table[c] = flags;
    }
    return TRUE;
}

static unsigned char classify(int c)
{
    ucrt_xp_once(&g_ctype_once, build_ctype_table, NULL);
    if (c < 0 || c > 255) return 0; /* matches CRT UB-avoidance convention:
                                      * anything outside unsigned char / EOF
                                      * range is simply "not classified" */
    return g_ctype_table[c];
}

__declspec(dllexport) int __cdecl ucrt_xp_isalpha(int c)  { return (classify(c) & (UCRT_XP_CT_UPPER | UCRT_XP_CT_LOWER)) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isdigit(int c)  { return (classify(c) & UCRT_XP_CT_DIGIT) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isalnum(int c)  { return (classify(c) & (UCRT_XP_CT_UPPER | UCRT_XP_CT_LOWER | UCRT_XP_CT_DIGIT)) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isspace(int c)  { return (classify(c) & UCRT_XP_CT_SPACE) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isupper(int c)  { return (classify(c) & UCRT_XP_CT_UPPER) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_islower(int c)  { return (classify(c) & UCRT_XP_CT_LOWER) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_ispunct(int c)  { return (classify(c) & UCRT_XP_CT_PUNCT) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_iscntrl(int c)  { return (classify(c) & UCRT_XP_CT_CNTRL) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isxdigit(int c) { return (classify(c) & UCRT_XP_CT_XDIGIT) != 0; }
__declspec(dllexport) int __cdecl ucrt_xp_isprint(int c)  { return c >= 0x20 && c <= 0x7e; }
__declspec(dllexport) int __cdecl ucrt_xp_isgraph(int c)  { return c > 0x20 && c <= 0x7e; }

__declspec(dllexport) int __cdecl ucrt_xp_toupper(int c)
{
    return ucrt_xp_islower(c) ? c - ('a' - 'A') : c;
}

__declspec(dllexport) int __cdecl ucrt_xp_tolower(int c)
{
    return ucrt_xp_isupper(c) ? c + ('a' - 'A') : c;
}

/* ------------------------------------------------------------------ */
/* MSVC / C99 extensions                                               */
/* ------------------------------------------------------------------ */

/* __isascii / isascii: 7-bit ASCII range. */
__declspec(dllexport) int __cdecl ucrt_xp_isascii(int c)
{
    return ((unsigned)c & ~0x7f) == 0;
}

/* __toascii / toascii: clear high bits. */
__declspec(dllexport) int __cdecl ucrt_xp_toascii(int c)
{
    return c & 0x7f;
}

/* C99 isblank: space or tab only (not the full isspace set). */
__declspec(dllexport) int __cdecl ucrt_xp_isblank(int c)
{
    return c == ' ' || c == '\t';
}

/* __iscsymf / iscsymf: legal first character of a C identifier
 * (letter or underscore). */
__declspec(dllexport) int __cdecl ucrt_xp_iscsymf(int c)
{
    return ucrt_xp_isalpha(c) || c == '_';
}

/* __iscsym / iscsym: legal non-first character of a C identifier
 * (letter, digit, or underscore). */
__declspec(dllexport) int __cdecl ucrt_xp_iscsym(int c)
{
    return ucrt_xp_isalnum(c) || c == '_';
}

/* Unchecked MSVC _tolower / _toupper (assume known case; still safe). */
__declspec(dllexport) int __cdecl ucrt_xp__tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

__declspec(dllexport) int __cdecl ucrt_xp__toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

/* ------------------------------------------------------------------ */
/* Wide character classification (minimal C/ASCII-range isw*)          */
/* ------------------------------------------------------------------ */

/* Treat wchar_t values that fit in unsigned char via the same table;
 * anything outside 0..255 is not classified (matches CRT "C" locale). */
static int isw_classify_mask(wint_t c, unsigned char mask)
{
    if (c > 255) return 0;
    /* Reuse narrow classify by calling the public is* which use the table. */
    switch (mask) {
    case 1: return ucrt_xp_isalpha((int)c);
    case 2: return ucrt_xp_isdigit((int)c);
    case 3: return ucrt_xp_isalnum((int)c);
    case 4: return ucrt_xp_isspace((int)c);
    case 5: return ucrt_xp_isupper((int)c);
    case 6: return ucrt_xp_islower((int)c);
    case 7: return ucrt_xp_ispunct((int)c);
    case 8: return ucrt_xp_iscntrl((int)c);
    case 9: return ucrt_xp_isxdigit((int)c);
    case 10: return ucrt_xp_isprint((int)c);
    case 11: return ucrt_xp_isgraph((int)c);
    case 12: return ucrt_xp_isblank((int)c);
    default: return 0;
    }
}

__declspec(dllexport) int __cdecl ucrt_xp_iswalpha(wint_t c)  { return isw_classify_mask(c, 1); }
__declspec(dllexport) int __cdecl ucrt_xp_iswdigit(wint_t c)  { return isw_classify_mask(c, 2); }
__declspec(dllexport) int __cdecl ucrt_xp_iswalnum(wint_t c)  { return isw_classify_mask(c, 3); }
__declspec(dllexport) int __cdecl ucrt_xp_iswspace(wint_t c)  { return isw_classify_mask(c, 4); }
__declspec(dllexport) int __cdecl ucrt_xp_iswupper(wint_t c)  { return isw_classify_mask(c, 5); }
__declspec(dllexport) int __cdecl ucrt_xp_iswlower(wint_t c)  { return isw_classify_mask(c, 6); }
__declspec(dllexport) int __cdecl ucrt_xp_iswpunct(wint_t c)  { return isw_classify_mask(c, 7); }
__declspec(dllexport) int __cdecl ucrt_xp_iswcntrl(wint_t c)  { return isw_classify_mask(c, 8); }
__declspec(dllexport) int __cdecl ucrt_xp_iswxdigit(wint_t c) { return isw_classify_mask(c, 9); }
__declspec(dllexport) int __cdecl ucrt_xp_iswprint(wint_t c)  { return isw_classify_mask(c, 10); }
__declspec(dllexport) int __cdecl ucrt_xp_iswgraph(wint_t c)  { return isw_classify_mask(c, 11); }
__declspec(dllexport) int __cdecl ucrt_xp_iswblank(wint_t c)  { return isw_classify_mask(c, 12); }
__declspec(dllexport) int __cdecl ucrt_xp_iswascii(wint_t c)  { return c <= 0x7f; }

__declspec(dllexport) wint_t __cdecl ucrt_xp_towupper(wint_t c)
{
    if (c <= 255) return (wint_t)ucrt_xp_toupper((int)c);
    {
        wchar_t ch = (wchar_t)c;
        CharUpperW(&ch);
        return (wint_t)ch;
    }
}

__declspec(dllexport) wint_t __cdecl ucrt_xp_towlower(wint_t c)
{
    if (c <= 255) return (wint_t)ucrt_xp_tolower((int)c);
    {
        wchar_t ch = (wchar_t)c;
        CharLowerW(&ch);
        return (wint_t)ch;
    }
}

__declspec(dllexport) int __cdecl ucrt_xp_iswcsymf(wint_t c)
{
    return ucrt_xp_iswalpha(c) || c == L'_';
}

__declspec(dllexport) int __cdecl ucrt_xp_iswcsym(wint_t c)
{
    return ucrt_xp_iswalnum(c) || c == L'_';
}

/* ------------------------------------------------------------------ */
/* wctype / iswctype (bitmask classifiers)                             */
/* ------------------------------------------------------------------ */

/* Bit flags matching common MSVC <wctype.h> layout closely enough for
 * portable checks (values are stable for this library). */
#define UCRT_XP_WCT_ALNUM  0x0001
#define UCRT_XP_WCT_ALPHA  0x0002
#define UCRT_XP_WCT_CNTRL  0x0004
#define UCRT_XP_WCT_DIGIT  0x0008
#define UCRT_XP_WCT_GRAPH  0x0010
#define UCRT_XP_WCT_LOWER  0x0020
#define UCRT_XP_WCT_PRINT  0x0040
#define UCRT_XP_WCT_PUNCT  0x0080
#define UCRT_XP_WCT_SPACE  0x0100
#define UCRT_XP_WCT_UPPER  0x0200
#define UCRT_XP_WCT_XDIGIT 0x0400

typedef unsigned short ucrt_xp_wctype_t;

__declspec(dllexport) ucrt_xp_wctype_t __cdecl ucrt_xp_wctype(const char *property)
{
    if (!property) return 0;
    if (ucrt_xp_strcmp(property, "alnum")  == 0) return UCRT_XP_WCT_ALNUM;
    if (ucrt_xp_strcmp(property, "alpha")  == 0) return UCRT_XP_WCT_ALPHA;
    if (ucrt_xp_strcmp(property, "cntrl")  == 0) return UCRT_XP_WCT_CNTRL;
    if (ucrt_xp_strcmp(property, "digit")  == 0) return UCRT_XP_WCT_DIGIT;
    if (ucrt_xp_strcmp(property, "graph")  == 0) return UCRT_XP_WCT_GRAPH;
    if (ucrt_xp_strcmp(property, "lower")  == 0) return UCRT_XP_WCT_LOWER;
    if (ucrt_xp_strcmp(property, "print")  == 0) return UCRT_XP_WCT_PRINT;
    if (ucrt_xp_strcmp(property, "punct")  == 0) return UCRT_XP_WCT_PUNCT;
    if (ucrt_xp_strcmp(property, "space")  == 0) return UCRT_XP_WCT_SPACE;
    if (ucrt_xp_strcmp(property, "upper")  == 0) return UCRT_XP_WCT_UPPER;
    if (ucrt_xp_strcmp(property, "xdigit") == 0) return UCRT_XP_WCT_XDIGIT;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_iswctype(wint_t c, ucrt_xp_wctype_t desc)
{
    unsigned short m = 0;
    if (ucrt_xp_iswalnum(c))  m |= UCRT_XP_WCT_ALNUM;
    if (ucrt_xp_iswalpha(c))  m |= UCRT_XP_WCT_ALPHA;
    if (ucrt_xp_iswcntrl(c))  m |= UCRT_XP_WCT_CNTRL;
    if (ucrt_xp_iswdigit(c))  m |= UCRT_XP_WCT_DIGIT;
    if (ucrt_xp_iswgraph(c))  m |= UCRT_XP_WCT_GRAPH;
    if (ucrt_xp_iswlower(c))  m |= UCRT_XP_WCT_LOWER;
    if (ucrt_xp_iswprint(c))  m |= UCRT_XP_WCT_PRINT;
    if (ucrt_xp_iswpunct(c))  m |= UCRT_XP_WCT_PUNCT;
    if (ucrt_xp_iswspace(c))  m |= UCRT_XP_WCT_SPACE;
    if (ucrt_xp_iswupper(c))  m |= UCRT_XP_WCT_UPPER;
    if (ucrt_xp_iswxdigit(c)) m |= UCRT_XP_WCT_XDIGIT;
    return (m & desc) != 0;
}
