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
