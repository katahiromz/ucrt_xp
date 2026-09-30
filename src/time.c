/*
 * time.c - <time.h>'s core surface, with two deliberate improvements
 * over the classic CRT rather than a literal reproduction of its bugs:
 *
 *   1. UCRT_XP_TIME_T is a 64-bit signed integer, not the classic CRT's
 *      32-bit time_t (which overflows in January 2038 - a real problem
 *      for any XP-era system still ticking in production past that
 *      date). This is a case where "ABI-stable" and "byte-identical to
 *      the old CRT" are different goals, and this project picks the
 *      former deliberately over the latter.
 *   2. ucrt_xp_localtime/gmtime fill a caller-supplied UCRT_XP_TM, not a
 *      shared static buffer - the classic CRT's localtime()/gmtime()
 *      returning a pointer to internal static storage is a well-known
 *      thread-safety trap (two threads calling it "simultaneously" get
 *      each other's results). The _r-suffixed POSIX convention (an
 *      out-parameter) is used here instead.
 */
#include "internal.h"

/* Windows FILETIME epoch (1601-01-01) to Unix epoch (1970-01-01) offset,
 * in 100-nanosecond units - the standard constant for this conversion. */
#define UCRT_XP_EPOCH_DIFF_100NS 116444736000000000LL

UCRT_XP_API __int64 __cdecl ucrt_xp_time(__int64 *out)
{
    FILETIME ft;
    ULARGE_INTEGER uli;
    __int64 t;

    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    t = ((__int64)uli.QuadPart - UCRT_XP_EPOCH_DIFF_100NS) / 10000000LL;
    if (out) *out = t;
    return t;
}

UCRT_XP_API unsigned long __cdecl ucrt_xp_clock(void)
{
    /* GetTickCount() wraps at ~49.7 days, same practical caveat as the
     * classic CRT clock()'s own documented wraparound behavior - not a
     * regression introduced here. Units: milliseconds, matching
     * CLOCKS_PER_SEC == 1000 (true for the classic Microsoft CRT, unlike
     * POSIX's 1,000,000 - a frequent porting gotcha this matches on
     * purpose for XP/MSVC-ported-code compatibility). */
    return (unsigned long)GetTickCount();
}

static void filetime_to_tm(const SYSTEMTIME *st, UCRT_XP_TM *out)
{
    out->tm_year = st->wYear - 1900;
    out->tm_mon  = st->wMonth - 1;
    out->tm_mday = st->wDay;
    out->tm_hour = st->wHour;
    out->tm_min  = st->wMinute;
    out->tm_sec  = st->wSecond;
    out->tm_wday = st->wDayOfWeek;
    out->tm_yday = 0;   /* not computed - most callers use gmtime/localtime
                          * for wall-clock display, not day-of-year math;
                          * see ucrt_xp_compute_yday below for the field
                          * a caller who does need it can call directly */
    out->tm_isdst = -1; /* "unknown" - matches the standard's own
                          * documented sentinel for "not determined" */
}

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

