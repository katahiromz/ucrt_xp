/*
 * stdio.c - the missing §4.6 piece: a descriptor table (the "_open/_read/
 * _write/_close/_lseek" layer) plus a buffered FILE-like layer on top of
 * it. Neither layer calls the host CRT's own stdio - everything goes
 * straight to Win32 (CreateFileA/ReadFile/WriteFile/SetFilePointerEx),
 * so this stays independent of whichever msvcrXX.dll a given module was
 * linked against.
 *
 * Scope, deliberately kept small for this reference implementation:
 *   - ANSI (char*) paths only, no wide-char variants.
 *   - Text mode (_O_TEXT) does LF<->CRLF translation; no locale-aware
 *     multibyte handling.
 *   - fprintf reuses wvsprintfA, so it inherits that function's 1024-byte
 *     output cap and its restricted format-specifier set (no floating
 *     point). Fine for logging/diagnostics; not a full printf.
 */
#include "internal.h"
#include <stdio.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Descriptor table                                                    */
/* ------------------------------------------------------------------ */

typedef struct FdEntry {
    HANDLE handle;
    int    in_use;
    int    text_mode;   /* UCRT_XP_O_TEXT was requested at open() time */
    int    append_mode;
} FdEntry;

/*
 * Segmented (block-list) descriptor table instead of a single fixed-size
 * or single realloc-grown array. This matters for correctness, not just
 * style: a plain array that ucrt_xp_realloc()-grows can move in memory,
 * which would invalidate an FdEntry* a caller obtained just before
 * another thread triggered a grow-via-realloc on a different fd. With a
 * block list, each UCRT_XP_FD_BLOCK-sized block is allocated once and
 * never moved or freed for the life of the process - only the *array of
 * block pointers* can grow (and that array is only ever read while
 * holding g_fd_lock, never dereferenced after release), so an FdEntry*
 * handed back by get_fd() stays valid indefinitely.
 */
#define UCRT_XP_FD_BLOCK 64

static FdEntry **g_fd_blocks = NULL;
static int g_fd_block_count = 0;
static CRITICAL_SECTION g_fd_lock;
static UCRT_XP_ONCE g_fd_once = UCRT_XP_ONCE_INIT;

static BOOL __cdecl init_fd_table(void *param)
{
    (void)param;
    InitializeCriticalSection(&g_fd_lock);
    g_fd_blocks = NULL;
    g_fd_block_count = 0;
    return TRUE;
}

static int alloc_fd(HANDLE h, int text_mode, int append_mode)
{
    int b, i;
    ucrt_xp_once(&g_fd_once, init_fd_table, NULL);

    EnterCriticalSection(&g_fd_lock);

    for (b = 0; b < g_fd_block_count; b++) {
        FdEntry *block = g_fd_blocks[b];
        for (i = 0; i < UCRT_XP_FD_BLOCK; i++) {
            if (!block[i].in_use) {
                block[i].handle = h;
                block[i].in_use = 1;
                block[i].text_mode = text_mode;
                block[i].append_mode = append_mode;
                LeaveCriticalSection(&g_fd_lock);
                return b * UCRT_XP_FD_BLOCK + i;
            }
        }
    }

    /* No free slot anywhere: allocate one more block. The new block's
     * own memory never moves once allocated; only g_fd_blocks (the array
     * of block *pointers*) is grown here, and that growth is entirely
     * contained within this locked section. */
    {
        FdEntry *new_block = (FdEntry *)ucrt_xp_malloc(
            (size_t)UCRT_XP_FD_BLOCK * sizeof(FdEntry));
        FdEntry **grown_index;
        if (!new_block) { LeaveCriticalSection(&g_fd_lock); return -1; }
        ZeroMemory(new_block, (size_t)UCRT_XP_FD_BLOCK * sizeof(FdEntry));

        grown_index = (FdEntry **)ucrt_xp_realloc(
            g_fd_blocks, (size_t)(g_fd_block_count + 1) * sizeof(FdEntry *));
        if (!grown_index) {
            ucrt_xp_free(new_block);
            LeaveCriticalSection(&g_fd_lock);
            return -1;
        }
        grown_index[g_fd_block_count] = new_block;
        g_fd_blocks = grown_index;
        b = g_fd_block_count;
        g_fd_block_count++;

        new_block[0].handle = h;
        new_block[0].in_use = 1;
        new_block[0].text_mode = text_mode;
        new_block[0].append_mode = append_mode;
        LeaveCriticalSection(&g_fd_lock);
        return b * UCRT_XP_FD_BLOCK + 0;
    }
}

/* Internal seam used by wide.c - see internal.h. */
int ucrt_xp__register_fd(HANDLE h, int text_mode, int append_mode)
{
    return alloc_fd(h, text_mode, append_mode);
}

static FdEntry *get_fd(int fd)
{
    int b, i;
    FdEntry *block;
    FdEntry *e;

    if (fd < 0) return NULL;
    ucrt_xp_once(&g_fd_once, init_fd_table, NULL);

    b = fd / UCRT_XP_FD_BLOCK;
    i = fd % UCRT_XP_FD_BLOCK;

    EnterCriticalSection(&g_fd_lock);
    if (b >= g_fd_block_count) {
        LeaveCriticalSection(&g_fd_lock);
        return NULL;
    }
    block = g_fd_blocks[b]; /* stable forever once published - safe to
                              * keep using after we unlock below */
    e = &block[i];
    if (!e->in_use) e = NULL;
    LeaveCriticalSection(&g_fd_lock);
    return e;
}

__declspec(dllexport) int __cdecl ucrt_xp_open(const char *path, int oflag, int pmode)
{
    DWORD access = 0, creation = OPEN_EXISTING;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    HANDLE h;
    int fd;

    (void)pmode; /* Win32 ACLs are out of scope for this reference layer;
                  * files are created with default security attributes. */

    if ((oflag & UCRT_XP_O_RDWR) == UCRT_XP_O_RDWR) {
        access = GENERIC_READ | GENERIC_WRITE;
    } else if (oflag & UCRT_XP_O_WRONLY) {
        access = GENERIC_WRITE;
    } else {
        access = GENERIC_READ;
    }

    if (oflag & UCRT_XP_O_CREAT) {
        if (oflag & UCRT_XP_O_EXCL) {
            creation = CREATE_NEW;
        } else if (oflag & UCRT_XP_O_TRUNC) {
            creation = CREATE_ALWAYS;
        } else {
            creation = OPEN_ALWAYS;
        }
    } else if (oflag & UCRT_XP_O_TRUNC) {
        creation = TRUNCATE_EXISTING;
    }

    h = CreateFileA(path, access, share, NULL, creation,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }

    if (oflag & UCRT_XP_O_APPEND) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(h, zero, NULL, FILE_END);
    }

    fd = alloc_fd(h, (oflag & UCRT_XP_O_TEXT) ? 1 : 0,
                  (oflag & UCRT_XP_O_APPEND) ? 1 : 0);
    if (fd < 0) {
        CloseHandle(h);
        return -1;
    }
    return fd;
}

