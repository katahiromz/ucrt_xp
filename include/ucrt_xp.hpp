/*
 * ucrt_xp.hpp - thin RAII wrappers over ucrt_xp.h for C++ callers.
 *
 * Deliberately written against C++98/03, not C++11: no auto, nullptr,
 * =default/=delete, move semantics, or rvalue references, since this
 * project's whole premise is staying buildable under VC6 through
 * VC2010 - reaching for modern C++ here would be self-defeating. Copy
 * is disabled the old-fashioned way (private, unimplemented copy
 * ctor/assignment) where a type shouldn't be copied.
 *
 * These classes are optional sugar: everything here is a direct,
 * inlineable wrapper around the plain-C API in ucrt_xp.h. Nothing here
 * is exported from the DLL or crosses a module boundary as a C++ type
 * (per the design doc's own warning about C++ ABI differing across VC
 * versions) - only plain C structs/handles (UCRT_XP_FILE*,
 * ucrt_xp_locale_t, CRITICAL_SECTION) ever cross that boundary.
 */
#ifndef UCRT_XP_HPP
#define UCRT_XP_HPP

#include "ucrt_xp.h"

namespace ucrt_xp {

/* ------------------------------------------------------------------ */
/* File - RAII wrapper around UCRT_XP_FILE*                            */
/* ------------------------------------------------------------------ */
class File {
public:
    File() : f_(NULL) {}
    File(const char *path, const char *mode) : f_(NULL) { open(path, mode); }
    ~File() { close(); }

    bool open(const char *path, const char *mode)
    {
        close();
        f_ = ucrt_xp_fopen(path, mode);
        return f_ != NULL;
    }

    void close()
    {
        if (f_) { ucrt_xp_fclose(f_); f_ = NULL; }
    }

    bool is_open() const { return f_ != NULL; }

    size_t read(void *buf, size_t size, size_t count)
    {
        return f_ ? ucrt_xp_fread(buf, size, count, f_) : 0;
    }

    size_t write(const void *buf, size_t size, size_t count)
    {
        return f_ ? ucrt_xp_fwrite(buf, size, count, f_) : 0;
    }

    bool flush() { return f_ && ucrt_xp_fflush(f_) == 0; }
    bool seek(long offset, int origin) { return f_ && ucrt_xp_fseek(f_, offset, origin) == 0; }
    long tell() const { return f_ ? ucrt_xp_ftell(f_) : -1; }
    bool eof() const { return f_ ? (ucrt_xp_feof(f_) != 0) : true; }
    bool error() const { return f_ ? (ucrt_xp_ferror(f_) != 0) : true; }

    /* Escape hatch for printf and anything else not wrapped here -
     * ucrt_xp_fprintf(file.handle(), "%d", 42) works directly, and
     * variadic member functions can't forward ... portably pre-C++11
     * anyway, so this is the intended way to format into a File. */
    UCRT_XP_FILE *handle() const { return f_; }

    /* Non-owning views of the process's standard streams. Wrapping
     * these in File would be wrong (File::~File would close the
     * process's own stdout on scope exit) - these return a bare
     * UCRT_XP_FILE* for use with ucrt_xp_fprintf() etc. directly. */
    static UCRT_XP_FILE *in()  { return ucrt_xp_stdin();  }
    static UCRT_XP_FILE *out() { return ucrt_xp_stdout(); }
    static UCRT_XP_FILE *err() { return ucrt_xp_stderr(); }

private:
    UCRT_XP_FILE *f_;

    File(const File &);              /* not implemented: no copying */
    File &operator=(const File &);   /* not implemented: no copying */
};

/* ------------------------------------------------------------------ */
/* Lock / Guard - RAII critical section                                */
/* ------------------------------------------------------------------ */
class Lock {
public:
    Lock() { InitializeCriticalSection(&cs_); }
    ~Lock() { DeleteCriticalSection(&cs_); }

    void acquire() { EnterCriticalSection(&cs_); }
    void release() { LeaveCriticalSection(&cs_); }
    CRITICAL_SECTION *handle() { return &cs_; }

private:
    CRITICAL_SECTION cs_;

    Lock(const Lock &);
    Lock &operator=(const Lock &);
};

/* Scoped acquire/release, the usual "declare it and forget it" idiom. */
class Guard {
public:
    explicit Guard(Lock &lock) : lock_(lock) { lock_.acquire(); }
    ~Guard() { lock_.release(); }

private:
    Lock &lock_;

    Guard(const Guard &);
    Guard &operator=(const Guard &);
};

/* ------------------------------------------------------------------ */
/* CondVar - wraps UCRT_XP_COND, pairs with Lock                       */
/* ------------------------------------------------------------------ */
class CondVar {
public:
    CondVar() { ucrt_xp_cond_init(&cv_); }
    ~CondVar() { ucrt_xp_cond_destroy(&cv_); }

    /* Caller must hold lock.acquire() before calling wait(); wait()
     * releases it while blocked and re-acquires it before returning -
     * identical contract to the plain-C ucrt_xp_cond_wait(). */
    bool wait(Lock &lock, DWORD timeout_ms = INFINITE)
    {
        return ucrt_xp_cond_wait(&cv_, lock.handle(), timeout_ms) != 0;
    }

    void signal() { ucrt_xp_cond_signal(&cv_); }
    void broadcast() { ucrt_xp_cond_broadcast(&cv_); }

private:
    UCRT_XP_COND cv_;

    CondVar(const CondVar &);
    CondVar &operator=(const CondVar &);
};

/* ------------------------------------------------------------------ */
/* Locale - RAII refcounting around ucrt_xp_locale_t                   */
/* ------------------------------------------------------------------ */
class Locale {
public:
    explicit Locale(const char *name) : loc_(ucrt_xp_locale_create(name)) {}
    Locale(const Locale &other) : loc_(ucrt_xp_locale_addref(other.loc_)) {}

    Locale &operator=(const Locale &other)
    {
        if (this != &other) {
            ucrt_xp_locale_t old = loc_;
            loc_ = ucrt_xp_locale_addref(other.loc_);
            ucrt_xp_locale_release(old);
        }
        return *this;
    }

    ~Locale() { ucrt_xp_locale_release(loc_); }

    ucrt_xp_locale_t handle() const { return loc_; }

    /* Makes this locale the current thread's default for functions that
     * take an implicit (NULL) locale parameter, e.g. ucrt_xp_stricmp /
     * ucrt_xp_wcsicmp (and the _l variants when passed NULL). */
    void make_current() const { ucrt_xp_locale_set_thread(loc_); }

    int stricmp(const char *a, const char *b) const
    {
        return ucrt_xp_stricmp_l(a, b, loc_);
    }

    int wcsicmp(const wchar_t *a, const wchar_t *b) const
    {
        return ucrt_xp_wcsicmp_l(a, b, loc_);
    }

private:
    ucrt_xp_locale_t loc_;
};

} /* namespace ucrt_xp */

#endif /* UCRT_XP_HPP */
