// Signal listeners, for package os/signal.
//
// A listener is a descriptor that receives one byte, the kind, each time a
// signal in its mask arrives. While any listener wants a kind, its default
// action (end the process) does not happen.
module;
#include <stdint.h>
#include <string.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <errno.h>
    #include <pthread.h>
    #include <signal.h>
    #include <unistd.h>
#endif
export module os.signal;
import os.sys;

// A listener is the write end of a pipe and the kinds it wants. The
// handler walks the table and writes to each listener that wants the
// kind: write is async-signal-safe, and nothing else happens in it.
struct Listener { volatile int fd; volatile int mask; int readFd; };
Listener listeners[64];
int wanted[8];  // how many listeners want each kind

#if defined(_WIN32)
SRWLOCK signalLock = SRWLOCK_INIT;
void lockSignals() { AcquireSRWLockExclusive(&signalLock); }
void unlockSignals() { ReleaseSRWLockExclusive(&signalLock); }
#else
pthread_mutex_t signalLock = PTHREAD_MUTEX_INITIALIZER;
void lockSignals() { pthread_mutex_lock(&signalLock); }
void unlockSignals() { pthread_mutex_unlock(&signalLock); }
#endif

void deliver(int kind) {
    char b = (char)kind;
    for (auto& l : listeners) {
        int fd = l.fd;
        if (fd >= 0 && (l.mask & (1 << kind))) {
#if defined(_WIN32)
            _write(fd, &b, 1);
#else
            int saved = errno;
            (void)!write(fd, &b, 1);
            errno = saved;
#endif
        }
    }
}

#if defined(_WIN32)
BOOL WINAPI consoleHandler(DWORD type) {
    int kind = 0;
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:    kind = Sig::interrupt; break;
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT: kind = Sig::terminate; break;
    case CTRL_LOGOFF_EVENT:   kind = Sig::hangup; break;
    default: return FALSE;
    }
    if (wanted[kind] == 0) return FALSE;
    deliver(kind);
    return TRUE;
}
bool handlerInstalled = false;

void want(int kind, int delta) {
    wanted[kind] += delta;
    if (!handlerInstalled) {
        SetConsoleCtrlHandler(consoleHandler, TRUE);
        handlerInstalled = true;
    }
}
#else
void onSignal(int sig) {
    int kind = 0;
    switch (sig) {
    case SIGINT:   kind = Sig::interrupt; break;
    case SIGTERM:  kind = Sig::terminate; break;
    case SIGHUP:   kind = Sig::hangup; break;
    case SIGWINCH: kind = Sig::windowResize; break;
    default: return;
    }
    deliver(kind);
}

// want counts a listener in or out of a kind, and puts the handler in
// place at the first and the default action back at the last.
void want(int kind, int delta) {
    int sig = posixSignal(kind);
    if (sig == 0) return;
    int before = wanted[kind];
    wanted[kind] += delta;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    if (before == 0 && wanted[kind] > 0) {
        sa.sa_handler = onSignal;
        sa.sa_flags = SA_RESTART;
        sigaction(sig, &sa, nullptr);
    } else if (before > 0 && wanted[kind] == 0) {
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, nullptr);
    }
}
#endif

void initListeners() {
    static bool done = false;
    if (!done) {
        for (auto& l : listeners) { l.fd = -1; l.mask = 0; l.readFd = -1; }
        done = true;
    }
}

// listen returns a listener's descriptor for the kinds in mask, or an
// error.
export int32_t listen(int32_t mask) noexcept {
    if (mask == 0 || (mask & (1 << Sig::kill))) return Err::invalid;
    int fds[2];
#if defined(_WIN32)
    if (_pipe(fds, 256, _O_BINARY) != 0) return Err::generic;
#else
    if (!makePipe(fds, 0)) return mapError(errno);
    // The handler's end must not block either: a full pipe drops the
    // byte, and the listener has one waiting already.
    setFlags(fds[1], true);
#endif
    lockSignals();
    initListeners();
    Listener* slot = nullptr;
    for (auto& l : listeners) if (l.fd < 0) { slot = &l; break; }
    if (!slot) {
        unlockSignals();
        closeFd(fds[0]);
        closeFd(fds[1]);
        return Err::noMemory;
    }
    // The mask goes in before the descriptor, so the handler never sees
    // a descriptor with another listener's mask.
    slot->readFd = fds[0];
    slot->mask = mask;
    slot->fd = fds[1];
    for (int k = 1; k < 8; k++) if (mask & (1 << k)) want(k, 1);
    unlockSignals();
    return fds[0];
}

// unlisten ends the listener fd and closes it.
export void unlisten(int32_t fd) noexcept {
    lockSignals();
    initListeners();
    for (auto& l : listeners) {
        if (l.fd >= 0 && l.readFd == fd) {
            int mask = l.mask;
            int w = l.fd;
            l.fd = -1;
            l.mask = 0;
            for (int k = 1; k < 8; k++) if (mask & (1 << k)) want(k, -1);
            closeFd(w);
            break;
        }
    }
    unlockSignals();
    closeFd(fd);
}
