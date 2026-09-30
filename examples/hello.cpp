/*
 * hello.cpp - the RAII quickstart: File/Lock/Guard/CondVar/Locale
 * clean up automatically, no manual fclose/DeleteCriticalSection calls.
 */
#include "ucrt_xp.hpp"

static void write_greeting()
{
    ucrt_xp::File f("ucrt_xp_hello_cpp.txt", "w");
    if (f.is_open()) {
        ucrt_xp_fprintf(f.handle(), "hello from ucrt_xp C++ wrapper\n");
    }
    /* f.close() happens automatically here, even on an early return. */
}

static void locked_counter_demo()
{
    ucrt_xp::Lock lock;
    int counter = 0;
    {
        ucrt_xp::Guard guard(lock); /* acquires */
        counter++;
    } /* released automatically */
    (void)counter;
}

int main()
{
    ucrt_xp_init(UCRT_XP_ABI_VERSION); /* explicit form, no autostart here */

    write_greeting();
    locked_counter_demo();

    ucrt_xp::Locale loc("C");
    loc.make_current();
    int cmp = loc.stricmp("ABC", "abc"); /* 0 */

    return cmp;
}
