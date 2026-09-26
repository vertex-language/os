// What every os package's C++ shares: error codes and their mapping,
// buffer filling, pipes, and the descriptor calls. Each os package has
// its own module for its own calls (os.env, os.process, ...), and imports
// this one.
//
// Nothing here waits on a descriptor. Pipes and signal listeners are
// non-blocking; a read that has nothing yet returns Err::wouldBlock, and
// the Vertex side waits through the runtime (vertex_task_wait_fd), so a
// waiting task parks and its thread runs others. A child's exit is a
// descriptor too (kqueue on macOS, a pidfd or a watcher's pipe on
// Android), which becomes readable when the child is done.
//
// Windows has no readiness for pipes. There, pollable() is 0, reads and
// waits block the calling thread, and the Vertex side calls them without
// waiting first -- slower, never wrong, as the runtime's own Windows
// fallback is.
//
// Strings in and out are UTF-8. A function that fills a caller's buffer
// returns the length the whole value needs (not counting the NUL); where
// that is more than the buffer holds, nothing useful was written and the
// caller asks again with a larger one.
module;
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <signal.h>
    #include <unistd.h>
#endif
export module os.sys;

// Error codes. Every call that can fail returns a negative one. sys.Code
// is the same set, for the Vertex side.
export namespace Err {
    constexpr int32_t ok = 0;
    constexpr int32_t generic = -1;
    constexpr int32_t notFound = -2;
    constexpr int32_t permission = -3;
    constexpr int32_t invalid = -4;
    constexpr int32_t wouldBlock = -5;
    constexpr int32_t unsupported = -6;
    constexpr int32_t noMemory = -7;
    constexpr int32_t interrupted = -8;
    constexpr int32_t brokenPipe = -9;
    constexpr int32_t notATerminal = -10;
}

// The portable signals. A listener's mask is (1 << kind) for each.
export namespace Sig {
    constexpr int32_t interrupt = 1;
    constexpr int32_t terminate = 2;
    constexpr int32_t hangup = 3;
    constexpr int32_t windowResize = 4;
    constexpr int32_t kill = 5;  // only as a thing to send, never listened for
}

// copyOutN writes n bytes of s into buf where they fit and returns the
// length the whole of it needs, which is the contract every
// buffer-filling call has.
export int32_t copyOutN(const char* s, size_t n, char* buf, int32_t max) noexcept {
    if (n > 0x7ffffffe) return Err::invalid;
    if (buf && max > 0 && n < (size_t)max) {
        memcpy(buf, s, n);
        buf[n] = 0;
    }
    return (int32_t)n;
}

// copyOut is copyOutN of a NUL-terminated s.
export int32_t copyOut(const char* s, char* buf, int32_t max) noexcept {
    return copyOutN(s, strlen(s), buf, max);
}

// mapError is the code for an OS error: an errno, or a GetLastError
// value on Windows.
export int32_t mapError(int64_t err) noexcept {
#if defined(_WIN32)
    switch ((DWORD)err) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_ENVVAR_NOT_FOUND:  return Err::notFound;
        case ERROR_ACCESS_DENIED:     return Err::permission;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:       return Err::noMemory;
        case ERROR_BROKEN_PIPE:
        case ERROR_NO_DATA:           return Err::brokenPipe;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:      return Err::invalid;
        default:                      return Err::generic;
    }
#else
    switch ((int)err) {
        case ENOENT:  return Err::notFound;
        case EACCES:
        case EPERM:   return Err::permission;
        case EINVAL:  return Err::invalid;
        case EAGAIN:  return Err::wouldBlock;
        case ENOMEM:  return Err::noMemory;
        case EINTR:   return Err::interrupted;
        case EPIPE:   return Err::brokenPipe;
        case ENOTTY:  return Err::notATerminal;
        case ESRCH:   return Err::notFound;
        default:      return Err::generic;
    }
#endif
}

// lastError is the last OS error code: errno, or GetLastError on Windows.
export int32_t lastError() noexcept {
#if defined(_WIN32)
    return (int32_t)GetLastError();
#else
    return errno;
#endif
}

