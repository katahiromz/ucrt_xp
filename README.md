# ucrt_xp — Universal CRT for Windows XP

A minimal "Universal CRT" implementation for Windows XP (SP2/SP3, x86),
built around one core idea: **any module, regardless of which MSVC
version compiled it, links against one ABI-stable runtime.**

This is a reference implementation of the design covering the highest-risk
pieces first — the parts that differ across VC versions or don't exist on
XP at all:

| Module | File | Replaces / provides |
|---|---|---|
| ABI negotiation | `src/init.c` | Hard-fails on version mismatch instead of silently corrupting state |
| Vista-style sync | `src/sync.c` | `CONDITION_VARIABLE` / `InitOnceExecuteOnce` shapes (Vista+ APIs) that use the OS's own implementation where present and a built-in one on XP — see "Vista-style synchronization" below |
| One-time init (legacy) | `src/once.c` | Poll-based once, needs no OS support at all (`InitOnceExecuteOnce` is Vista+, it does not exist on any XP) |
| Condition variable (legacy) | `src/condvar.c` | `init`/`destroy`-style wrappers over the Vista-style API above |
| Heap | `src/heap.c` | `malloc`/`free` with LFH + per-thread small-object cache |
| Locale | `src/locale.c` | Thread-local, refcounted, immutable locale objects; `LC_NUMERIC`/`LC_MONETARY`-style fields, `_stricmp` + `stricmp_l`, `toupper_l`/`tolower_l`, and a static name→LCID table (XP has no `LocaleNameToLCID`, that's Vista+) |
| Exception trampoline | `src/exception.c` | Shared last-chance SEH filter, per-thread `_set_se_translator`-style hook, and `ucrt_xp_guarded_call()` fault-containment primitive |
| stdio | `src/stdio.c` | POSIX-style fd table (`_open`/`_read`/`_write`/`_close`/`_lseek`) + buffered `FILE*` layer with text-mode CRLF↔LF, `fgets`/`fputs`/`ungetc`/`clearerr`/`rewind`/`fileno`, `remove`/`rename`, standard streams, and a magic field on `UCRT_XP_FILE` that rejects use-after-`fclose()` / garbage pointers cleanly |
| printf engine | `src/format.c` | `vsnprintf`/`snprintf`/`vsprintf`/`sprintf` (int/uint/hex/oct/string/char/pointer/float) |
| scanf engine | `src/scan.c` | `scanf`/`fscanf`/`sscanf` subset (`%d %i %u %o %x %c %s %f %n`) |
| Wide-char layer | `src/wide.c` | `_wopen`/`_wfopen`, `wcslen`/`wcsnlen`/`wcscmp`/`wcsncmp`/`wcscpy`/`wcsncpy`/`wcscat`, `_wcsicmp` + `wcsicmp_l`, ANSI↔UTF-16 conversion, wide printf family |
| `<string.h>` | `src/string.c` | `mem*`/`str*` family, `_strnicmp`, reentrant `strtok_r` |
| `<ctype.h>` | `src/ctype.c` | `is*`/`toupper`/`tolower`, fixed to the "C"/ASCII locale (use `locale.c`'s `_l` functions for locale-aware classification) |
| `<stdlib.h>` numeric/algorithms | `src/convert.c` | `atoi`/`atol`/`atof`/`strtol`/`strtoul`/`strtod`, `abs`/`labs`, always-per-thread `rand`/`srand`, `qsort`/`bsearch` |
| Process lifetime | `src/process.c` | TLS-backed `errno`, `exit`/`abort`/`atexit` |
| `<time.h>` | `src/time.c` | 64-bit `time_t` (no 2038 overflow), reentrant `localtime`/`gmtime` (out-parameter, not a shared static buffer), `clock`, a minimal `strftime` |
| Usability layer | `include/ucrt_xp_compat.h`, `include/ucrt_xp.hpp` | Optional: familiar CRT names via macros, and C++ RAII wrappers — see "Quickstart" below |

## Scope: what "full CRT replacement" means here

A complete C runtime is hundreds of functions across `<stdio.h>`,
`<stdlib.h>`, `<string.h>`, `<ctype.h>`, `<math.h>`, `<time.h>`,
`<locale.h>`, `<setjmp.h>`, `<signal.h>`, wide-char equivalents of all
of the above, and more. Reproducing every corner of that safely isn't
realistic here, so coverage is prioritized by a specific criterion:

**Covered, in priority order:**
1. Functions with real **cross-module/cross-VC-version ABI risk** —
   heap, locale, threading sync, exceptions, stdio (the original design
   doc's whole reason for existing).
2. Functions that are **pure/stateless but commonly needed** and safe to
   reimplement directly — `string.h`, `ctype.h`, numeric conversions,
   `qsort`/`bsearch`.
3. Functions where the **classic CRT has a known, real defect** worth
   fixing outright rather than reproducing — non-reentrant `rand()`
   (ifdef-dependent global-vs-TLS state across CRT versions),
   non-reentrant `localtime()`/`gmtime()` (shared static buffer), 32-bit
   `time_t` (2038 overflow), process-global `errno`.

**Deliberately NOT covered, and why:**
- **`<math.h>` transcendental functions** (`sin`/`cos`/`log`/`exp`/`pow`/
  etc.) — these are pure, stateless, and the compiler's own FPU
  code-gen or intrinsics already handle them correctly and fast, with no
  cross-module ABI hazard (there's no state to disagree about). Hand-
  rolling these would trade a well-tested, hardware-accelerated
  implementation for a hand-written one purely for the sake of
  "coverage" — worse on every axis. Link against the compiler's own
  runtime support for these, same as before.
- **`<setjmp.h>`** — `setjmp`/`longjmp` are compiler intrinsics tied to
  stack-unwind code generation, not linkable library functions in the
  usual sense; there's nothing here to "replace."
- **Full `<locale.h>` category coverage, `<signal.h>`** —
  real gaps, not principled exclusions; see "What's intentionally not
  implemented" below for the honest list of what's still missing versus
  what's out of scope on purpose. (Wide scanf is now implemented.)

## Design note: why UCRT_XP_FILE has a magic number, and what it's NOT for

`UCRT_XP_FILE`'s first field is a magic number, checked by every
function in `stdio.c` before touching anything else in the struct. This
exists purely to catch **misuse of ucrt_xp's own objects** — a stale
pointer used after `ucrt_xp_fclose()`, or a garbage/uninitialized
pointer passed in by mistake — and fail with a clean error return
instead of reading whatever bytes happen to be at the expected offsets.
`fclose()` overwrites the magic with a distinct "dead" sentinel before
freeing, so a use-after-free is caught immediately rather than only
after the memory happens to be reused for something unrelated.

This is deliberately **not** a mechanism for detecting whether a pointer
is a `UCRT_XP_FILE*` versus a real CRT `FILE*`, so the two could be used
interchangeably behind one type name. That idea was raised and
considered, and rejected for two structural reasons: a real `FILE`'s
memory layout is exactly the kind of CRT-version-dependent unknown this
whole project exists to route around, so there's no offset that's safe
to read from an arbitrary `FILE*` in the first place; and even a
successful "this is a real FILE" detection would still need to hand the
call off to some actual CRT's `fread`/`fwrite`/`fclose` — and *which*
CRT, out of `msvcrt.dll`/`MSVCR70`/.../`MSVCR100`, is exactly the
ABI-version question this project exists to eliminate, not re-derive at
runtime. Using a magic number for that purpose would trade a free,
100%-reliable compile-time type distinction (today, mixing up `FILE*`
and `UCRT_XP_FILE*` is a compiler error) for a runtime heuristic that
can, in principle, collide with whatever bytes an unrelated struct
happens to start with — a strict downgrade, not an improvement, and
inconsistent with the "never guess, always verify" philosophy behind
`ucrt_xp_init()`'s own ABI check.

## Vista-style synchronization (ABI 1.1)

`CONDITION_VARIABLE`, `SleepConditionVariableCS`, `WakeConditionVariable`,
`WakeAllConditionVariable` and `InitOnceExecuteOnce` all arrived with Windows
Vista; none exists on XP. ucrt_xp offers the same shapes so Vista-style code
ports by renaming - or without even that (see the compat header below):

```c
static UCRT_XP_CONDITION_VARIABLE cv = UCRT_XP_CONDITION_VARIABLE_INIT; /* no init/destroy calls */
static CRITICAL_SECTION cs;

EnterCriticalSection(&cs);
while (!ready)
    ucrt_xp_SleepConditionVariableCS(&cv, &cs, INFINITE);   /* FALSE + ERROR_TIMEOUT on timeout */
LeaveCriticalSection(&cs);
/* elsewhere:  ucrt_xp_WakeConditionVariable(&cv);  /  ucrt_xp_WakeAllConditionVariable(&cv); */

static UCRT_XP_INIT_ONCE once = UCRT_XP_INIT_ONCE_STATIC_INIT;
static BOOL WINAPI init_fn(UCRT_XP_INIT_ONCE *o, PVOID param, PVOID *ctx) { /* ... */ return TRUE; }
ucrt_xp_InitOnceExecuteOnce(&once, init_fn, NULL, &ctx);
```

Or, with the optional compat header, write the Vista names verbatim:

```c
#define UCRT_XP_USE_VISTA_NAMES
#include "ucrt_xp_compat.h"
static CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
SleepConditionVariableCS(&cv, &cs, INFINITE);
```

**Runtime dispatch.** On first use the runtime looks the five APIs up in
`kernel32.dll` with `GetProcAddress`. If all are present (Vista and later, and
Wine) every call is forwarded to the OS. Otherwise (XP) a built-in
implementation with the same semantics is used. The choice is made once per
process; `ucrt_xp_sync_is_native()` reports which one you got.

**The XP implementation** gives each waiter its own event and a place in a FIFO
queue, so a wakeup is aimed at one specific waiter and a thread that starts
waiting later cannot steal it. A waiter that times out removes itself under the
same lock the waker uses, so "timed out" and "was woken" are decided exactly
once. All fallback objects share one process-wide leaf lock; that is what lets
a zero-filled object be valid with no per-object setup.

**Not provided:** `SleepConditionVariableSRW` (SRW locks do not exist on XP) and
`InitOnceBeginInitialize`/`InitOnceComplete`. As with the native API: re-check
your predicate in a loop (wakeups may be spurious), hold the CS exactly once
while waiting, and never initialize the same once-object from inside its own
initializer. The callback is `WINAPI` (`__stdcall`), matching the native type.

The legacy `ucrt_xp_cond_*` functions are unchanged in signature and struct
layout but now simply forward to this API, so they get the same behavior.

## Layout

```
ucrt_xp/
├── include/
│   ├── ucrt_xp.h        # public, ABI-stable C header — the contract
│   ├── ucrt_xp_compat.h # optional: familiar CRT names via macros
│   └── ucrt_xp.hpp      # optional: C++ RAII wrappers (C++98/03)
├── src/
│   ├── internal.h
│   ├── init.c
│   ├── once.c
│   ├── condvar.c
│   ├── sync.c
│   ├── heap.c
│   ├── locale.c
│   ├── exception.c
│   ├── stdio.c
│   ├── format.c
│   ├── wide.c
│   ├── string.c
│   ├── ctype.c
│   ├── convert.c
│   ├── process.c
│   ├── time.c
│   └── ucrt_xp.def     # pinned export ordinals
├── examples/
│   ├── hello.c          # autostart + compat header
│   └── hello.cpp        # RAII wrappers
├── tests/test_basic.c
├── tests/test_sync.c
└── CMakeLists.txt
```

## Building

### With real MSVC (VC6 → VC2010), targeting XP

```bat
cmake -G "Visual Studio 9 2008" -A Win32 -B build .
cmake --build build --config Release
```

`CMakeLists.txt` forces `_WIN32_WINNT=0x0501` / `WINVER=0x0501` and (on
MSVC) `/SUBSYSTEM:WINDOWS,5.01`, so any accidental use of a Vista+-only
API fails at compile/link time rather than at runtime on an XP machine.

### Cross-compiling / CI sanity build (MinGW-w64, 32-bit)

This is how the code in this repo was verified in this environment,
since no MSVC/XP box was available:

```sh
sudo apt-get install mingw-w64-i686-dev gcc-mingw-w64-i686
mkdir build && cd build
i686-w64-mingw32-gcc -shared -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
    -DUCRT_XP_BUILD_DLL -I../include -o ucrt_xp.dll \
    ../src/*.c ../src/ucrt_xp.def -Wl,--kill-at -lgdi32 -luser32

i686-w64-mingw32-gcc -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -c -I../include ../src/*.c
i686-w64-mingw32-ar rcs libucrt_xp_static.a *.o

i686-w64-mingw32-gcc -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -I../include \
    -o ucrt_xp_tests.exe ../tests/test_basic.c libucrt_xp_static.a -luser32
```

Both the shared DLL and the static archive built and linked cleanly
against this project's own test harness; running the resulting `.exe`
requires an actual Windows/XP environment (or Wine), which wasn't
available in this sandbox, so runtime execution has **not** been
verified end-to-end — treat this as compile/link-verified, not
execution-verified.

## Quickstart (the easy path)

The core API (§"Using it from an application" below) is explicit by
design - every call is traceable, nothing happens behind your back. If
that's more ceremony than you want for a small program, three optional,
purely-additive pieces cut it down to nearly zero:

**1. Autostart - skip the manual `ucrt_xp_init()` call.** In exactly one
source file:
```c
#define UCRT_XP_IMPLEMENT_AUTOSTART
#include "ucrt_xp.h"
```
The ABI check now runs automatically before `main()` (via MSVC's
`.CRT$XCU` init-section mechanism, or `__attribute__((constructor))` on
GCC/MinGW - both verified in this repo's own build, see "Verification
status"). No call needed anywhere else in the program.

**2. `ucrt_xp_compat.h` - keep the names you already know.**
```c
#define UCRT_XP_USE_STD_NAMES
#include "ucrt_xp_compat.h"

void *p = malloc(64);              /* -> ucrt_xp_malloc */
UCRT_XP_FILE *f = fopen("x", "w"); /* -> ucrt_xp_fopen   */
fprintf(f, "%d\n", 42);
printf("also works: %d\n", 42);    /* -> ucrt_xp_printf, writes to stdout */
free(p);
```
Read the caveats in `include/ucrt_xp_compat.h` before using this in a
file that also includes the real `<stdio.h>`/`<stdlib.h>`.

**3. `ucrt_xp.hpp` - RAII for C++ callers.**
```cpp
#include "ucrt_xp.hpp"

ucrt_xp::File f("x.txt", "w");           // auto-closes
ucrt_xp::Lock lock;
{ ucrt_xp::Guard g(lock); /* ... */ }    // auto-unlocks
ucrt_xp::Locale loc("C"); loc.make_current();
```
Written against C++98/03 on purpose (no `auto`, `nullptr`, move
semantics) so it stays buildable on VC6 like the rest of this project.
Only plain-C types (`UCRT_XP_FILE*`, `CRITICAL_SECTION`) ever cross a
module/DLL boundary - the C++ classes are header-only sugar local to one
translation unit, consistent with the project's own warning that C++ ABI
isn't stable across VC versions.

Working examples: `examples/hello.c` (autostart + compat header) and
`examples/hello.cpp` (RAII). Both build and link cleanly in this repo's
own verification pass - see below.

## Full API (explicit, no magic)

```c
#include "ucrt_xp.h"

int main(void)
{
    /* First call, before touching anything else in ucrt_xp: */
    if (!ucrt_xp_init(UCRT_XP_ABI_VERSION)) {
        return 1; /* unreachable: ucrt_xp_init terminates on mismatch */
    }

    void *p = ucrt_xp_malloc(128);
    ucrt_xp_free(p);
    return 0;
}
```

Recommended deployment: copy `ucrt_xp.dll` into the application's own
folder (next to the `.exe`) rather than relying on WinSxS, per the
design doc's §5 — XP's side-by-side resolution is the least reliable
part of the OS to depend on.

## What's intentionally NOT implemented here

This is a reference implementation, not a literal "every CRT function."
See "Scope" above for the principled exclusions (`math.h` transcendentals,
`setjmp.h`). Remaining real gaps, not principled exclusions:

- **True cross-VC-version C++ exception interop.** `ucrt_xp_guarded_call()`
  and `ucrt_xp_set_se_translator()` give a shared, ABI-stable way to
  catch and inspect *structured* (hardware/OS) exceptions across module
  boundaries, but a `throw`n C++ object's internal layout still depends
  on which VC version compiled the throwing module. This is a permanent,
  structural limitation, not a to-do item — it needs compiler-level
  cooperation (matching `ThrowInfo`/RTTI layouts) that a runtime DLL
  cannot retrofit on its own.
- **Wide-char stdio.** `wide.c` covers wide paths (`_wopen`/`_wfopen`),
  basic `wcs*` primitives (including `_wcsicmp`), a full wide printf
  family (`vswprintf`/`swprintf`/`fwprintf`), and wide scanf
  (`vswscanf`/`swscanf`/`vfwscanf`/`fwscanf`/`wscanf`) including scansets
  and `%p`. `fwprintf` still narrows its output to the current code page
  before writing (documented in its header comment) rather than emitting
  raw UTF-16 to the file; file-based wide scanf likewise reads via the
  narrow stdio layer (one byte → one wchar).
- **`scanf`.** `scan.c` implements `scanf`/`fscanf`/`sscanf` for
  `%d %i %u %o %x %c %s %f %n %p %[…] %%` with width and `h`/`l`/`ll`.
  Still missing: full locale-aware numeric parsing.
- **Full Unicode locale data.** `locale.c`'s name→LCID table covers a
  handful of common locales; it's not a substitute for a real CLDR/ICU-
  style data set, and case folding beyond ASCII goes through
  `CharUpperW`/`CompareStringW` rather than a home-grown Unicode table.
- **`strftime`** covers `%Y %y %m %d %H %M %S %j %w %a %A %b %B %h %I %p
  %U %W %c %x %X %%`. Day/month names are English (C locale). Full
  localized name tables remain out of scope (same CLDR trade-off as
  `locale.c`).
- **`format.c`'s float conversion** is a straightforward multiply/divide
  implementation, not a shortest-round-trip (Grisu/Ryu-class) algorithm —
  fine for logs and UI text, not for lossless float serialization. The
  same caveat applies to `convert.c`'s `strtod`.
- **`%n`** is deliberately disabled in both formatters (see the comment
  in `format.c` — it's a classic exploit primitive; real CRTs disable it
  by default too).
- **`process.c`'s `errno`** is provided for ported code that reads/writes
  it directly, but ucrt_xp's own functions don't set it — they report
  errors via return value / `GetLastError()` instead, which is more
  precise on Win32. Don't expect `ucrt_xp_malloc` failure, for instance,
  to set `errno` the way the classic CRT's `malloc` does.

## Verification status

**Sync layer (added in ABI 1.1): executed under Wine 9.0, not on a real XP
machine.** `tests/test_sync.c` runs every test twice - once with whatever the
OS provides (Wine's native implementation) and once with the XP fallback forced
on - covering: static initialization, timeout semantics (`ERROR_TIMEOUT`, lock
re-held), Wake-one vs WakeAll counts, a 4-producer/4-consumer bounded queue
using single-wake only (20,000 items, none lost), 6,000 timed waits racing
wakes (both outcomes occur), and `InitOnceExecuteOnce` (8 racers, context
propagation, failure-then-retry, argument checks). It also builds and runs as
a DLL client. Wine's kernel32 is not XP's, so the fallback's behavior on real
XP is still unconfirmed - the fallback uses only APIs present on XP RTM
(`CreateEvent`, `WaitForSingleObject`, `CRITICAL_SECTION`, `GetProcAddress`),
but run `tests/test_sync.c` on a real XP box before shipping.

**Everything else below** predates the sync layer and is unchanged:

Every `.c` file in `src/` cross-compiles cleanly (0 errors; only benign
`#pragma warning`/unused-flag notices) against real Win32 headers via
`i686-w64-mingw32-gcc`, and the shared DLL, static archive, and
`tests/test_basic.c` all link successfully — including `string.c`/
`ctype.c`/`convert.c`/`process.c`/`time.c` and their dedicated tests
(`test_string`, `test_ctype`, `test_convert`, `test_process`,
`test_time`), the `stdin`/`stdout`/`stderr`/`printf`/`puts`/`putchar`/
`getchar` additions to `stdio.c` (`test_std_streams`, which tolerates
`GetStdHandle` legitimately returning NULL when no console is attached
rather than treating that as a failure), the `UCRT_XP_FILE` magic-number
misuse detection (`test_file_magic_detection`, which verifies a
use-after-`fclose()` pointer and an arbitrary non-FILE pointer are both
rejected by every stdio function rather than crashing), on top of the
earlier `format.c`/`wide.c` coverage (`test_format`, `test_wide`,
`test_wide_printf`) and the segmented (block-list) fd table in
`stdio.c`.

`examples/hello.c` (autostart + `ucrt_xp_compat.h`) and `examples/
hello.cpp` (`ucrt_xp.hpp` RAII wrappers) both build with zero warnings
against the static library, via `gcc` and `g++` respectively. For the
autostart mechanism specifically, `objdump` on the resulting binary
confirms `__CTOR_LIST__`/`.ctors` contains `ucrt_xp__autostart_ctor` and
that MinGW's own CRT startup (`___do_global_ctors`) is linked in to walk
it — i.e. this isn't just "it compiled", the actual before-`main()`
call-out mechanism is present and wired up in the binary. The MSVC
`.CRT$XCU` path uses the same well-established technique (it's how C++
static object constructors work under MSVC) but couldn't be verified in
this sandbox, which has no MSVC toolchain.

`ucrt_xp_guarded_call()` has a GCC/MinGW-specific fallback path
(`AddVectoredExceptionHandler` + `setjmp`/`longjmp`, see the comment in
`exception.c`) that exists purely so this logic is exercised outside of
MSVC; the real target path for VC6–VC2010 uses native `__try`/`__except`
and only that path ships in a production build.

Actual **execution** on Windows/XP (or under Wine) was not possible in
this sandbox: no XP box is available, and multiple attempts to get Wine
running (`apt`, and a portable static build fetched from GitHub releases)
were blocked by this sandbox's broken/partial package mirror (missing
32-bit multiarch libraries, conflicting package versions) rather than
anything about the project itself. Treat this as compile/link-verified,
not execution-verified, and run `tests/test_basic.c` on a real target
before shipping.

## ABI stability rules (read before modifying `ucrt_xp.h`)

1. Never change an existing function's signature or a struct's layout.
2. Only append new functions/fields, and bump `UCRT_XP_ABI_MINOR`.
3. A breaking change bumps `UCRT_XP_ABI_MAJOR` and is a new, separately
   loaded/deployed DLL name generation (e.g. `ucrt_xp2.dll`) — old
   binaries must keep working against the old DLL indefinitely.
4. Every new export gets the next unused ordinal in `ucrt_xp.def`.
   Ordinals are never reused or renumbered.
