// The process environment, for package os/env.
module;
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <errno.h>
    #if defined(__APPLE__)
        #include <crt_externs.h>
    #endif
#endif
export module os.env;
import os.sys;

#if defined(_WIN32)
// The block of NAME=value strings Windows keeps, with the entries that
// start with '=' (per-drive current directories) passed over.
template <typename F>
int32_t eachEnv(F f) {
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) return mapError(GetLastError());
    int32_t i = 0;
    for (wchar_t* p = block; *p; p += wcslen(p) + 1) {
        if (*p == L'=') continue;
        if (f(i, p)) break;
        i++;
    }
    FreeEnvironmentStringsW(block);
    return i;
}
#else
char** environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    extern char** environ;
    return environ;
#endif
}
#endif

// envGet writes the value of name into buf, or is Err::notFound where it
// is unset.
export int32_t envGet(const char* name, char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    wchar_t* w = widen(name);
    if (!w) return Err::invalid;
    DWORD n = GetEnvironmentVariableW(w, nullptr, 0);
    if (n == 0) { free(w); return mapError(GetLastError()); }
    wchar_t* v = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!v) { free(w); return Err::noMemory; }
    DWORD got = GetEnvironmentVariableW(w, v, n);
    int32_t rc = narrowOut(v, (int32_t)got, buf, max);
    free(v);
    free(w);
    return rc;
#else
    const char* v = getenv(name);
    if (!v) return Err::notFound;
    return copyOut(v, buf, max);
#endif
}

export int32_t envSet(const char* name, const char* value) noexcept {
#if defined(_WIN32)
    wchar_t* n = widen(name);
    wchar_t* v = widen(value);
    int32_t rc = !n || !v ? Err::invalid
               : SetEnvironmentVariableW(n, v) ? Err::ok : mapError(GetLastError());
    free(n);
    free(v);
    return rc;
#else
    return setenv(name, value, 1) == 0 ? Err::ok : mapError(errno);
#endif
}

export int32_t envRemove(const char* name) noexcept {
#if defined(_WIN32)
    wchar_t* n = widen(name);
    if (!n) return Err::invalid;
    int32_t rc = Err::ok;
    if (!SetEnvironmentVariableW(n, nullptr)) {
        DWORD err = GetLastError();
        rc = err == ERROR_ENVVAR_NOT_FOUND ? Err::ok : mapError(err);
    }
    free(n);
    return rc;
#else
    return unsetenv(name) == 0 ? Err::ok : mapError(errno);
#endif
}

// The environment as a snapshot: envCount entries, then each "NAME=value".
export int32_t envCount() noexcept {
#if defined(_WIN32)
    return eachEnv([](int32_t, const wchar_t*) { return false; });
#else
    int32_t n = 0;
    for (char** e = environment(); e && *e; e++) n++;
    return n;
#endif
}

export int32_t envEntry(int32_t index, char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    int32_t rc = Err::notFound;
    eachEnv([&](int32_t i, const wchar_t* p) {
        if (i != index) return false;
        rc = narrowOut(p, (int32_t)wcslen(p), buf, max);
        return true;
    });
    return rc;
#else
    char** e = environment();
    for (int32_t i = 0; e && e[i]; i++) {
        if (i == index) return copyOut(e[i], buf, max);
    }
    return Err::notFound;
#endif
}
