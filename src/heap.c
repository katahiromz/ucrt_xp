/*
 * heap.c - malloc/free implementation on top of a dedicated private heap
 * with the Low-Fragmentation Heap (LFH) policy explicitly enabled
 * (available since XP SP2 via HeapSetInformation; silently ignored / a
 * no-op-safe call on RTM where the constant still compiles but the OS
 * may return FALSE, which we treat as non-fatal).
 *
 * A small per-thread free-list cache is layered on top for the common
 * small-allocation, single-thread-dominant case, to reduce contention on
 * the process heap's internal lock relative to calling HeapAlloc directly
 * for every single malloc/free pair.
 */
#include "internal.h"

#ifndef HeapEnableTerminationOnCorruption
#define HeapEnableTerminationOnCorruption ((HEAP_INFORMATION_CLASS)1)
#endif
#ifndef HeapCompatibilityInformation
#define HeapCompatibilityInformation ((HEAP_INFORMATION_CLASS)0)
#endif

/* --- tiny per-thread cache for allocations <= UCRT_XP_SMALL_MAX -------- */

#define UCRT_XP_SMALL_MAX   256
#define UCRT_XP_CACHE_SLOTS 16   /* one freelist head per small size class */
#define UCRT_XP_CACHE_DEPTH 32   /* max cached blocks per size class/thread */

typedef struct FreeNode {
    struct FreeNode *next;
} FreeNode;

typedef struct ThreadCache {
    FreeNode *slots[UCRT_XP_CACHE_SLOTS];
    int       counts[UCRT_XP_CACHE_SLOTS];
} ThreadCache;

static DWORD g_tls_index = TLS_OUT_OF_INDEXES;

/* Block header so free() can find the real allocation size without the
 * caller passing it back in, and so we can tell cached vs. direct-heap
 * blocks apart. */
typedef struct BlockHeader {
    size_t size;      /* usable size requested by caller, rounded to class */
    int    slot;       /* -1 if not eligible for the thread cache */
} BlockHeader;

static int size_to_slot(size_t size)
{
    /* 16-byte size classes: 1..16 -> slot0, 17..32 -> slot1, etc. */
    if (size == 0 || size > UCRT_XP_SMALL_MAX) return -1;
    return (int)((size - 1) / 16);
}

static size_t slot_to_size(int slot)
{
    return (size_t)(slot + 1) * 16;
}

static ThreadCache *get_thread_cache(void)
{
    ThreadCache *tc;
    if (g_tls_index == TLS_OUT_OF_INDEXES) return NULL;

    tc = (ThreadCache *)TlsGetValue(g_tls_index);
    if (!tc) {
        tc = (ThreadCache *)HeapAlloc(g_ucrt_xp_heap, HEAP_ZERO_MEMORY, sizeof(ThreadCache));
        if (tc) {
            TlsSetValue(g_tls_index, tc);
        }
    }
    return tc;
}

__declspec(dllexport) BOOL __cdecl ucrt_xp_heap_init(void)
{
    if (g_ucrt_xp_heap) return TRUE;

    g_ucrt_xp_heap = HeapCreate(0, 0, 0);
    if (!g_ucrt_xp_heap) return FALSE;

    /* Best-effort: enable LFH. Not fatal if the OS refuses (e.g. RTM
     * without the relevant hotfix, or a heap with debug options set). */
    {
        ULONG lfh = 2; /* HEAP_LFH */
        HeapSetInformation(g_ucrt_xp_heap, HeapCompatibilityInformation,
                            &lfh, sizeof(lfh));
    }
    /* Best-effort: crash instead of silently corrupting on heap overrun. */
    HeapSetInformation(g_ucrt_xp_heap, HeapEnableTerminationOnCorruption, NULL, 0);

    if (g_tls_index == TLS_OUT_OF_INDEXES) {
        g_tls_index = TlsAlloc();
    }
    return TRUE;
}

__declspec(dllexport) void* __cdecl ucrt_xp_malloc(size_t size)
{
    int slot;
    BlockHeader *hdr;
    size_t total;

    if (!g_ucrt_xp_heap) {
        if (!ucrt_xp_heap_init()) return NULL;
    }
    if (size == 0) size = 1;

    slot = size_to_slot(size);
    if (slot >= 0) {
        ThreadCache *tc = get_thread_cache();
        if (tc && tc->slots[slot]) {
            FreeNode *node = tc->slots[slot];
            tc->slots[slot] = node->next;
            tc->counts[slot]--;
            /* node itself sits right after the BlockHeader in memory */
            return (void *)((char *)node);
        }
    }

    total = sizeof(BlockHeader) + (slot >= 0 ? slot_to_size(slot) : size);
    hdr = (BlockHeader *)HeapAlloc(g_ucrt_xp_heap, 0, total);
    if (!hdr) return NULL;
    hdr->size = (slot >= 0) ? slot_to_size(slot) : size;
    hdr->slot = slot;
    return (void *)(hdr + 1);
}

__declspec(dllexport) void* __cdecl ucrt_xp_calloc(size_t count, size_t size)
{
    size_t total;
    void *p;

    /* overflow check before multiplying */
    if (count != 0 && size > (SIZE_MAX / count)) {
        return NULL;
    }
    total = count * size;
    p = ucrt_xp_malloc(total);
    if (p) ZeroMemory(p, total);
    return p;
}

__declspec(dllexport) void* __cdecl ucrt_xp_realloc(void *ptr, size_t size)
{
    BlockHeader *hdr;
    void *newp;
    size_t old_size;

    if (!ptr) return ucrt_xp_malloc(size);
    if (size == 0) { ucrt_xp_free(ptr); return NULL; }

    hdr = ((BlockHeader *)ptr) - 1;
    old_size = hdr->size;
    if (size <= old_size) return ptr; /* shrink-in-place, keep it simple */

    newp = ucrt_xp_malloc(size);
    if (!newp) return NULL;
    CopyMemory(newp, ptr, old_size);
    ucrt_xp_free(ptr);
    return newp;
}

/* Called from DllMain(DLL_THREAD_DETACH / DLL_PROCESS_DETACH) to return a
 * thread's cached blocks to the real heap before the TLS slot is dropped,
 * otherwise every thread that ever malloc'd a small object leaks its
 * cache on exit. */
void ucrt_xp_heap_thread_cleanup(void)
{
    int i;
    ThreadCache *tc;
    if (g_tls_index == TLS_OUT_OF_INDEXES) return;

    tc = (ThreadCache *)TlsGetValue(g_tls_index);
    if (!tc) return;

    for (i = 0; i < UCRT_XP_CACHE_SLOTS; i++) {
        FreeNode *node = tc->slots[i];
        while (node) {
            FreeNode *next = node->next;
            HeapFree(g_ucrt_xp_heap, 0, ((BlockHeader *)node) - 1);
            node = next;
        }
    }
    HeapFree(g_ucrt_xp_heap, 0, tc);
    TlsSetValue(g_tls_index, NULL);
}

__declspec(dllexport) void __cdecl ucrt_xp_free(void *ptr)
{
    BlockHeader *hdr;
    if (!ptr) return;

    hdr = ((BlockHeader *)ptr) - 1;
    if (hdr->slot >= 0) {
        ThreadCache *tc = get_thread_cache();
        if (tc && tc->counts[hdr->slot] < UCRT_XP_CACHE_DEPTH) {
            FreeNode *node = (FreeNode *)ptr;
            node->next = tc->slots[hdr->slot];
            tc->slots[hdr->slot] = node;
            tc->counts[hdr->slot]++;
            return;
        }
    }
    HeapFree(g_ucrt_xp_heap, 0, hdr);
}