__declspec(dllexport) int __cdecl ucrt_xp_close(int fd)
{
    FdEntry *e = get_fd(fd);
    if (!e) return -1;

    EnterCriticalSection(&g_fd_lock);
    CloseHandle(e->handle);
    e->in_use = 0;
    e->handle = NULL;
    LeaveCriticalSection(&g_fd_lock);
    return 0;
}

/* Appends to bytes onto *this call's write* (append-mode reseek before
 * each write is what real POSIX O_APPEND semantics require, since two
 * processes/handles can otherwise interleave writes at stale offsets). */
static void reseek_if_append(FdEntry *e)
{
    if (e->append_mode) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(e->handle, zero, NULL, FILE_END);
    }
}

__declspec(dllexport) long __cdecl ucrt_xp_read(int fd, void *buf, unsigned int count)
{
    FdEntry *e = get_fd(fd);
    DWORD got = 0;
    if (!e) return -1;

    if (!ReadFile(e->handle, buf, count, &got, NULL)) {
        return -1;
    }

    if (e->text_mode && got > 0) {
        /* Strip CR before LF in place - shrinks got, never grows it. */
        unsigned char *p = (unsigned char *)buf;
        DWORD src, dst = 0;
        for (src = 0; src < got; src++) {
            if (p[src] == '\r' && src + 1 < got && p[src + 1] == '\n') {
                continue; /* drop the CR, keep the LF on the next iter */
            }
            p[dst++] = p[src];
        }
        got = dst;
    }
    return (long)got;
}

__declspec(dllexport) long __cdecl ucrt_xp_write(int fd, const void *buf, unsigned int count)
{
    FdEntry *e = get_fd(fd);
    DWORD written = 0;
    if (!e) return -1;

    reseek_if_append(e);

    if (!e->text_mode) {
        if (!WriteFile(e->handle, buf, count, &written, NULL)) return -1;
        return (long)written;
    }

    /* Text mode: expand lone LF -> CRLF. Done via a bounce buffer sized
     * generously (2x + slack) rather than byte-at-a-time WriteFile calls. */
    {
        const unsigned char *src = (const unsigned char *)buf;
        unsigned char stackbuf[4096];
        unsigned char *bounce = stackbuf;
        void *heap_bounce = NULL;
        size_t need = (size_t)count * 2 + 16;
        unsigned int i;
        size_t o = 0;
        long total_written = 0;

        if (need > sizeof(stackbuf)) {
            heap_bounce = ucrt_xp_malloc(need);
            if (!heap_bounce) return -1;
            bounce = (unsigned char *)heap_bounce;
        }

        for (i = 0; i < count; i++) {
            if (src[i] == '\n') {
                bounce[o++] = '\r';
            }
            bounce[o++] = src[i];
        }

        if (!WriteFile(e->handle, bounce, (DWORD)o, &written, NULL)) {
            if (heap_bounce) ucrt_xp_free(heap_bounce);
            return -1;
        }
        /* Report the caller's original byte count on full success, per
         * conventional _write()/fwrite() semantics (callers count logical
         * bytes, not the on-disk CRLF-expanded byte count). */
        total_written = (written == o) ? (long)count : -1;
        if (heap_bounce) ucrt_xp_free(heap_bounce);
        return total_written;
    }
}

__declspec(dllexport) __int64 __cdecl ucrt_xp_lseek(int fd, __int64 offset, int origin)
{
    FdEntry *e = get_fd(fd);
    LARGE_INTEGER li, out;
    DWORD method;
    if (!e) return -1;

    li.QuadPart = offset;
    switch (origin) {
    case UCRT_XP_SEEK_SET: method = FILE_BEGIN;   break;
    case UCRT_XP_SEEK_CUR: method = FILE_CURRENT; break;
    case UCRT_XP_SEEK_END: method = FILE_END;     break;
    default: return -1;
    }
    if (!SetFilePointerEx(e->handle, li, &out, method)) return -1;
    return out.QuadPart;
}

/* ------------------------------------------------------------------ */
/* Buffered FILE layer                                                 */
/* ------------------------------------------------------------------ */

#define UCRT_XP_BUFSIZE 4096

typedef enum { STREAM_IDLE, STREAM_READING, STREAM_WRITING } StreamDir;

/*
 * UCRT_XP_FILE carries a magic number as its first field, checked by
 * every function below before touching anything else in the struct.
 * This is deliberately scoped to one purpose: catching misuse of
 * ucrt_xp's own objects (use-after-fclose, an uninitialized or garbage
 * pointer passed in by mistake) and failing safely instead of reading
 * whatever garbage happens to be at the expected offsets.
 *
 * This is NOT a mechanism for distinguishing a UCRT_XP_FILE* from a
 * real CRT FILE* so the two could be used interchangeably - that idea
 * was considered and deliberately rejected. Two reasons: (1) a real
 * FILE's layout is exactly the kind of CRT-version-dependent unknown
 * this whole project exists to route around, so there is no safe offset
 * to even read from an arbitrary FILE* to check; and (2) even a
 * successful "this is a real FILE" detection would still need to hand
 * the call off to *some* actual CRT's fread/fwrite/fclose - and which
 * CRT's, out of msvcrt.dll/MSVCR70/.../MSVCR100, is exactly the
 * ABI-version question ucrt_xp exists to eliminate, not solve. A magic
 * check would only move that unresolved question one layer deeper while
 * also downgrading a free, 100%-reliable compile-time type distinction
 * (FILE* vs UCRT_XP_FILE* are different types - mixing them up is a
 * compiler error today) into a runtime heuristic that can, in principle,
 * collide with whatever bytes happen to sit at the front of an unrelated
 * struct. That trade is a strict downgrade, not an improvement.
 */