UCRT_XP_API int __cdecl ucrt_xp_compute_yday(int year, int mon0, int mday)
{
    static const int cum_days[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int yday = cum_days[mon0] + (mday - 1);
    if (mon0 >= 2 && is_leap_year(year)) yday++;
    return yday;
}

UCRT_XP_API BOOL __cdecl ucrt_xp_gmtime(const __int64 *timer, UCRT_XP_TM *out)
{
    FILETIME ft;
    SYSTEMTIME st;
    ULARGE_INTEGER uli;
    __int64 t100ns;

    if (!timer || !out) return FALSE;

    t100ns = (*timer) * 10000000LL + UCRT_XP_EPOCH_DIFF_100NS;
    uli.QuadPart = (ULONGLONG)t100ns;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;

    if (!FileTimeToSystemTime(&ft, &st)) return FALSE;

    filetime_to_tm(&st, out);
    out->tm_yday = ucrt_xp_compute_yday(st.wYear, out->tm_mon, st.wDay);
    return TRUE;
}

UCRT_XP_API BOOL __cdecl ucrt_xp_localtime(const __int64 *timer, UCRT_XP_TM *out)
{
    FILETIME ft, local_ft;
    SYSTEMTIME st_utc, st_local;
    ULARGE_INTEGER uli;
    __int64 t100ns;

    if (!timer || !out) return FALSE;

    t100ns = (*timer) * 10000000LL + UCRT_XP_EPOCH_DIFF_100NS;
    uli.QuadPart = (ULONGLONG)t100ns;
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;

    if (!FileTimeToSystemTime(&ft, &st_utc)) return FALSE;

    /* FileTimeToLocalFileTime applies the process's current time zone
     * (including DST) - this is the one function in this file that
     * genuinely depends on process-wide state (the OS time zone), which
     * is unavoidable: wall-clock local time IS a global concept on a
     * single machine, unlike the per-thread locale state elsewhere in
     * this project. */
    if (!FileTimeToLocalFileTime(&ft, &local_ft)) return FALSE;
    if (!FileTimeToSystemTime(&local_ft, &st_local)) return FALSE;

    filetime_to_tm(&st_local, out);
    out->tm_yday = ucrt_xp_compute_yday(st_local.wYear, out->tm_mon, st_local.wDay);
    (void)st_utc;
    return TRUE;
}

/* English C-locale day/month names. Full CLDR localization is out of
 * scope (see README); English matches the classic CRT "C" locale. */
static const char * const s_wday_abbr[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char * const s_wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};
static const char * const s_mon_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char * const s_mon_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static int week_num_U(const UCRT_XP_TM *tm)
{
    /* %U: Sunday-based week of year, 00-53. */
    return (tm->tm_yday + 7 - tm->tm_wday) / 7;
}

static int week_num_W(const UCRT_XP_TM *tm)
{
    /* %W: Monday-based week of year, 00-53. */
    int wday_mon = (tm->tm_wday == 0) ? 6 : (tm->tm_wday - 1);
    return (tm->tm_yday + 7 - wday_mon) / 7;
}

/*
 * ucrt_xp_strftime - practical C-locale subset: numeric fields, English
 * day/month names, %c/%x/%X composites, and %U/%W week numbers.
 */
UCRT_XP_API size_t __cdecl ucrt_xp_strftime(
    char *buf, size_t bufsize, const char *fmt, const UCRT_XP_TM *tm)
{
    char work[128];
    size_t total = 0;
    const char *p = fmt;

    if (!buf || bufsize == 0 || !fmt || !tm) return 0;

    while (*p) {
        int n;
        if (*p != '%') {
            if (total + 1 < bufsize) buf[total] = *p;
            total++;
            p++;
            continue;
        }
        p++;
        switch (*p) {
        case 'Y': n = ucrt_xp_snprintf(work, sizeof(work), "%d", tm->tm_year + 1900); break;
        case 'y': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", (tm->tm_year + 1900) % 100); break;
        case 'm': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", tm->tm_mon + 1); break;
        case 'd': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", tm->tm_mday); break;
        case 'H': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", tm->tm_hour); break;
        case 'M': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", tm->tm_min); break;
        case 'S': n = ucrt_xp_snprintf(work, sizeof(work), "%02d", tm->tm_sec); break;
        case 'j': n = ucrt_xp_snprintf(work, sizeof(work), "%03d", tm->tm_yday + 1); break;
        case 'w': n = ucrt_xp_snprintf(work, sizeof(work), "%d", tm->tm_wday); break;
        case 'a': {
            int wd = tm->tm_wday;
            if (wd < 0 || wd > 6) wd = 0;
            n = ucrt_xp_snprintf(work, sizeof(work), "%s", s_wday_abbr[wd]);
            break;
        }
        case 'A': {
            int wd = tm->tm_wday;
            if (wd < 0 || wd > 6) wd = 0;
            n = ucrt_xp_snprintf(work, sizeof(work), "%s", s_wday_full[wd]);
            break;
        }
        case 'b': case 'h': {
            int mo = tm->tm_mon;
            if (mo < 0 || mo > 11) mo = 0;
            n = ucrt_xp_snprintf(work, sizeof(work), "%s", s_mon_abbr[mo]);
            break;
        }
        case 'B': {
            int mo = tm->tm_mon;
            if (mo < 0 || mo > 11) mo = 0;
            n = ucrt_xp_snprintf(work, sizeof(work), "%s", s_mon_full[mo]);
            break;
        }
        case 'I': {
            int h = tm->tm_hour % 12;
            if (h == 0) h = 12;
            n = ucrt_xp_snprintf(work, sizeof(work), "%02d", h);
            break;
        }
        case 'p':
            n = ucrt_xp_snprintf(work, sizeof(work), "%s",
                                 (tm->tm_hour >= 12) ? "PM" : "AM");
            break;
        case 'U':
            n = ucrt_xp_snprintf(work, sizeof(work), "%02d", week_num_U(tm));
            break;
        case 'W':
            n = ucrt_xp_snprintf(work, sizeof(work), "%02d", week_num_W(tm));
            break;
        case 'x':
            n = ucrt_xp_snprintf(work, sizeof(work), "%02d/%02d/%02d",
                                 tm->tm_mon + 1, tm->tm_mday,
                                 (tm->tm_year + 1900) % 100);
            break;
        case 'X':
            n = ucrt_xp_snprintf(work, sizeof(work), "%02d:%02d:%02d",
                                 tm->tm_hour, tm->tm_min, tm->tm_sec);
            break;
        case 'c':
            {
                int wd = tm->tm_wday, mo = tm->tm_mon;
                if (wd < 0 || wd > 6) wd = 0;
                if (mo < 0 || mo > 11) mo = 0;
                n = ucrt_xp_snprintf(work, sizeof(work),
                    "%s %s %02d %02d:%02d:%02d %d",
                    s_wday_abbr[wd], s_mon_abbr[mo], tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec,
                    tm->tm_year + 1900);
            }
            break;
        case '%': work[0] = '%'; work[1] = 0; n = 1; break;
        case 0:   n = 0; break;
        default:  work[0] = '%'; work[1] = *p; work[2] = 0; n = 2; break;
        }

        {
            int i;
            for (i = 0; i < n; i++) {
                if (total + 1 < bufsize) buf[total] = work[i];
                total++;
            }
        }
        if (*p) p++;
        else break;
    }

    if (bufsize > 0) buf[(total < bufsize) ? total : bufsize - 1] = 0;
    return (total < bufsize) ? total : 0;
}

/* ------------------------------------------------------------------ */
/* asctime / ctime / _strdate / _strtime (classic CRT forms)           */
/* ------------------------------------------------------------------ */

/* Non-reentrant static buffers, matching classic CRT (thread-unsafe by
 * design of the original API). Prefer strftime + localtime for new code. */
UCRT_XP_API char* __cdecl ucrt_xp_asctime(const UCRT_XP_TM *tm)
{
    static char buf[32];
    static const char *wday[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mon[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};
    int y;

    if (!tm) return NULL;
    y = tm->tm_year + 1900;
    /* "Www Mmm dd hh:mm:ss yyyy\n" */
    wsprintfA(buf, "%s %s %02d %02d:%02d:%02d %04d\n",
              (tm->tm_wday >= 0 && tm->tm_wday < 7) ? wday[tm->tm_wday] : "???",
              (tm->tm_mon >= 0 && tm->tm_mon < 12) ? mon[tm->tm_mon] : "???",
              tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, y);
    return buf;
}

UCRT_XP_API char* __cdecl ucrt_xp_ctime(const UCRT_XP_TIME_T *timer)
{
    UCRT_XP_TM tm;
    if (!timer) return NULL;
    if (!ucrt_xp_localtime(timer, &tm)) return NULL;
    return ucrt_xp_asctime(&tm);
}

UCRT_XP_API char* __cdecl ucrt_xp_strdate(char *buf)
{
    SYSTEMTIME st;
    if (!buf) return NULL;
    GetLocalTime(&st);
    /* MM/DD/YY */
    wsprintfA(buf, "%02d/%02d/%02d", st.wMonth, st.wDay, st.wYear % 100);
    return buf;
}

UCRT_XP_API char* __cdecl ucrt_xp_strtime(char *buf)
{
    SYSTEMTIME st;
    if (!buf) return NULL;
    GetLocalTime(&st);
    /* HH:MM:SS */
    wsprintfA(buf, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}
