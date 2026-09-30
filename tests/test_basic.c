#include "ucrt_xp.h"
#include <stdio.h>
#include <assert.h>

static UCRT_XP_ONCE g_once = UCRT_XP_ONCE_INIT;
static int g_once_calls = 0;

static BOOL __cdecl once_body(void *param)
{
    (void)param;
    g_once_calls++;
    return TRUE;
}

static void test_once(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        ucrt_xp_once(&g_once, once_body, NULL);
    }
    assert(g_once_calls == 1);
    printf("[OK] ucrt_xp_once fires exactly once across %d calls\n", i);
}

static void test_heap(void)
{
    char *p1 = (char *)ucrt_xp_malloc(10);
    char *p2 = (char *)ucrt_xp_malloc(300); /* bypasses small-object cache */
    assert(p1 && p2);
    lstrcpynA(p1, "hello", 10);
    assert(lstrcmpA(p1, "hello") == 0);

    p1 = (char *)ucrt_xp_realloc(p1, 64);
    assert(p1 && lstrcmpA(p1, "hello") == 0);

    ucrt_xp_free(p1);
    ucrt_xp_free(p2);
    printf("[OK] malloc/realloc/free basic round-trip\n");
}

static void test_locale(void)
{
    ucrt_xp_locale_t loc = ucrt_xp_locale_get_thread();
    UCRT_XP_LCONV lc;
    assert(loc != NULL);
    assert(ucrt_xp_stricmp_l("ABC", "abc", loc) == 0);
    assert(ucrt_xp_stricmp_l("abc", "abd", loc) < 0);
    /* Non-locale _stricmp equivalent uses the thread-default locale. */
    assert(ucrt_xp_stricmp("ABC", "abc") == 0);
    assert(ucrt_xp_stricmp("abc", "abd") < 0);
    assert(ucrt_xp_toupper_l('a', loc) == 'A');
    assert(ucrt_xp_tolower_l('Z', loc) == 'z');
    assert(ucrt_xp_locale_get_lconv(loc, &lc));
    printf("[OK] thread-default locale + stricmp/stricmp_l/toupper_l/lconv (decimal='%s')\n",
           lc.decimal_point);
}

typedef struct { CRITICAL_SECTION lock; UCRT_XP_COND cv; int ready; } SharedState;

static DWORD WINAPI producer_thread(LPVOID param)
{
    SharedState *s = (SharedState *)param;
    Sleep(50);
    EnterCriticalSection(&s->lock);
    s->ready = 1;
    LeaveCriticalSection(&s->lock);
    ucrt_xp_cond_signal(&s->cv);
    return 0;
}

static void test_condvar(void)
{
    SharedState s;
    HANDLE th;
    InitializeCriticalSection(&s.lock);
    ucrt_xp_cond_init(&s.cv);
    s.ready = 0;

    th = CreateThread(NULL, 0, producer_thread, &s, 0, NULL);
    assert(th != NULL);

    EnterCriticalSection(&s.lock);
    while (!s.ready) {
        ucrt_xp_cond_wait(&s.cv, &s.lock, INFINITE);
    }
    LeaveCriticalSection(&s.lock);

    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    ucrt_xp_cond_destroy(&s.cv);
    DeleteCriticalSection(&s.lock);
    printf("[OK] condvar producer/consumer handshake\n");
}

static int __cdecl crash_fn(void *param)
{
    volatile int *p = (volatile int *)param;
    *p = 42; /* param is NULL when called from test_guarded_call() */
    return 0;
}

static int __cdecl ok_fn(void *param)
{
    (void)param;
    return 7;
}

static void test_guarded_call(void)
{
    int result = -1;
    UCRT_XP_EXCEPTION_INFO info;
    BOOL ok;

    ok = ucrt_xp_guarded_call(ok_fn, NULL, &result, NULL);
    assert(ok && result == 7);

    ZeroMemory(&info, sizeof(info));
    ok = ucrt_xp_guarded_call(crash_fn, NULL, &result, &info);
    assert(!ok);
    assert(info.code == EXCEPTION_ACCESS_VIOLATION);
    printf("[OK] guarded_call: normal path returns 7, faulting path trapped 0x%08lX\n",
           (unsigned long)info.code);
}