#define UCRT_XP_FILE_MAGIC 0x55584646u /* ASCII "UXFF", arbitrary but stable */
#define UCRT_XP_FILE_MAGIC_DEAD 0x44454144u /* "DEAD" - written over the
                                              * magic on fclose(), so a
                                              * stale use-after-free
                                              * pointer fails validation
                                              * immediately rather than
                                              * still "looking valid"
                                              * until the memory happens
                                              * to be reused for
                                              * something else. */

struct UCRT_XP_FILE {
    DWORD magic;     /* must be UCRT_XP_FILE_MAGIC - checked first, always */
    int fd;
    unsigned char *buf;
    size_t bufsize;
    size_t bufpos;   /* next unread byte (read mode) or next free slot (write mode) */
    size_t buflen;   /* valid bytes in buf (read mode only) */
    StreamDir dir;
    int eof;
    int error;
    int owns_fd;     /* TRUE unless wrapping an externally-managed fd */
    int unget_char;  /* single-char pushback for ungetc; -1 = empty */
};

/* Every public UCRT_XP_FILE* function calls this first. Deliberately
 * tolerant of a NULL pointer (returns FALSE, same as every other NULL
 * check in this file) - the actual safety property this adds is for a
 * *non-NULL but wrong/stale* pointer, which a plain NULL check can't
 * catch. */
static BOOL validate_file(const UCRT_XP_FILE *f)
{
    return f != NULL && f->magic == UCRT_XP_FILE_MAGIC;
}

static BOOL parse_mode(const char *mode, int *oflag)
{
    int f = 0;
    int has_plus = (mode && (strchr(mode, '+') != NULL));

    if (!mode || !*mode) return FALSE;

    switch (mode[0]) {
    case 'r': f = has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_RDONLY; break;
    case 'w': f = (has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_WRONLY)
                  | UCRT_XP_O_CREAT | UCRT_XP_O_TRUNC; break;
    case 'a': f = (has_plus ? UCRT_XP_O_RDWR : UCRT_XP_O_WRONLY)
                  | UCRT_XP_O_CREAT | UCRT_XP_O_APPEND; break;
    default: return FALSE;
    }

    if (strchr(mode, 'b')) {
        f |= UCRT_XP_O_BINARY;
    } else {
        f |= UCRT_XP_O_TEXT; /* default, matches classic CRT fopen() */
    }

    *oflag = f;
    return TRUE;
}

static UCRT_XP_FILE *make_file_from_fd(int fd, int owns_fd)
{
    UCRT_XP_FILE *f;
    if (fd < 0) return NULL;

    f = (UCRT_XP_FILE *)ucrt_xp_malloc(sizeof(UCRT_XP_FILE));
    if (!f) {
        if (owns_fd) ucrt_xp_close(fd);
        return NULL;
    }

    f->fd = fd;
    f->buf = (unsigned char *)ucrt_xp_malloc(UCRT_XP_BUFSIZE);
    if (!f->buf) {
        ucrt_xp_free(f);
        if (owns_fd) ucrt_xp_close(fd);
        return NULL;
    }
    f->bufsize = UCRT_XP_BUFSIZE;
    f->bufpos = 0;
    f->buflen = 0;
    f->dir = STREAM_IDLE;
    f->eof = 0;
    f->error = 0;
    f->owns_fd = owns_fd ? 1 : 0;
    f->unget_char = -1;
    f->magic = UCRT_XP_FILE_MAGIC; /* set last, after every other field
                                     * is initialized - so a struct is
                                     * never observably "valid" (by
                                     * magic) before it's actually fully
                                     * constructed. */
    return f;
}

