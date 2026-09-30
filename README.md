# ucrt_xp — Universal CRT for Windows XP

One ABI-stable C runtime for Windows XP (SP2/SP3, x86): any module, any
MSVC version, same `ucrt_xp.dll`.

## Why

Classic CRT state (heap, locale, errno, FILE*, sync) differs across VC
versions and cannot be mixed safely in one process. Vista+ APIs
(`CONDITION_VARIABLE`, `InitOnceExecuteOnce`, …) do not exist on XP.
ucrt_xp centralizes those pieces behind a version-checked, XP-safe ABI.

## Quick start

```c
#define UCRT_XP_USE_STD_NAMES
#include "ucrt_xp_compat.h"

int main(void) {
    ucrt_xp_init(UCRT_XP_ABI_VERSION);  /* or UCRT_XP_IMPLEMENT_AUTOSTART */
    printf("hello %d\n", 42);
    return 0;
}
```

```cmake
add_subdirectory(ucrt_xp)          # or link the prebuilt DLL
target_link_libraries(app ucrt_xp) # or ucrt_xp_static
```

Deploy `ucrt_xp.dll` next to the EXE (app-local). Static link is also
supported via `ucrt_xp_static`.

## Coverage (high level)

| Area | Notes |
|------|--------|
| Heap | `malloc`/`free`, LFH + per-thread small-object cache |
| Locale | Thread-local, refcounted, immutable; `strcoll`/`strxfrm`/`_stricoll` |
| Sync | Vista-style `CONDITION_VARIABLE` / `InitOnce*` (native on Vista+, fallback on XP) |
| stdio | fd table + buffered `FILE*`, text CRLF, stdin/stdout/stderr |
| printf / scanf | Integer, float, string, wide variants |
| string / mem | Full `str*`/`mem*` + MSVC extras (`_strlwr`, `_memicmp`, …) |
| wide | `wcs*`, `_wopen`/`_wfopen`, wide printf/scanf, wide fs/process |
| ctype / wctype | Standard + `iscsym`, `isw*`, `tow*` |
| convert | `strtol`/`strtoll`/`strtod`/`strtof`, `_itoa`, `rand`/`rand_s` |
| process | `getenv`/`_putenv`, `_pipe`/`_popen`, spawn/exec, `_beginthread*` |
| time | 64-bit `time_t`, reentrant `localtime`/`gmtime`, `strftime`, `asctime` |
| fs | `_access`/`_stat`/`_findfirst`, `_fullpath`/`_splitpath`, wide path helpers |
| fd | `_dup`/`_dup2`/`_setmode`/`_get_osfhandle`/`_eof`/`_lseeki64` |
| locale names | `setlocale` / `localeconv` |
| multibyte | `mblen`/`mbtowc`/`mbstowcs`/`wcstombs` |
| exception | SEH trampoline + `ucrt_xp_guarded_call` |
| assert | `assert` macro → `ucrt_xp_assert` |
| secure CRT | `strcpy_s`/`memcpy_s`/`sprintf_s`/`fopen_s`/`localtime_s`/… |

**Not covered (by design):** `<math.h>` transcendentals (use the compiler’s),
`setjmp`/`longjmp` (compiler intrinsics). Secure CRT `*_s` is included (string/mem/printf/file/time/mb).

## Design rules that matter

1. **ABI negotiation** — call `ucrt_xp_init(UCRT_XP_ABI_VERSION)` once.
   Mismatch → diagnostic and hard exit (no silent partial compat).
2. **Append-only ABI** — never change existing signatures or struct
   layouts; only append; bump `UCRT_XP_ABI_MINOR` (or `MAJOR` + new DLL
   name for breaks). Ordinals in `ucrt_xp.def` are never reused.
3. **No host CRT stdio/heap** — implementations talk to Win32 directly so
   behavior does not depend on which `msvcrXX.dll` a module linked.
4. **Thread-local where it counts** — errno, locale, `rand` state;
   `_beginthread*` runs CRT attach/detach (errno, locale, heap cache).
5. **XP-only APIs** — `_WIN32_WINNT=0x0501`; no Vista+ imports at link time.
   Vista-shaped sync is resolved at runtime when present.

## Layout

```
include/ucrt_xp.h         Public C ABI
include/ucrt_xp_compat.h  Optional `#define UCRT_XP_USE_STD_NAMES` macros
include/ucrt_xp.hpp       Optional C++ RAII helpers
src/                      Implementation + ucrt_xp.def
tests/                    test_basic, test_sync
examples/                 hello.c / hello.cpp
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Targets XP subsystem (`5.01`) when built with MSVC.

## ABI checklist (before changing `ucrt_xp.h`)

1. Do not change existing function signatures or struct layouts.
2. Only append; bump `UCRT_XP_ABI_MINOR`.
3. Breaking change → bump `UCRT_XP_ABI_MAJOR` and ship a new DLL name.
4. Every new export gets the next ordinal in `ucrt_xp.def`.

Current ABI: **1.10** (`UCRT_XP_ABI_MAJOR.MINOR`).