static void test_stdio(void)
{
    const char *path = "ucrt_xp_test_tmp.txt";
    const char *path2 = "ucrt_xp_test_tmp2.txt";
    UCRT_XP_FILE *f;
    char line[64];
    size_t n;
    int c;

    f = ucrt_xp_fopen(path, "w");
    assert(f != NULL);
    ucrt_xp_fprintf(f, "line1\nline2\n");
    assert(ucrt_xp_fputs("line3\n", f) == 0);
    assert(ucrt_xp_fileno(f) >= 0);
    ucrt_xp_fclose(f);

    f = ucrt_xp_fopen(path, "r");
    assert(f != NULL);
    n = ucrt_xp_fread(line, 1, sizeof(line) - 1, f);
    line[n] = 0;
    assert(lstrcmpA(line, "line1\r\nline2\r\n") != 0); /* text mode strips CR on read */
    assert(lstrcmpA(line, "line1\nline2\nline3\n") == 0);

    ucrt_xp_rewind(f);
    assert(ucrt_xp_fgets(line, (int)sizeof(line), f) != NULL);
    assert(lstrcmpA(line, "line1\n") == 0);
    assert(ucrt_xp_fgets(line, (int)sizeof(line), f) != NULL);
    assert(lstrcmpA(line, "line2\n") == 0);

    /* ungetc: push back one char and re-read it */
    c = ucrt_xp_fgetc(f);
    assert(c == 'l');
    assert(ucrt_xp_ungetc(c, f) == 'l');
    assert(ucrt_xp_fgetc(f) == 'l');

    ucrt_xp_clearerr(f);
    assert(ucrt_xp_ferror(f) == 0);
    ucrt_xp_fclose(f);

    assert(ucrt_xp_rename(path, path2) == 0);
    assert(ucrt_xp_remove(path2) == 0);
    assert(ucrt_xp_remove(path) != 0); /* already renamed away */

    printf("[OK] fopen/fprintf/fputs/fgets/ungetc/rewind/clearerr/fileno/remove/rename\n");
}

static void test_file_magic_detection(void)
{
    const char *path = "ucrt_xp_test_magic.txt";
    UCRT_XP_FILE *f = ucrt_xp_fopen(path, "w");
    char dummy[4];
    assert(f != NULL);

    ucrt_xp_fclose(f);
    /* f is now a dangling pointer - fclose() poisoned the magic field
     * before freeing. Every call below must fail safely (return an
     * error value) rather than crash or silently succeed on freed
     * memory. This is exactly the misuse-detection property added in
     * response to the "embed a magic number" design discussion. */
    assert(ucrt_xp_fread(dummy, 1, 1, f) == 0);
    assert(ucrt_xp_fwrite("x", 1, 1, f) == 0);
    assert(ucrt_xp_fflush(f) == -1);
    assert(ucrt_xp_fseek(f, 0, UCRT_XP_SEEK_SET) == -1);
    assert(ucrt_xp_ftell(f) == -1);
    assert(ucrt_xp_feof(f) == 0);
    assert(ucrt_xp_ferror(f) == 0);
    assert(ucrt_xp_fclose(f) == -1); /* double-close also caught */

    /* An arbitrary non-FILE pointer must also be rejected, not just a
     * freed one - this is the "garbage/wrong pointer" case, distinct
     * from use-after-free. */
    {
        int not_a_file = 12345;
        assert(ucrt_xp_fread(dummy, 1, 1, (UCRT_XP_FILE *)&not_a_file) == 0);
    }

    DeleteFileA(path);
    printf("[OK] UCRT_XP_FILE magic detection: use-after-fclose and "
           "garbage pointers are rejected, not crashed on\n");
}

static void test_format(void)
{
    char buf[128];
    int n;

    n = ucrt_xp_snprintf(buf, sizeof(buf), "%d/%u/%x/%s/%c", -42, 42u, 255, "hi", 'Z');
    assert(n > 0);
    assert(lstrcmpA(buf, "-42/42/ff/hi/Z") == 0);

    n = ucrt_xp_sprintf(buf, "%d:%s", 7, "ok");
    assert(n > 0);
    assert(lstrcmpA(buf, "7:ok") == 0);

    ucrt_xp_snprintf(buf, sizeof(buf), "%05d|%-5d|%+d", 7, 7, 7);
    assert(lstrcmpA(buf, "00007|7    |+7") == 0);

    ucrt_xp_snprintf(buf, sizeof(buf), "%.2f", 3.14159);
    assert(lstrcmpA(buf, "3.14") == 0);

    {
        int a = 0, b = 0;
        char s[32];
        float f = 0;
        n = ucrt_xp_sscanf("42 hello 3.5", "%d %31s %f", &a, s, &f);
        assert(n == 3);
        assert(a == 42);
        assert(lstrcmpA(s, "hello") == 0);
        assert(f > 3.4f && f < 3.6f);
        n = ucrt_xp_sscanf("0x2A", "%i", &a);
        assert(n == 1 && a == 42);
    }

    /* scansets and %p */
    {
        char s[32];
        void *ptr = NULL;
        n = ucrt_xp_sscanf("abc123", "%[a-z]%[0-9]", s, s + 16);
        assert(n == 2);
        assert(lstrcmpA(s, "abc") == 0);
        assert(lstrcmpA(s + 16, "123") == 0);
        n = ucrt_xp_sscanf("hello world", "%[^ ]", s);
        assert(n == 1 && lstrcmpA(s, "hello") == 0);
        n = ucrt_xp_sscanf("0xDEAD", "%p", &ptr);
        assert(n == 1);
        assert((size_t)ptr == 0xDEADu);
    }

    printf("[OK] snprintf/sprintf/sscanf: ints/strings/padding/float/scanset/%%p all correct\n");
}