/* Internal seam used by wide.c - see internal.h. */
UCRT_XP_FILE *ucrt_xp__file_from_fd(int fd)
{
    return make_file_from_fd(fd, 1);
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_fopen(const char *path, const char *mode)
{
    int oflag, fd;

    if (!parse_mode(mode, &oflag)) return NULL;

    fd = ucrt_xp_open(path, oflag, 0);
    if (fd < 0) return NULL;

    return make_file_from_fd(fd, 1);
}

static int flush_write_buffer(UCRT_XP_FILE *f)
{
    if (f->dir != STREAM_WRITING || f->bufpos == 0) return 0;

    {
        long written = ucrt_xp_write(f->fd, f->buf, (unsigned int)f->bufpos);
        if (written < 0 || (size_t)written != f->bufpos) {
            f->error = 1;
            return -1;
        }
    }
    f->bufpos = 0;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_fflush(UCRT_XP_FILE *f)
{
    if (!validate_file(f)) return -1;
    return flush_write_buffer(f);
}

__declspec(dllexport) int __cdecl ucrt_xp_fclose(UCRT_XP_FILE *f)
{
    int rc = 0;
    if (!validate_file(f)) return -1;

    if (flush_write_buffer(f) != 0) rc = -1;
    if (f->owns_fd) ucrt_xp_close(f->fd);
    f->magic = UCRT_XP_FILE_MAGIC_DEAD; /* poison before freeing - see
                                          * the struct's header comment */
    ucrt_xp_free(f->buf);
    ucrt_xp_free(f);
    return rc;
}

/* Refills the read buffer from the fd. Returns number of bytes now
 * available (0 on EOF, negative on error). */
static long refill_read_buffer(UCRT_XP_FILE *f)
{
    long got = ucrt_xp_read(f->fd, f->buf, (unsigned int)f->bufsize);
    if (got < 0) { f->error = 1; return -1; }
    if (got == 0) { f->eof = 1; }
    f->bufpos = 0;
    f->buflen = (size_t)got;
    return got;
}

__declspec(dllexport) size_t __cdecl ucrt_xp_fread(
    void *buf, size_t size, size_t count, UCRT_XP_FILE *f)
{
    unsigned char *out = (unsigned char *)buf;
    size_t total_bytes = size * count;
    size_t copied = 0;

    if (!validate_file(f) || size == 0 || count == 0) return 0;

    if (f->dir == STREAM_WRITING) {
        flush_write_buffer(f);
        f->dir = STREAM_IDLE;
    }
    f->dir = STREAM_READING;

    /* Drain single-char ungetc pushback first. */
    if (f->unget_char >= 0 && copied < total_bytes) {
        out[copied++] = (unsigned char)f->unget_char;
        f->unget_char = -1;
        f->eof = 0;
    }

    while (copied < total_bytes) {
        size_t avail = f->buflen - f->bufpos;
        size_t need = total_bytes - copied;
        size_t chunk;

        if (avail == 0) {
            if (f->eof) break;
            if (refill_read_buffer(f) <= 0) break;
            avail = f->buflen - f->bufpos;
            if (avail == 0) break;
        }

        chunk = (need < avail) ? need : avail;
        CopyMemory(out + copied, f->buf + f->bufpos, chunk);
        f->bufpos += chunk;
        copied += chunk;
    }

    return copied / size;
}

__declspec(dllexport) size_t __cdecl ucrt_xp_fwrite(
    const void *buf, size_t size, size_t count, UCRT_XP_FILE *f)
{
    const unsigned char *in = (const unsigned char *)buf;
    size_t total_bytes = size * count;
    size_t copied = 0;

    if (!validate_file(f) || size == 0 || count == 0) return 0;

    if (f->dir == STREAM_READING) {
        f->bufpos = 0;
        f->buflen = 0;
        f->unget_char = -1;
        f->dir = STREAM_IDLE;
    }
    f->dir = STREAM_WRITING;

    while (copied < total_bytes) {
        size_t space = f->bufsize - f->bufpos;
        size_t need = total_bytes - copied;
        size_t chunk;

        if (space == 0) {
            if (flush_write_buffer(f) != 0) break;
            space = f->bufsize;
        }

        chunk = (need < space) ? need : space;
        CopyMemory(f->buf + f->bufpos, in + copied, chunk);
        f->bufpos += chunk;
        copied += chunk;
    }

    return copied / size;
}

__declspec(dllexport) int __cdecl ucrt_xp_fseek(UCRT_XP_FILE *f, long offset, int origin)
{
    __int64 r;
    if (!validate_file(f)) return -1;

    flush_write_buffer(f);
    f->bufpos = 0;
    f->buflen = 0;
    f->unget_char = -1;
    f->dir = STREAM_IDLE;
    f->eof = 0;

    r = ucrt_xp_lseek(f->fd, offset, origin);
    return (r < 0) ? -1 : 0;
}

__declspec(dllexport) long __cdecl ucrt_xp_ftell(UCRT_XP_FILE *f)
{
    __int64 pos;
    if (!validate_file(f)) return -1;

    pos = ucrt_xp_lseek(f->fd, 0, UCRT_XP_SEEK_CUR);
    if (pos < 0) return -1;

    if (f->dir == STREAM_READING) {
        pos -= (__int64)(f->buflen - f->bufpos);
    } else if (f->dir == STREAM_WRITING) {
        pos += (__int64)f->bufpos;
    }
    return (long)pos;
}

__declspec(dllexport) int __cdecl ucrt_xp_fgetc(UCRT_XP_FILE *f)
{
    unsigned char c;
    /* No separate validate_file() call needed - ucrt_xp_fread() already
     * validates and returns 0 on a bad pointer, which this correctly
     * surfaces as EOF/-1 rather than reading further. */
    if (ucrt_xp_fread(&c, 1, 1, f) != 1) return -1 /* EOF */;
    return (int)c;
}

__declspec(dllexport) int __cdecl ucrt_xp_fputc(int c, UCRT_XP_FILE *f)
{
    unsigned char ch = (unsigned char)c;
    if (ucrt_xp_fwrite(&ch, 1, 1, f) != 1) return -1;
    return (int)ch;
}

__declspec(dllexport) int __cdecl ucrt_xp_feof(UCRT_XP_FILE *f)
{
    return validate_file(f) ? f->eof : 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_ferror(UCRT_XP_FILE *f)
{
    return validate_file(f) ? f->error : 0;
}

__declspec(dllexport) void __cdecl ucrt_xp_clearerr(UCRT_XP_FILE *f)
{
    if (!validate_file(f)) return;
    f->eof = 0;
    f->error = 0;
}

__declspec(dllexport) void __cdecl ucrt_xp_rewind(UCRT_XP_FILE *f)
{
    if (!validate_file(f)) return;
    ucrt_xp_fseek(f, 0, UCRT_XP_SEEK_SET);
    f->eof = 0;
    f->error = 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_fileno(UCRT_XP_FILE *f)
{
    return validate_file(f) ? f->fd : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_ungetc(int c, UCRT_XP_FILE *f)
{
    if (!validate_file(f) || c == -1 /* EOF */) return -1;
    if (f->dir == STREAM_WRITING) {
        if (flush_write_buffer(f) != 0) return -1;
        f->dir = STREAM_IDLE;
    }
    /* Single-character pushback only (matches classic CRT). */
    if (f->unget_char >= 0) return -1;
    f->unget_char = (unsigned char)c;
    f->eof = 0;
    f->dir = STREAM_READING;
    return (unsigned char)c;
}

__declspec(dllexport) char* __cdecl ucrt_xp_fgets(char *s, int n, UCRT_XP_FILE *f)
{
    int i = 0;
    int c;

    if (!s || n <= 0 || !validate_file(f)) return NULL;

    while (i < n - 1) {
        c = ucrt_xp_fgetc(f);
        if (c == -1) {
            if (i == 0) return NULL; /* EOF with no data */
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

__declspec(dllexport) int __cdecl ucrt_xp_fputs(const char *s, UCRT_XP_FILE *f)
{
    size_t len, written;
    if (!s || !validate_file(f)) return -1;
    len = ucrt_xp_strlen(s);
    written = ucrt_xp_fwrite(s, 1, len, f);
    return (written == len) ? 0 : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_remove(const char *path)
{
    if (!path) return -1;
    return DeleteFileA(path) ? 0 : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) return -1;
    return MoveFileA(oldpath, newpath) ? 0 : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_fprintf(UCRT_XP_FILE *f, const char *fmt, ...)
{
    char stackbuf[512];
    char *heapbuf = NULL;
    char *outbuf = stackbuf;
    int needed, written;
    va_list args;

    if (!validate_file(f)) return -1;

    va_start(args, fmt);
    needed = ucrt_xp_vsnprintf(stackbuf, sizeof(stackbuf), fmt, args);
    va_end(args);

    if (needed < 0) return -1;

    if ((size_t)needed >= sizeof(stackbuf)) {
        /* Formatted output didn't fit the stack buffer - reformat once
         * into a precisely-sized heap buffer rather than truncating. */
        heapbuf = (char *)ucrt_xp_malloc((size_t)needed + 1);
        if (!heapbuf) return -1;
        va_start(args, fmt);
        ucrt_xp_vsnprintf(heapbuf, (size_t)needed + 1, fmt, args);
        va_end(args);
        outbuf = heapbuf;
    }

    written = (int)ucrt_xp_fwrite(outbuf, 1, (size_t)needed, f);
    if (heapbuf) ucrt_xp_free(heapbuf);
    return (written == needed) ? written : -1;
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_fdopen(int fd, const char *mode)
{
    int oflag;
    UCRT_XP_FILE *f;
    (void)oflag;
    if (fd < 0) return NULL;
    /* mode is accepted for API compatibility; buffering defaults apply. */
    if (mode && !parse_mode(mode, &oflag)) return NULL;
    f = make_file_from_fd(fd, 0); /* do not own/close the caller's fd */
    return f;
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_freopen(
    const char *path, const char *mode, UCRT_XP_FILE *f)
{
    int oflag;
    int fd;
    if (!validate_file(f)) return NULL;
    if (!parse_mode(mode, &oflag)) return NULL;

    flush_write_buffer(f);
    if (f->owns_fd) ucrt_xp_close(f->fd);

    fd = ucrt_xp_open(path, oflag, 0);
    if (fd < 0) {
        f->magic = UCRT_XP_FILE_MAGIC_DEAD;
        ucrt_xp_free(f->buf);
        ucrt_xp_free(f);
        return NULL;
    }

    f->fd = fd;
    f->bufpos = 0;
    f->buflen = 0;
    f->dir = STREAM_IDLE;
    f->eof = 0;
    f->error = 0;
    f->owns_fd = 1;
    f->unget_char = -1;
    return f;
}

__declspec(dllexport) int __cdecl ucrt_xp_setvbuf(
    UCRT_XP_FILE *f, char *buf, int mode, size_t size)
{
    /* mode: 0=_IOFBF full, 1=_IOLBF line, 2=_IONBF none.
     * This implementation supports full buffering and unbuffered;
     * line buffering is treated as full (adequate for most ports). */
    if (!validate_file(f)) return -1;
    if (f->dir == STREAM_WRITING) flush_write_buffer(f);
    f->bufpos = 0;
    f->buflen = 0;
    f->unget_char = -1;
    f->dir = STREAM_IDLE;

    if (mode == 2 /* _IONBF */) {
        /* Keep a 1-byte internal buffer so the rest of the code path
         * still works; every write/read effectively bypasses batching. */
        unsigned char *nb = (unsigned char *)ucrt_xp_malloc(1);
        if (!nb) return -1;
        ucrt_xp_free(f->buf);
        f->buf = nb;
        f->bufsize = 1;
        return 0;
    }

    if (size == 0) size = UCRT_XP_BUFSIZE;
    if (buf) {
        /* Caller-supplied buffer - we do not free it on fclose. To keep
         * ownership simple, copy into our own allocation of the same
         * size rather than adopting the pointer (avoids lifetime bugs). */
        unsigned char *nb = (unsigned char *)ucrt_xp_malloc(size);
        if (!nb) return -1;
        ucrt_xp_free(f->buf);
        f->buf = nb;
        f->bufsize = size;
        (void)buf;
    } else {
        unsigned char *nb = (unsigned char *)ucrt_xp_malloc(size);
        if (!nb) return -1;
        ucrt_xp_free(f->buf);
        f->buf = nb;
        f->bufsize = size;
    }
    return 0;
}

__declspec(dllexport) void __cdecl ucrt_xp_setbuf(UCRT_XP_FILE *f, char *buf)
{
    if (buf)
        ucrt_xp_setvbuf(f, buf, 0 /* _IOFBF */, UCRT_XP_BUFSIZE);
    else
        ucrt_xp_setvbuf(f, NULL, 2 /* _IONBF */, 0);
}

__declspec(dllexport) int __cdecl ucrt_xp_fseeki64(
    UCRT_XP_FILE *f, __int64 offset, int origin)
{
    __int64 r;
    if (!validate_file(f)) return -1;

    flush_write_buffer(f);
    f->bufpos = 0;
    f->buflen = 0;
    f->unget_char = -1;
    f->dir = STREAM_IDLE;
    f->eof = 0;

    r = ucrt_xp_lseek(f->fd, offset, origin);
    return (r < 0) ? -1 : 0;
}

__declspec(dllexport) __int64 __cdecl ucrt_xp_ftelli64(UCRT_XP_FILE *f)
{
    __int64 pos;
    if (!validate_file(f)) return -1;

    pos = ucrt_xp_lseek(f->fd, 0, UCRT_XP_SEEK_CUR);
    if (pos < 0) return -1;

    if (f->dir == STREAM_READING) {
        pos -= (__int64)(f->buflen - f->bufpos);
        if (f->unget_char >= 0) pos -= 1;
    } else if (f->dir == STREAM_WRITING) {
        pos += (__int64)f->bufpos;
    }
    return pos;
}

/* ------------------------------------------------------------------ */
/* Filesystem helpers (_access / _stat / _mkdir / _getcwd / _chdir)    */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_access(const char *path, int mode)
{
    DWORD attrs;
    if (!path) return -1;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return -1;
    /* mode: 0=exist, 2=write, 4=read, 6=read+write (MSVC _access). */
    if ((mode & 2) && (attrs & FILE_ATTRIBUTE_READONLY)) return -1;
    (void)mode; /* read bit is always ok if the file exists on Win32 */
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_mkdir(const char *path)
{
    if (!path) return -1;
    return CreateDirectoryA(path, NULL) ? 0 : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_chdir(const char *path)
{
    if (!path) return -1;
    return SetCurrentDirectoryA(path) ? 0 : -1;
}

__declspec(dllexport) char* __cdecl ucrt_xp_getcwd(char *buf, int maxlen)
{
    DWORD n;
    if (!buf || maxlen <= 0) return NULL;
    n = GetCurrentDirectoryA((DWORD)maxlen, buf);
    if (n == 0 || n >= (DWORD)maxlen) return NULL;
    return buf;
}

__declspec(dllexport) int __cdecl ucrt_xp_stat(const char *path, UCRT_XP_STAT *st)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ULARGE_INTEGER sz, ct, mt, at;
    SYSTEMTIME utc, local;
    FILETIME lft;

    if (!path || !st) return -1;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return -1;

    st->st_mode = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0040000 : 0100000;
    if (!(fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
        st->st_mode |= 0000200; /* write bit */
    st->st_mode |= 0000400; /* read bit */

    sz.LowPart  = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    st->st_size = (__int64)sz.QuadPart;

    /* Convert FILETIME (100ns since 1601) to approximate time_t (seconds
     * since 1970). Good enough for ported code checking mtime/size. */
    ct.LowPart = fad.ftCreationTime.dwLowDateTime;
    ct.HighPart = fad.ftCreationTime.dwHighDateTime;
    mt.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    mt.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    at.LowPart = fad.ftLastAccessTime.dwLowDateTime;
    at.HighPart = fad.ftLastAccessTime.dwHighDateTime;

    /* 116444736000000000 = 100ns intervals between 1601 and 1970 */
    st->st_ctime = (__int64)((ct.QuadPart - 116444736000000000ULL) / 10000000ULL);
    st->st_mtime = (__int64)((mt.QuadPart - 116444736000000000ULL) / 10000000ULL);
    st->st_atime = (__int64)((at.QuadPart - 116444736000000000ULL) / 10000000ULL);

    (void)utc; (void)local; (void)lft;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Standard streams (stdin/stdout/stderr)                              */
/* ------------------------------------------------------------------ */

/*
 * A prior pass of this project shipped fopen()/FILE* without ever
 * wrapping the process's own standard handles - meaning there was no
 * way to print anything without opening a named file, which is a real
 * gap for anything calling itself CRT-like. Fixed here.
 *
 * Lazily constructed (first call wins, via ucrt_xp_once) rather than
 * built at process-attach time, because GetStdHandle() can legitimately
 * return NULL/INVALID_HANDLE_VALUE for a GUI subsystem app with no
 * console - that's not an error this file should fail process startup
 * over, only something a caller discovers if/when it actually asks for
 * one of these streams.
 */

static UCRT_XP_FILE *g_stdin = NULL;
static UCRT_XP_FILE *g_stdout = NULL;
static UCRT_XP_FILE *g_stderr = NULL;
static UCRT_XP_ONCE g_std_streams_once = UCRT_XP_ONCE_INIT;

static UCRT_XP_FILE *wrap_std_handle(DWORD std_handle_id)
{
    HANDLE h = GetStdHandle(std_handle_id);
    int fd;

    if (h == NULL || h == INVALID_HANDLE_VALUE) return NULL;

    /* Text mode by default, matching the classic CRT's own default for
     * stdin/stdout/stderr. append_mode is always 0 here - reseek-before-
     * write makes no sense on a console/pipe handle and SetFilePointerEx
     * would simply fail on one anyway (harmlessly, since append_mode
     * being 0 means it's never called). */
    fd = ucrt_xp__register_fd(h, /*text_mode=*/1, /*append_mode=*/0);
    if (fd < 0) return NULL;

    return ucrt_xp__file_from_fd(fd);
}

static BOOL __cdecl init_std_streams(void *param)
{
    (void)param;
    g_stdin  = wrap_std_handle(STD_INPUT_HANDLE);
    g_stdout = wrap_std_handle(STD_OUTPUT_HANDLE);
    g_stderr = wrap_std_handle(STD_ERROR_HANDLE);
    return TRUE;
}

/*
 * Function-call accessors, not plain global-variable exports. This is
 * deliberate, not just a style choice: a `__declspec(dllexport)
 * UCRT_XP_FILE *ucrt_xp_stdout` data export would be read by the caller
 * at whatever moment it's first touched, with no way to guarantee lazy
 * initialization has run first - the same "initialization order across
 * a DLL boundary" hazard the rest of this project goes out of its way
 * to avoid (see ucrt_xp_once/the ABI-negotiation design). A function
 * call has none of that ambiguity: calling it IS the synchronization
 * point. Real CRTs converged on the same answer decades ago (compare
 * glibc's stdout being `(&_IO_2_1_stdout_)` vs. a modern MSVC CRT's
 * stdout being defined as a macro around __acrt_iob_func(1)).
 */
__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stdin(void)
{
    ucrt_xp_once(&g_std_streams_once, init_std_streams, NULL);
    return g_stdin;
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stdout(void)
{
    ucrt_xp_once(&g_std_streams_once, init_std_streams, NULL);
    return g_stdout;
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_stderr(void)
{
    ucrt_xp_once(&g_std_streams_once, init_std_streams, NULL);
    return g_stderr;
}

/* ------------------------------------------------------------------ */
/* stdout-targeted convenience functions                               */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_printf(const char *fmt, ...)
{
    UCRT_XP_FILE *f = ucrt_xp_stdout();
    char stackbuf[512];
    char *heapbuf = NULL;
    char *outbuf = stackbuf;
    int needed, written;
    va_list args;

    if (!f) return -1;

    va_start(args, fmt);
    needed = ucrt_xp_vsnprintf(stackbuf, sizeof(stackbuf), fmt, args);
    va_end(args);
    if (needed < 0) return -1;

    if ((size_t)needed >= sizeof(stackbuf)) {
        heapbuf = (char *)ucrt_xp_malloc((size_t)needed + 1);
        if (!heapbuf) return -1;
        va_start(args, fmt);
        ucrt_xp_vsnprintf(heapbuf, (size_t)needed + 1, fmt, args);
        va_end(args);
        outbuf = heapbuf;
    }

    written = (int)ucrt_xp_fwrite(outbuf, 1, (size_t)needed, f);
    if (heapbuf) ucrt_xp_free(heapbuf);
    return (written == needed) ? written : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_puts(const char *s)
{
    UCRT_XP_FILE *f = ucrt_xp_stdout();
    size_t len;
    if (!f || !s) return -1;

    len = ucrt_xp_strlen(s);
    if (ucrt_xp_fwrite(s, 1, len, f) != len) return -1;
    if (ucrt_xp_fputc('\n', f) < 0) return -1; /* puts() always appends \n */
    return 0; /* non-negative on success, matching the standard's contract */
}

__declspec(dllexport) int __cdecl ucrt_xp_putchar(int c)
{
    return ucrt_xp_fputc(c, ucrt_xp_stdout());
}

__declspec(dllexport) int __cdecl ucrt_xp_getchar(void)
{
    return ucrt_xp_fgetc(ucrt_xp_stdin());
}

/* ------------------------------------------------------------------ */
/* _findfirst / _findnext / _findclose                                 */
/* ------------------------------------------------------------------ */

typedef struct FindCtx {
    HANDLE h;
    int    first_done;
    WIN32_FIND_DATAA fd;
} FindCtx;

__declspec(dllexport) intptr_t __cdecl ucrt_xp_findfirst(const char *filespec, UCRT_XP_FINDDATA *data)
{
    FindCtx *ctx;
    if (!filespec || !data) return -1;
    ctx = (FindCtx *)ucrt_xp_malloc(sizeof(FindCtx));
    if (!ctx) return -1;
    ctx->h = FindFirstFileA(filespec, &ctx->fd);
    if (ctx->h == INVALID_HANDLE_VALUE) {
        ucrt_xp_free(ctx);
        return -1;
    }
    ctx->first_done = 0;
    /* Fill data from first entry */
    lstrcpynA(data->name, ctx->fd.cFileName, sizeof(data->name));
    data->attrib = ctx->fd.dwFileAttributes;
    data->size = ((__int64)ctx->fd.nFileSizeHigh << 32) | ctx->fd.nFileSizeLow;
    data->time_write = 0; /* simplified */
    ctx->first_done = 1;
    return (intptr_t)ctx;
}

__declspec(dllexport) int __cdecl ucrt_xp_findnext(intptr_t handle, UCRT_XP_FINDDATA *data)
{
    FindCtx *ctx = (FindCtx *)handle;
    if (!ctx || !data || ctx->h == INVALID_HANDLE_VALUE) return -1;
    if (!FindNextFileA(ctx->h, &ctx->fd)) return -1;
    lstrcpynA(data->name, ctx->fd.cFileName, sizeof(data->name));
    data->attrib = ctx->fd.dwFileAttributes;
    data->size = ((__int64)ctx->fd.nFileSizeHigh << 32) | ctx->fd.nFileSizeLow;
    data->time_write = 0;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_findclose(intptr_t handle)
{
    FindCtx *ctx = (FindCtx *)handle;
    if (!ctx) return -1;
    if (ctx->h != INVALID_HANDLE_VALUE) FindClose(ctx->h);
    ucrt_xp_free(ctx);
    return 0;
}

/* ------------------------------------------------------------------ */
/* fd extras: dup, setmode, osfhandle, eof, lseeki64 aliases           */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_dup(int fd)
{
    FdEntry *e = get_fd(fd);
    HANDLE hdup;
    if (!e) return -1;
    if (!DuplicateHandle(GetCurrentProcess(), e->handle,
                         GetCurrentProcess(), &hdup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return -1;
    return alloc_fd(hdup, e->text_mode, e->append_mode);
}

/* Grow the fd table so that slot `fd` exists (not necessarily in use). */
static int ensure_fd_slot(int fd)
{
    int need_blocks, b;
    if (fd < 0) return -1;
    ucrt_xp_once(&g_fd_once, init_fd_table, NULL);
    need_blocks = fd / UCRT_XP_FD_BLOCK + 1;
    EnterCriticalSection(&g_fd_lock);
    while (g_fd_block_count < need_blocks) {
        FdEntry *new_block = (FdEntry *)ucrt_xp_malloc(
            (size_t)UCRT_XP_FD_BLOCK * sizeof(FdEntry));
        FdEntry **grown;
        if (!new_block) { LeaveCriticalSection(&g_fd_lock); return -1; }
        ZeroMemory(new_block, (size_t)UCRT_XP_FD_BLOCK * sizeof(FdEntry));
        grown = (FdEntry **)ucrt_xp_realloc(
            g_fd_blocks, (size_t)(g_fd_block_count + 1) * sizeof(FdEntry *));
        if (!grown) {
            ucrt_xp_free(new_block);
            LeaveCriticalSection(&g_fd_lock);
            return -1;
        }
        grown[g_fd_block_count] = new_block;
        g_fd_blocks = grown;
        g_fd_block_count++;
    }
    (void)b;
    LeaveCriticalSection(&g_fd_lock);
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_dup2(int fd1, int fd2)
{
    FdEntry *e1;
    HANDLE hdup;
    int text_mode, append_mode;
    int b, i;

    if (fd2 < 0) return -1;
    if (fd1 == fd2) return fd2;

    e1 = get_fd(fd1);
    if (!e1) return -1;
    text_mode = e1->text_mode;
    append_mode = e1->append_mode;

    if (!DuplicateHandle(GetCurrentProcess(), e1->handle,
                         GetCurrentProcess(), &hdup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return -1;

    if (ensure_fd_slot(fd2) != 0) {
        CloseHandle(hdup);
        return -1;
    }

    b = fd2 / UCRT_XP_FD_BLOCK;
    i = fd2 % UCRT_XP_FD_BLOCK;

    EnterCriticalSection(&g_fd_lock);
    {
        FdEntry *slot = &g_fd_blocks[b][i];
        if (slot->in_use) {
            CloseHandle(slot->handle);
            slot->in_use = 0;
        }
        slot->handle = hdup;
        slot->in_use = 1;
        slot->text_mode = text_mode;
        slot->append_mode = append_mode;
    }
    LeaveCriticalSection(&g_fd_lock);
    return fd2;
}

__declspec(dllexport) int __cdecl ucrt_xp_setmode(int fd, int mode)
{
    FdEntry *e = get_fd(fd);
    int prev;
    if (!e) return -1;
    prev = e->text_mode ? UCRT_XP_O_TEXT : UCRT_XP_O_BINARY;
    if (mode & UCRT_XP_O_TEXT) e->text_mode = 1;
    else if (mode & UCRT_XP_O_BINARY) e->text_mode = 0;
    else return -1;
    return prev;
}

__declspec(dllexport) int __cdecl ucrt_xp_commit(int fd)
{
    FdEntry *e = get_fd(fd);
    if (!e) return -1;
    return FlushFileBuffers(e->handle) ? 0 : -1;
}

__declspec(dllexport) intptr_t __cdecl ucrt_xp_get_osfhandle(int fd)
{
    FdEntry *e = get_fd(fd);
    if (!e) return -1;
    return (intptr_t)e->handle;
}

__declspec(dllexport) int __cdecl ucrt_xp_open_osfhandle(intptr_t osfhandle, int flags)
{
    HANDLE h = (HANDLE)osfhandle;
    if (h == NULL || h == INVALID_HANDLE_VALUE) return -1;
    return alloc_fd(h,
                    (flags & UCRT_XP_O_TEXT) ? 1 : 0,
                    (flags & UCRT_XP_O_APPEND) ? 1 : 0);
}

__declspec(dllexport) int __cdecl ucrt_xp_eof(int fd)
{
    FdEntry *e = get_fd(fd);
    LARGE_INTEGER pos, size, zero;
    if (!e) return -1;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(e->handle, zero, &pos, FILE_CURRENT)) return -1;
    if (!GetFileSizeEx(e->handle, &size)) return -1;
    return pos.QuadPart >= size.QuadPart ? 1 : 0;
}

__declspec(dllexport) __int64 __cdecl ucrt_xp_lseeki64(int fd, __int64 offset, int origin)
{
    return ucrt_xp_lseek(fd, offset, origin);
}

__declspec(dllexport) __int64 __cdecl ucrt_xp_telli64(int fd)
{
    return ucrt_xp_lseek(fd, 0, UCRT_XP_SEEK_CUR);
}

__declspec(dllexport) long __cdecl ucrt_xp_tell(int fd)
{
    return (long)ucrt_xp_telli64(fd);
}

/* ------------------------------------------------------------------ */
/* fgetpos / fsetpos                                                   */
/* ------------------------------------------------------------------ */

__declspec(dllexport) int __cdecl ucrt_xp_fgetpos(UCRT_XP_FILE *f, __int64 *pos)
{
    __int64 p;
    if (!f || !pos || f->magic != UCRT_XP_FILE_MAGIC) return -1;
    p = ucrt_xp_ftelli64(f);
    if (p < 0) return -1;
    *pos = p;
    return 0;
}

__declspec(dllexport) int __cdecl ucrt_xp_fsetpos(UCRT_XP_FILE *f, const __int64 *pos)
{
    if (!f || !pos || f->magic != UCRT_XP_FILE_MAGIC) return -1;
    return ucrt_xp_fseeki64(f, *pos, UCRT_XP_SEEK_SET);
}

/* ------------------------------------------------------------------ */
/* Path helpers: fullpath, makepath, splitpath, rmdir, unlink          */
/* ------------------------------------------------------------------ */

__declspec(dllexport) char* __cdecl ucrt_xp_fullpath(char *absPath, const char *relPath, size_t maxLength)
{
    DWORD n;
    if (!relPath || !absPath || maxLength == 0) return NULL;
    n = GetFullPathNameA(relPath, (DWORD)maxLength, absPath, NULL);
    if (n == 0 || n >= maxLength) return NULL;
    return absPath;
}

__declspec(dllexport) void __cdecl ucrt_xp_splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *p = path ? path : "";
    const char *slash, *dot;
    size_t n;

    if (drive) drive[0] = 0;
    if (dir) dir[0] = 0;
    if (fname) fname[0] = 0;
    if (ext) ext[0] = 0;

    if (p[0] && p[1] == ':') {
        if (drive) { drive[0] = p[0]; drive[1] = ':'; drive[2] = 0; }
        p += 2;
    }
    slash = p;
    {
        const char *q;
        for (q = p; *q; q++)
            if (*q == '\\' || *q == '/') slash = q;
    }
    if (slash != p || *p == '\\' || *p == '/') {
        if (*slash == '\\' || *slash == '/') {
            n = (size_t)(slash - p) + 1;
            if (dir) { if (n > 255) n = 255; CopyMemory(dir, p, n); dir[n] = 0; }
            p = slash + 1;
        }
    }
    dot = ucrt_xp_strrchr(p, '.');
    if (dot && dot != p) {
        n = (size_t)(dot - p);
        if (fname) { if (n > 255) n = 255; CopyMemory(fname, p, n); fname[n] = 0; }
        if (ext) lstrcpynA(ext, dot, 256);
    } else {
        if (fname) lstrcpynA(fname, p, 256);
    }
}

__declspec(dllexport) void __cdecl ucrt_xp_makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
    char *p;
    if (!path) return;
    p = path; *p = 0;
    if (drive && drive[0]) {
        *p++ = drive[0];
        *p++ = ':';
        *p = 0;
    }
    if (dir && dir[0]) {
        lstrcatA(path, dir);
        {
            size_t L = ucrt_xp_strlen(path);
            if (L && path[L-1] != '\\' && path[L-1] != '/')
                lstrcatA(path, "\\");
        }
    }
    if (fname) lstrcatA(path, fname);
    if (ext && ext[0]) {
        if (ext[0] != '.') lstrcatA(path, ".");
        lstrcatA(path, ext[0] == '.' ? ext + 1 : ext);
    }
}

__declspec(dllexport) int __cdecl ucrt_xp_rmdir(const char *path)
{
    if (!path) return -1;
    return RemoveDirectoryA(path) ? 0 : -1;
}

__declspec(dllexport) int __cdecl ucrt_xp_unlink(const char *path)
{
    if (!path) return -1;
    return DeleteFileA(path) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* tmpnam / tmpfile                                                    */
/* ------------------------------------------------------------------ */

__declspec(dllexport) char* __cdecl ucrt_xp_tmpnam(char *s)
{
    static char static_buf[MAX_PATH];
    static unsigned counter = 0;
    char *out = s ? s : static_buf;
    char tmpdir[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmpdir);
    if (n == 0 || n >= MAX_PATH) return NULL;
    counter++;
    wsprintfA(out, "%sucrt%x%x.tmp", tmpdir,
              GetCurrentProcessId(), counter);
    return out;
}

__declspec(dllexport) UCRT_XP_FILE* __cdecl ucrt_xp_tmpfile(void)
{
    char name[MAX_PATH];
    UCRT_XP_FILE *f;
    if (!ucrt_xp_tmpnam(name)) return NULL;
    f = ucrt_xp_fopen(name, "w+b");
    if (f) DeleteFileA(name); /* unlinked on close when possible; XP may keep until fclose */
    return f;
}
