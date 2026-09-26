// This process and the children it starts, for package os/process.
//
// Starting, waiting for and signalling a child is process_posix.cpp and
// process_windows.cpp; this unit declares them and has the rest.
module;
#include <stdint.h>
#include <stdlib.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <errno.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #if defined(__APPLE__)
        #include <mach-o/dyld.h>
    #endif
#endif
export module os.process;
import os.sys;

// What a child's standard stream is connected to, as Stdio's cases are.
export namespace StdioMode {
    constexpr int32_t inherit = 0;
    constexpr int32_t pipe = 1;
    constexpr int32_t discard = 2;
    constexpr int32_t file = 3;
}

// How a child ended, as tryWait reports it.
export namespace ExitKind {
    constexpr int32_t exited = 1;
    constexpr int32_t signaled = 2;
}

// killKind is the portable kind that stops a child at once: SIGKILL, or
// TerminateProcess on Windows. It is only sent, never listened for, so
// signal.Kind has no case for it.
export constexpr int32_t killKind = Sig::kill;

// ---- This process ------------------------------------------------------

export int32_t processId() noexcept {
#if defined(_WIN32)
    return (int32_t)GetCurrentProcessId();
#else
    return (int32_t)getpid();
#endif
}

export int32_t parentProcessId() noexcept {
#if defined(_WIN32)
    return 0;  // Windows keeps no parent a process can ask for cheaply.
#else
    return (int32_t)getppid();
#endif
}

export int32_t executablePath(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    wchar_t w[32768];
    DWORD n = GetModuleFileNameW(nullptr, w, 32768);
    if (n == 0) return mapError(GetLastError());
    return narrowOut(w, (int32_t)n, buf, max);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    char* raw = (char*)malloc(size + 1);
    if (!raw) return Err::noMemory;
    if (_NSGetExecutablePath(raw, &size) != 0) { free(raw); return Err::generic; }
    char* real = realpath(raw, nullptr);
    free(raw);
    if (!real) return mapError(errno);
    int32_t rc = copyOut(real, buf, max);
    free(real);
    return rc;
#else
    char tmp[4096];
    ssize_t n = readlink("/proc/self/exe", tmp, sizeof(tmp) - 1);
    if (n < 0) return mapError(errno);
    return copyOutN(tmp, (size_t)n, buf, max);
#endif
}

export int32_t currentDir(char* buf, int32_t max) noexcept {
#if defined(_WIN32)
    DWORD n = GetCurrentDirectoryW(0, nullptr);
    if (n == 0) return mapError(GetLastError());
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return Err::noMemory;
    DWORD got = GetCurrentDirectoryW(n, w);
    int32_t rc = narrowOut(w, (int32_t)got, buf, max);
    free(w);
    return rc;
#else
    char* d = getcwd(nullptr, 0);
    if (!d) return mapError(errno);
    int32_t rc = copyOut(d, buf, max);
    free(d);
    return rc;
#endif
}

export int32_t changeDir(const char* path) noexcept {
#if defined(_WIN32)
    wchar_t* w = widen(path);
    if (!w) return Err::invalid;
    int32_t rc = SetCurrentDirectoryW(w) ? Err::ok : mapError(GetLastError());
    free(w);
    return rc;
#else
    return chdir(path) == 0 ? Err::ok : mapError(errno);
#endif
}

// exitProcess ends the process, flushing C stdio (where print's output is
// buffered).
export [[noreturn]] void exitProcess(int32_t code) noexcept {
    exit(code);
}

// abortProcess ends the process now: nothing is flushed, nothing runs.
export [[noreturn]] void abortProcess() noexcept {
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), 134);
    for (;;) {}
#else
    _exit(134);
#endif
}

// isExecutable is 1 where path names a file this process may execute.
export int32_t isExecutable(const char* path) noexcept {
#if defined(_WIN32)
    wchar_t* w = widen(path);
    if (!w) return 0;
    DWORD attrs = GetFileAttributesW(w);
    free(w);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return access(path, X_OK) == 0 ? 1 : 0;
#endif
}

// ---- Children ----------------------------------------------------------

// spawn starts path with arguments and, unless envCount is -1, exactly
// the environment given. args and env are blobs of NUL-terminated
// strings, one after another; args includes argv[0]. dir may be null.
// modes are three StdioModes, for stdin, stdout and stderr, and files is
// a blob of three paths the same way, the i-th used where modes[i] is
// StdioMode::file.
//
// out is five: pid, stdin's write end, stdout's read end, stderr's read
// end (-1 where not piped), and the exit descriptor (-1 where the
// platform has none, which pollable says). Returns 0 or an error.
export int32_t spawn(const char* path,
                     const char* args, int32_t argCount,
                     const char* env, int32_t envCount,
                     const char* dir,
                     const int32_t* modes, const char* files,
                     int64_t* out) noexcept;

// tryWait reports whether the child has ended: 1 with kind and code
// filled, 0 where it is still running. block waits for it (only where
// there is no exit descriptor to wait on instead). Reaps the child; call
// it once it ends.
export int32_t tryWait(int64_t pid, int32_t exitFd, int32_t block,
                       int32_t* kind, int32_t* code) noexcept;

// sendSignal sends a portable signal kind or, where posix is non-zero,
// that POSIX signal number.
export int32_t sendSignal(int64_t pid, int32_t kind, int32_t posix) noexcept;

// releaseChild releases what is held for a child after it has been
// waited for, and closes its exit descriptor.
export void releaseChild(int64_t pid, int32_t exitFd) noexcept;