static void test_wide(void)
{
    wchar_t w1[32];
    char a1[32];
    UCRT_XP_FILE *f;
    const wchar_t *path = L"ucrt_xp_test_wide.txt";

    assert(ucrt_xp_wcslen(L"hello") == 5);
    assert(ucrt_xp_wcsnlen(L"hello", 3) == 3);
    assert(ucrt_xp_wcsnlen(L"hi", 10) == 2);
    assert(ucrt_xp_wcscmp(L"abc", L"abc") == 0);
    assert(ucrt_xp_wcscmp(L"abc", L"abd") < 0);
    assert(ucrt_xp_wcsncmp(L"abcX", L"abcY", 3) == 0);
    ucrt_xp_wcscpy(w1, L"round-trip");
    assert(ucrt_xp_wcscmp(w1, L"round-trip") == 0);
    ucrt_xp_wcsncpy(w1, L"xy", 32);
    assert(ucrt_xp_wcscmp(w1, L"xy") == 0);
    ucrt_xp_wcscat(w1, L"z");
    assert(ucrt_xp_wcscmp(w1, L"xyz") == 0);

    /* Non-locale _wcsicmp equivalent. */
    assert(ucrt_xp_wcsicmp(L"ABC", L"abc") == 0);
    assert(ucrt_xp_wcsicmp(L"abc", L"abd") < 0);

    ucrt_xp_ansi_to_wide("test", w1, 32);
    assert(ucrt_xp_wcscmp(w1, L"test") == 0);
    ucrt_xp_wide_to_ansi(L"test", a1, 32);
    assert(lstrcmpA(a1, "test") == 0);

    f = ucrt_xp_wfopen(path, L"w");
    assert(f != NULL);
    ucrt_xp_fwrite("hi", 1, 2, f);
    ucrt_xp_fclose(f);
    DeleteFileW(path);

    printf("[OK] wcs*/wcsn*/wcsicmp + ansi<->wide conversion + wfopen (_wfopen)\n");
}

static void test_wide_printf(void)
{
    wchar_t buf[128];
    int n;
    UCRT_XP_FILE *f;
    char check[64];
    size_t got;

    n = ucrt_xp_swprintf(buf, 128, L"%d/%u/%x/%ls/%c", -42, 42u, 255, L"hi", L'Z');
    assert(n > 0);
    assert(ucrt_xp_wcscmp(buf, L"-42/42/ff/hi/Z") == 0);

    ucrt_xp_swprintf(buf, 128, L"%05d|%.2f", 7, 3.14159);
    assert(ucrt_xp_wcscmp(buf, L"00007|3.14") == 0);

    f = ucrt_xp_fopen("ucrt_xp_test_wprintf.txt", "w");
    assert(f != NULL);
    ucrt_xp_fwprintf(f, L"wide:%d", 99);
    ucrt_xp_fclose(f);

    f = ucrt_xp_fopen("ucrt_xp_test_wprintf.txt", "r");
    got = ucrt_xp_fread(check, 1, sizeof(check) - 1, f);
    check[got] = 0;
    assert(lstrcmpA(check, "wide:99") == 0);
    ucrt_xp_fclose(f);
    DeleteFileA("ucrt_xp_test_wprintf.txt");

    printf("[OK] ucrt_xp_swprintf/fwprintf: formatting + ANSI-narrowed file output correct\n");
}

