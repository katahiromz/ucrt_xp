/*
 * hello.c - the "easy mode" quickstart: autostart (no manual init call)
 * plus the compat header's familiar CRT names.
 *
 * UCRT_XP_IMPLEMENT_AUTOSTART must be defined in exactly ONE source file
 * per program (this one) - see the comment above it in ucrt_xp.h.
 */
#define UCRT_XP_IMPLEMENT_AUTOSTART
#define UCRT_XP_USE_STD_NAMES
#include "ucrt_xp_compat.h"

int main(void)
{
    /* No ucrt_xp_init() call anywhere - autostart already ran it. */
    char *greeting = (char *)malloc(64);
    UCRT_XP_FILE *f;

    lstrcpynA(greeting, "hello from ucrt_xp", 64);

    printf("stdout works too: %s\n", greeting);

    f = fopen("ucrt_xp_hello.txt", "w");
    if (f) {
        fprintf(f, "%s (pid-ish tick=%lu)\n", greeting, GetTickCount());
        fclose(f);
    }

    free(greeting);
    return 0;
}