// pollable is 1 where descriptors the os packages hand out can be waited
// on through the runtime, 0 where reads and waits block the thread
// instead (Windows).
export int32_t pollable() noexcept {
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

#if defined(_WIN32)

// narrowOut writes a UTF-16 string into buf as UTF-8.
export int32_t narrowOut(const wchar_t* w, int32_t wlen, char* buf, int32_t max) noexcept {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    if (n < 0) return Err::invalid;
    if (buf && max > 0 && n < max) {
        WideCharToMultiByte(CP_UTF8, 0, w, wlen, buf, n, nullptr, nullptr);
        buf[n] = 0;
    }
    return n;
}

// widen is a UTF-16 copy of a UTF-8 string, to be freed, or null.
export wchar_t* widen(const char* s) noexcept {
    if (!s) return nullptr;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return nullptr;
    wchar_t* p = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (p) MultiByteToWideChar(CP_UTF8, 0, s, -1, p, n);
    return p;
}

#else

// setFlags makes fd close-on-exec, and non-blocking where asked.
export bool setFlags(int32_t fd, bool nonblocking) noexcept {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return false;
    if (nonblocking) {
        int fl = fcntl(fd, F_GETFL);
        if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return false;
    }
    return true;
}

// makePipe is a pipe whose ends are close-on-exec; the parent's end is
// non-blocking, the child's is not (a child expects blocking stdio).
export bool makePipe(int32_t* fds, int32_t parentEnd) noexcept {
    if (pipe(fds) < 0) return false;
    if (!setFlags(fds[0], parentEnd == 0) || !setFlags(fds[1], parentEnd == 1)) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    return true;
}

// posixSignal is the POSIX number of a portable signal, or 0.
export int32_t posixSignal(int32_t kind) noexcept {
    switch (kind) {
        case Sig::interrupt:    return SIGINT;
        case Sig::terminate:    return SIGTERM;
        case Sig::hangup:       return SIGHUP;
        case Sig::windowResize: return SIGWINCH;
        case Sig::kill:         return SIGKILL;
        default:                return 0;
    }
}

#endif

// ---- Descriptors -------------------------------------------------------

// readFd is non-blocking where pollable is 1: Err::wouldBlock when
// nothing is ready. 0 is the end of the stream.
export int64_t readFd(int32_t fd, void* buf, int64_t count) noexcept {
#if defined(_WIN32)
    unsigned n = count > 0x7fffffff ? 0x7fffffff : (unsigned)count;
    int r = _read(fd, buf, n);
    if (r < 0) {
        // A pipe whose writer has gone reads as its end.
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : mapError(GetLastError());
    }
    return r;
#else
    for (;;) {
        ssize_t r = read(fd, buf, (size_t)count);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        if (errno == EWOULDBLOCK) return Err::wouldBlock;
        return mapError(errno);
    }
#endif
}

export int64_t writeFd(int32_t fd, const void* buf, int64_t count) noexcept {
#if defined(_WIN32)
    unsigned n = count > 0x7fffffff ? 0x7fffffff : (unsigned)count;
    int r = _write(fd, buf, n);
    return r < 0 ? mapError(GetLastError()) : r;
#else
    for (;;) {
        ssize_t r = write(fd, buf, (size_t)count);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        if (errno == EWOULDBLOCK) return Err::wouldBlock;
        return mapError(errno);
    }
#endif
}

export int32_t closeFd(int32_t fd) noexcept {
#if defined(_WIN32)
    return _close(fd) == 0 ? Err::ok : Err::generic;
#else
    return ::close(fd) == 0 ? Err::ok : mapError(errno);
#endif
}

// waitFd holds the thread until fd can be read (events 1) or written (2):
// for the synchronous standard streams, whose waiting is the point, where
// someone has made the descriptor non-blocking. 1 ready, or an error.
export int32_t waitFd(int32_t fd, int32_t events) noexcept {
#if defined(_WIN32)
    (void)fd;
    (void)events;
    return 1;  // Windows' standard handles block; there is nothing to wait for.
#else
    struct pollfd p = { fd, (short)(events == 2 ? POLLOUT : POLLIN), 0 };
    for (;;) {
        int n = poll(&p, 1, -1);
        if (n >= 0) return 1;
        if (errno != EINTR) return mapError(errno);
    }
#endif
}

// flushStdio writes out what C stdio holds for stdout and stderr -- where
// print's output waits -- so that a write straight to fd 1 or 2 comes
// after it.
export void flushStdio() noexcept {
    fflush(stdout);
    fflush(stderr);
}