static void test_wide_scanf(void)
{
    int a = 0;
    wchar_t ws[32];
    char s[32];
    int n;

    n = ucrt_xp_swscanf(L"42 hello", L"%d %31ls", &a, ws);
    assert(n == 2);
    assert(a == 42);
    assert(ucrt_xp_wcscmp(ws, L"hello") == 0);

    n = ucrt_xp_swscanf(L"abc123", L"%[a-z]%[0-9]", s, s + 16);
    assert(n == 2);
    assert(lstrcmpA(s, "abc") == 0);
    assert(lstrcmpA(s + 16, "123") == 0);

    n = ucrt_xp_swscanf(L"0xFF", L"%i", &a);
    assert(n == 1 && a == 255);

    printf("[OK] wide scanf: swscanf ints/strings/scansets correct\n");
}

static void test_string(void)
{
    char buf[64];
    char *tok, *save;

    assert(ucrt_xp_strlen("hello") == 5);
    ucrt_xp_strcpy(buf, "hello");
    assert(ucrt_xp_strcmp(buf, "hello") == 0);
    ucrt_xp_strcat(buf, " world");
    assert(ucrt_xp_strcmp(buf, "hello world") == 0);
    assert(ucrt_xp_strncmp("abcX", "abcY", 3) == 0);
    assert(ucrt_xp_strnicmp("ABC", "abd", 2) == 0);
    assert(ucrt_xp_strnicmp("abc", "abd", 3) < 0);
    assert(ucrt_xp_strchr("hello", 'l') != NULL);
    assert(ucrt_xp_strstr("hello world", "wor") != NULL);
    {
        char *dup = ucrt_xp_strdup("dup-me");
        assert(dup && ucrt_xp_strcmp(dup, "dup-me") == 0);
        ucrt_xp_free(dup);
    }

    ucrt_xp_strcpy(buf, "a,b,,c");
    tok = ucrt_xp_strtok_r(buf, ",", &save);
    assert(tok && ucrt_xp_strcmp(tok, "a") == 0);
    tok = ucrt_xp_strtok_r(NULL, ",", &save);
    assert(tok && ucrt_xp_strcmp(tok, "b") == 0);
    tok = ucrt_xp_strtok_r(NULL, ",", &save);
    assert(tok && ucrt_xp_strcmp(tok, "c") == 0);
    tok = ucrt_xp_strtok_r(NULL, ",", &save);
    assert(tok == NULL);

    {
        char a[4] = {1,2,3,4};
        char b[4] = {1,2,3,4};
        assert(ucrt_xp_memcmp(a, b, 4) == 0);
        ucrt_xp_memset(a, 0, 4);
        assert(a[0] == 0 && a[3] == 0);
    }

    printf("[OK] string.c: strlen/cpy/cat/cmp/strnicmp/chr/str/dup/strtok_r/mem* all correct\n");
}

static void test_ctype(void)
{
    assert(ucrt_xp_isalpha('a') && ucrt_xp_isalpha('Z'));
    assert(!ucrt_xp_isalpha('5'));
    assert(ucrt_xp_isdigit('5') && !ucrt_xp_isdigit('a'));
    assert(ucrt_xp_isspace(' ') && ucrt_xp_isspace('\t'));
    assert(ucrt_xp_toupper('a') == 'A');
    assert(ucrt_xp_tolower('Z') == 'z');
    assert(ucrt_xp_isxdigit('f') && !ucrt_xp_isxdigit('g'));
    printf("[OK] ctype.c: is*/toupper/tolower correct\n");
}

static int __cdecl int_cmp(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static void test_convert(void)
{
    char *end;
    int arr[6] = {5, 3, 1, 4, 1, 2};
    int key = 4;
    int *found;

    assert(ucrt_xp_atoi("42") == 42);
    assert(ucrt_xp_atoi("-17") == -17);
    assert(ucrt_xp_strtol("  0x2A", &end, 0) == 42);
    assert(ucrt_xp_strtoul("101", NULL, 2) == 5);
    {
        double d = ucrt_xp_atof("3.14159");
        assert(d > 3.14 && d < 3.15);
    }
    {
        double d = ucrt_xp_strtod("1.5e3", NULL);
        assert(d > 1499.0 && d < 1501.0);
    }
    assert(ucrt_xp_abs(-5) == 5);

    ucrt_xp_srand(12345);
    {
        int r1 = ucrt_xp_rand();
        int r2 = ucrt_xp_rand();
        assert(r1 != r2 || r1 >= 0); /* just exercise it, don't over-assert PRNG props */
    }

    ucrt_xp_qsort(arr, 6, sizeof(int), int_cmp);
    assert(arr[0] == 1 && arr[5] == 5);

    found = (int *)ucrt_xp_bsearch(&key, arr, 6, sizeof(int), int_cmp);
    assert(found && *found == 4);

    {
        char ibuf[32];
        assert(ucrt_xp_itoa(-42, ibuf, 10) && lstrcmpA(ibuf, "-42") == 0);
        assert(ucrt_xp_itoa(255, ibuf, 16) && lstrcmpA(ibuf, "ff") == 0);
        assert(ucrt_xp_ultoa(10UL, ibuf, 2) && lstrcmpA(ibuf, "1010") == 0);
    }

    printf("[OK] convert.c: atoi/strtol/strtod/itoa/rand/qsort/bsearch correct\n");
}

static void test_process(void)
{
    char *env;
    ucrt_xp_set_errno(42);
    assert(ucrt_xp_get_errno() == 42);
    assert(*ucrt_xp_errno_location() == 42);
    assert(ucrt_xp_strerror(2) != NULL);
    env = ucrt_xp_getenv("PATH");
    assert(env != NULL && env[0] != 0); /* PATH almost always exists on Windows */
    printf("[OK] process.c: errno/strerror/getenv correct\n");
}

static void test_time(void)
{
    UCRT_XP_TIME_T now;
    UCRT_XP_TM tm;
    char buf[64];

    now = ucrt_xp_time(NULL);
    assert(now > 1700000000LL); /* sometime after Nov 2023 - sanity floor */

    assert(ucrt_xp_gmtime(&now, &tm));
    assert(tm.tm_year + 1900 >= 2024);

    ucrt_xp_strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    assert(ucrt_xp_strlen(buf) == 19); /* "YYYY-MM-DD HH:MM:SS" */

    {
        char buf2[128];
        size_t n;
        UCRT_XP_TM fixed;
        fixed.tm_year = 125; /* 2025 */
        fixed.tm_mon  = 0;   /* Jan */
        fixed.tm_mday = 1;
        fixed.tm_hour = 15;
        fixed.tm_min  = 30;
        fixed.tm_sec  = 45;
        fixed.tm_wday = 3;   /* Wednesday */
        fixed.tm_yday = 0;
        fixed.tm_isdst = 0;
        n = ucrt_xp_strftime(buf2, sizeof(buf2), "%a %A %b %B %p %I", &fixed);
        assert(n > 0);
        assert(ucrt_xp_strstr(buf2, "Wed") != NULL);
        assert(ucrt_xp_strstr(buf2, "Wednesday") != NULL);
        assert(ucrt_xp_strstr(buf2, "Jan") != NULL);
        assert(ucrt_xp_strstr(buf2, "January") != NULL);
        assert(ucrt_xp_strstr(buf2, "PM") != NULL);
        n = ucrt_xp_strftime(buf2, sizeof(buf2), "%c", &fixed);
        assert(n > 0 && ucrt_xp_strstr(buf2, "Wed") != NULL);
    }

    printf("[OK] time.c: time/gmtime/strftime (incl. day/month names) correct (%s UTC)\n", buf);
}

static void test_std_streams(void)
{
    UCRT_XP_FILE *out = ucrt_xp_stdout();
    /* In this sandbox's build (no console attached when run under a
     * test harness / redirected output), GetStdHandle can legitimately
     * return NULL - that's not a bug, see stdio.c's comment. Only
     * assert the shape of the API (repeated calls return the same
     * cached pointer), not that a console is actually present. */
    UCRT_XP_FILE *out2 = ucrt_xp_stdout();
    assert(out == out2); /* lazy singleton, not re-constructed each call */

    if (out) {
        int n = ucrt_xp_printf("stdout smoke test: %d\n", 123);
        assert(n > 0);
        ucrt_xp_puts("puts() smoke test");
        ucrt_xp_putchar('X');
        ucrt_xp_putchar('\n');
    }

    printf("[OK] stdin/stdout/stderr accessors are stable singletons; "
           "printf/puts/putchar %s\n", out ? "wrote to a real console" : "skipped (no console attached)");
}

int main(void)
{
    ucrt_xp_heap_init();
    test_once();
    test_heap();
    test_locale();
    test_condvar();
    test_stdio();
    test_file_magic_detection();
    test_guarded_call();
    test_format();
    test_wide();
    test_wide_printf();
    test_wide_scanf();
    test_string();
    test_ctype();
    test_convert();
    test_process();
    test_time();
    test_std_streams();
    printf("All ucrt_xp smoke tests passed.\n");
    return 0;
}
