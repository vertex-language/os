#include "cos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #include <process.h>
    #include <direct.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <pthread.h>
    #include <pwd.h>
    #include <signal.h>
    #include <spawn.h>
    #include <termios.h>
    #include <time.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #if defined(__APPLE__)
        #include <crt_externs.h>
        #include <mach-o/dyld.h>
        #include <mach/mach.h>
        #include <sys/event.h>
        #include <sys/sysctl.h>
    #else
        #include <sys/syscall.h>
        #include <sys/sysinfo.h>
        #include <sys/system_properties.h>
    #endif
#endif

namespace {

// copyOut writes s into buf where it fits and returns the length the
// whole of it needs, which is the contract every buffer-filling call has.
int32_t copyOut(const char* s, size_t n, char* buf, int32_t max) {
    if (n > 0x7ffffffe) return COS_ERR_INVALID;
    if (buf && max > 0 && n < (size_t)max) {
        memcpy(buf, s, n);
        buf[n] = 0;
    }
    return (int32_t)n;
}

int32_t copyOut(const char* s, char* buf, int32_t max) {
    return copyOut(s, strlen(s), buf, max);
}

#if defined(_WIN32)

int32_t mapError(DWORD err) {
    switch (err) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_ENVVAR_NOT_FOUND:  return COS_ERR_NOT_FOUND;
        case ERROR_ACCESS_DENIED:     return COS_ERR_PERMISSION;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:       return COS_ERR_NO_MEMORY;
        case ERROR_BROKEN_PIPE:
        case ERROR_NO_DATA:           return COS_ERR_BROKEN_PIPE;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:      return COS_ERR_INVALID;
        default:                      return COS_ERR_GENERIC;
    }
}

// Wide is a UTF-16 copy of a UTF-8 string, freed when it goes.
struct Wide {
    wchar_t* p = nullptr;
    explicit Wide(const char* s) {
        if (!s) return;
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 0) return;
        p = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
        if (p) MultiByteToWideChar(CP_UTF8, 0, s, -1, p, n);
    }
    ~Wide() { free(p); }
    Wide(const Wide&) = delete;
    Wide& operator=(const Wide&) = delete;
};

// narrowOut writes a UTF-16 string into buf as UTF-8.
int32_t narrowOut(const wchar_t* w, int wlen, char* buf, int32_t max) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    if (n < 0) return COS_ERR_INVALID;
    if (buf && max > 0 && n < max) {
        WideCharToMultiByte(CP_UTF8, 0, w, wlen, buf, n, nullptr, nullptr);
        buf[n] = 0;
    }
    return n;
}

#else

int32_t mapError(int err) {
    switch (err) {
        case ENOENT:  return COS_ERR_NOT_FOUND;
        case EACCES:
        case EPERM:   return COS_ERR_PERMISSION;
        case EINVAL:  return COS_ERR_INVALID;
        case EAGAIN:  return COS_ERR_WOULD_BLOCK;
        case ENOMEM:  return COS_ERR_NO_MEMORY;
        case EINTR:   return COS_ERR_INTERRUPTED;
        case EPIPE:   return COS_ERR_BROKEN_PIPE;
        case ENOTTY:  return COS_ERR_NOT_A_TERMINAL;
        case ESRCH:   return COS_ERR_NOT_FOUND;
        default:      return COS_ERR_GENERIC;
    }
}

char** environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    extern char** environ;
    return environ;
#endif
}

// Fd closes a descriptor on every way out of the scope that holds it,
// unless release hands it on.
struct Fd {
    int fd = -1;
    Fd() = default;
    explicit Fd(int f) : fd(f) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    int release() { int f = fd; fd = -1; return f; }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

bool setFlags(int fd, bool nonblocking) {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return false;
    if (nonblocking) {
        int fl = fcntl(fd, F_GETFL);
        if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) return false;
    }
    return true;
}

// makePipe is a pipe whose ends are close-on-exec; the parent's end is
// non-blocking, the child's is not (a child expects blocking stdio).
bool makePipe(int fds[2], int parentEnd) {
    if (pipe(fds) < 0) return false;
    if (!setFlags(fds[0], parentEnd == 0) || !setFlags(fds[1], parentEnd == 1)) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    return true;
}

// splitBlob turns count NUL-terminated strings laid end to end into a
// NULL-terminated array of pointers into the blob.
char** splitBlob(const char* blob, int32_t count) {
    char** out = (char**)calloc((size_t)count + 1, sizeof(char*));
    if (!out) return nullptr;
    const char* p = blob;
    for (int32_t i = 0; i < count; i++) {
        out[i] = (char*)p;
        p += strlen(p) + 1;
    }
    return out;
}

int posixSignal(int32_t kind) {
    switch (kind) {
        case COS_SIG_INTERRUPT:     return SIGINT;
        case COS_SIG_TERMINATE:     return SIGTERM;
        case COS_SIG_HANGUP:        return SIGHUP;
        case COS_SIG_WINDOW_RESIZE: return SIGWINCH;
        case COS_SIG_KILL:          return SIGKILL;
        default:                    return 0;
    }
}

#endif

} // namespace

extern "C" {

int32_t cos_last_error(void) {
#if defined(_WIN32)
    return (int32_t)GetLastError();
#else
    return errno;
#endif
}

int32_t cos_pollable(void) {
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

// ---- Environment -------------------------------------------------------

int32_t cos_env_get(const char* name, char* buf, int32_t max) {
#if defined(_WIN32)
    Wide w(name);
    if (!w.p) return COS_ERR_INVALID;
    DWORD n = GetEnvironmentVariableW(w.p, nullptr, 0);
    if (n == 0) return mapError(GetLastError());
    wchar_t* v = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!v) return COS_ERR_NO_MEMORY;
    DWORD got = GetEnvironmentVariableW(w.p, v, n);
    int32_t rc = narrowOut(v, (int)got, buf, max);
    free(v);
    return rc;
#else
    const char* v = getenv(name);
    if (!v) return COS_ERR_NOT_FOUND;
    return copyOut(v, buf, max);
#endif
}

int32_t cos_env_set(const char* name, const char* value) {
#if defined(_WIN32)
    Wide n(name), v(value);
    if (!n.p || !v.p) return COS_ERR_INVALID;
    return SetEnvironmentVariableW(n.p, v.p) ? COS_OK : mapError(GetLastError());
#else
    return setenv(name, value, 1) == 0 ? COS_OK : mapError(errno);
#endif
}

int32_t cos_env_remove(const char* name) {
#if defined(_WIN32)
    Wide n(name);
    if (!n.p) return COS_ERR_INVALID;
    if (SetEnvironmentVariableW(n.p, nullptr)) return COS_OK;
    DWORD err = GetLastError();
    return err == ERROR_ENVVAR_NOT_FOUND ? COS_OK : mapError(err);
#else
    return unsetenv(name) == 0 ? COS_OK : mapError(errno);
#endif
}

#if defined(_WIN32)
namespace {
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
}
#endif

int32_t cos_env_count(void) {
#if defined(_WIN32)
    return eachEnv([](int32_t, const wchar_t*) { return false; });
#else
    int32_t n = 0;
    for (char** e = environment(); e && *e; e++) n++;
    return n;
#endif
}

int32_t cos_env_entry(int32_t index, char* buf, int32_t max) {
#if defined(_WIN32)
    int32_t rc = COS_ERR_NOT_FOUND;
    eachEnv([&](int32_t i, const wchar_t* p) {
        if (i != index) return false;
        rc = narrowOut(p, (int)wcslen(p), buf, max);
        return true;
    });
    return rc;
#else
    char** e = environment();
    for (int32_t i = 0; e && e[i]; i++) {
        if (i == index) return copyOut(e[i], buf, max);
    }
    return COS_ERR_NOT_FOUND;
#endif
}

// ---- This process ------------------------------------------------------

int32_t cos_pid(void) {
#if defined(_WIN32)
    return (int32_t)GetCurrentProcessId();
#else
    return (int32_t)getpid();
#endif
}

int32_t cos_ppid(void) {
#if defined(_WIN32)
    return 0;  // Windows keeps no parent a process can ask for cheaply.
#else
    return (int32_t)getppid();
#endif
}

int32_t cos_executable_path(char* buf, int32_t max) {
#if defined(_WIN32)
    wchar_t w[32768];
    DWORD n = GetModuleFileNameW(nullptr, w, 32768);
    if (n == 0) return mapError(GetLastError());
    return narrowOut(w, (int)n, buf, max);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    char* raw = (char*)malloc(size + 1);
    if (!raw) return COS_ERR_NO_MEMORY;
    if (_NSGetExecutablePath(raw, &size) != 0) { free(raw); return COS_ERR_GENERIC; }
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
    return copyOut(tmp, (size_t)n, buf, max);
#endif
}

int32_t cos_current_dir(char* buf, int32_t max) {
#if defined(_WIN32)
    DWORD n = GetCurrentDirectoryW(0, nullptr);
    if (n == 0) return mapError(GetLastError());
    wchar_t* w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return COS_ERR_NO_MEMORY;
    DWORD got = GetCurrentDirectoryW(n, w);
    int32_t rc = narrowOut(w, (int)got, buf, max);
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

int32_t cos_set_current_dir(const char* path) {
#if defined(_WIN32)
    Wide w(path);
    if (!w.p) return COS_ERR_INVALID;
    return SetCurrentDirectoryW(w.p) ? COS_OK : mapError(GetLastError());
#else
    return chdir(path) == 0 ? COS_OK : mapError(errno);
#endif
}

void cos_exit(int32_t code) {
    exit(code);
}

void cos_abort(void) {
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), 134);
    for (;;) {}
#else
    _exit(134);
#endif
}

int32_t cos_is_executable(const char* path) {
#if defined(_WIN32)
    Wide w(path);
    if (!w.p) return 0;
    DWORD attrs = GetFileAttributesW(w.p);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return access(path, X_OK) == 0 ? 1 : 0;
#endif
}

// ---- Child processes ---------------------------------------------------

#if !defined(_WIN32)

#if !defined(__APPLE__)
namespace {
// Where there is no pidfd, a thread waits for the child and writes a
// byte to a pipe when it has ended. It only watches: waitid with WNOWAIT
// leaves the child to be reaped by cos_try_wait.
struct Watch { pid_t pid; int fd; };
void* watcher(void* arg) {
    Watch* w = (Watch*)arg;
    siginfo_t info;
    while (waitid(P_PID, (id_t)w->pid, &info, WEXITED | WNOWAIT) < 0 && errno == EINTR) {}
    char b = 1;
    (void)!write(w->fd, &b, 1);
    ::close(w->fd);
    free(w);
    return nullptr;
}

int exitDescriptor(pid_t pid) {
#if defined(SYS_pidfd_open)
    long fd = syscall(SYS_pidfd_open, pid, 0);
#else
    long fd = syscall(434, pid, 0);  // pidfd_open, Linux 5.3 and later
#endif
    if (fd >= 0) {
        setFlags((int)fd, false);
        return (int)fd;
    }
    int fds[2];
    if (!makePipe(fds, 0)) return -1;
    Watch* w = (Watch*)malloc(sizeof(Watch));
    if (!w) { ::close(fds[0]); ::close(fds[1]); return -1; }
    w->pid = pid;
    w->fd = fds[1];
    pthread_t t;
    if (pthread_create(&t, nullptr, watcher, w) != 0) {
        free(w);
        ::close(fds[0]);
        ::close(fds[1]);
        return -1;
    }
    pthread_detach(t);
    return fds[0];
}
}
#endif

int32_t cos_spawn(const char* path,
                  const char* args, int32_t arg_count,
                  const char* env, int32_t env_count,
                  const char* dir,
                  const int32_t* modes, const char* filesBlob,
                  int64_t* out) {
    for (int i = 0; i < 5; i++) out[i] = -1;
    const char* files[3];
    files[0] = filesBlob;
    files[1] = files[0] + strlen(files[0]) + 1;
    files[2] = files[1] + strlen(files[1]) + 1;

    // The ends the child gets, and the ends the parent keeps.
    Fd child[3], parent[3];
    for (int i = 0; i < 3; i++) {
        switch (modes[i]) {
        case COS_STDIO_PIPE: {
            int fds[2];
            // stdin: the child reads fds[0], the parent writes fds[1].
            if (!makePipe(fds, i == 0 ? 1 : 0)) return mapError(errno);
            child[i].fd = i == 0 ? fds[0] : fds[1];
            parent[i].fd = i == 0 ? fds[1] : fds[0];
            break;
        }
        case COS_STDIO_NULL: {
            int f = open("/dev/null", i == 0 ? O_RDONLY : O_WRONLY);
            if (f < 0) return mapError(errno);
            child[i].fd = f;
            break;
        }
        case COS_STDIO_FILE: {
            int flags = i == 0 ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
            int f = open(files[i], flags, 0666);
            if (f < 0) return mapError(errno);
            child[i].fd = f;
            break;
        }
        default:
            break;
        }
    }

    char** argv = splitBlob(args, arg_count);
    char** envp = env_count >= 0 ? splitBlob(env, env_count) : nullptr;
    if (!argv || (env_count >= 0 && !envp)) {
        free(argv);
        free(envp);
        return COS_ERR_NO_MEMORY;
    }

    pid_t pid = -1;
    int err = 0;

#if defined(__APPLE__)
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_init(&attr);
    for (int i = 0; i < 3; i++) {
        if (child[i].fd >= 0) {
            posix_spawn_file_actions_adddup2(&fa, child[i].fd, i);
        } else {
            posix_spawn_file_actions_addinherit_np(&fa, i);
        }
    }
    if (dir) posix_spawn_file_actions_addchdir_np(&fa, dir);
    // Only 0, 1 and 2 reach the child, whatever else the parent has open.
    // The child starts with default signal handling and an empty mask,
    // whatever this process listens for.
    sigset_t all, none;
    sigfillset(&all);
    sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attr, &all);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT |
                                    POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    err = posix_spawn(&pid, path, &fa, &attr, argv, envp ? envp : environment());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
#else
    // Android: fork and exec at once, with nothing between but the
    // descriptors, the directory and the signal state -- all of it
    // async-signal-safe. A failed exec reports its errno through a
    // close-on-exec pipe, which a successful one closes.
    int report[2];
    if (!makePipe(report, 0)) { free(argv); free(envp); return mapError(errno); }
    pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 3; i++) {
            if (child[i].fd >= 0) dup2(child[i].fd, i);
        }
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int s = 1; s < 32; s++) signal(s, SIG_DFL);
        if (dir && chdir(dir) != 0) {
            int e = errno;
            (void)!write(report[1], &e, sizeof(e));
            _exit(127);
        }
        execve(path, argv, envp ? envp : environment());
        int e = errno;
        (void)!write(report[1], &e, sizeof(e));
        _exit(127);
    }
    ::close(report[1]);
    if (pid < 0) {
        err = errno;
    } else {
        int e = 0;
        ssize_t n;
        // The report end was made non-blocking for the parent; the child
        // is about to exec or fail, so wait for one or the other.
        struct pollfd p = { report[0], POLLIN, 0 };
        while (poll(&p, 1, -1) < 0 && errno == EINTR) {}
        while ((n = read(report[0], &e, sizeof(e))) < 0 && errno == EINTR) {}
        if (n == (ssize_t)sizeof(e)) {
            err = e;
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
    }
    ::close(report[0]);
#endif

    free(argv);
    free(envp);
    if (err != 0 || pid < 0) return mapError(err ? err : errno);

    out[0] = pid;
    for (int i = 0; i < 3; i++) out[i + 1] = parent[i].release();

#if defined(__APPLE__)
    int kq = kqueue();
    if (kq >= 0) {
        setFlags(kq, false);
        struct kevent ev;
        EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
            // ESRCH: it has ended already. The descriptor is still one to
            // wait on; cos_try_wait finds it done without waiting.
            ::close(kq);
            kq = -1;
        }
    }
    out[4] = kq;
#else
    out[4] = exitDescriptor(pid);
#endif
    return COS_OK;
}

int32_t cos_try_wait(int64_t pid, int32_t exit_fd, int32_t block,
                     int32_t* kind, int32_t* code) {
    (void)exit_fd;
    int status = 0;
    pid_t r;
    while ((r = waitpid((pid_t)pid, &status, block ? 0 : WNOHANG)) < 0 && errno == EINTR) {}
    if (r < 0) return mapError(errno);
    if (r == 0) return 0;
    if (WIFEXITED(status)) {
        *kind = COS_EXIT_EXITED;
        *code = WEXITSTATUS(status);
    } else {
        *kind = COS_EXIT_SIGNALED;
        *code = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    }
    return 1;
}

int32_t cos_send_signal(int64_t pid, int32_t kind, int32_t posix) {
    int sig = posix != 0 ? posix : posixSignal(kind);
    if (sig == 0) return COS_ERR_INVALID;
    return kill((pid_t)pid, sig) == 0 ? COS_OK : mapError(errno);
}

void cos_release_child(int64_t pid, int32_t exit_fd) {
    (void)pid;
    if (exit_fd >= 0) ::close(exit_fd);
}

#else // _WIN32

namespace {

// quoteArg appends one argument to a command line the way
// CommandLineToArgvW and the C runtime read it back: quoted where it has
// a space, a tab or a quote in it (or is empty), with backslashes doubled
// only where a quote follows them.
void quoteArg(wchar_t*& out, const wchar_t* a) {
    bool quote = *a == 0 || wcspbrk(a, L" \t\"") != nullptr;
    if (!quote) {
        while (*a) *out++ = *a++;
        return;
    }
    *out++ = L'"';
    for (;;) {
        size_t slashes = 0;
        while (*a == L'\\') { a++; slashes++; }
        if (*a == 0) {
            for (size_t i = 0; i < slashes * 2; i++) *out++ = L'\\';
            break;
        }
        if (*a == L'"') {
            for (size_t i = 0; i < slashes * 2 + 1; i++) *out++ = L'\\';
            *out++ = *a++;
            continue;
        }
        for (size_t i = 0; i < slashes; i++) *out++ = L'\\';
        *out++ = *a++;
    }
    *out++ = L'"';
}

// The children this process started, by pid, with their handles: a pid
// is what crosses the boundary, and the handle is what Windows waits on.
struct ChildHandle { DWORD pid; HANDLE h; };
ChildHandle children[256];
SRWLOCK childrenLock = SRWLOCK_INIT;

HANDLE handleOf(int64_t pid) {
    AcquireSRWLockShared(&childrenLock);
    HANDLE h = nullptr;
    for (auto& c : children) {
        if (c.h && c.pid == (DWORD)pid) { h = c.h; break; }
    }
    ReleaseSRWLockShared(&childrenLock);
    return h;
}

}

int32_t cos_spawn(const char* path,
                  const char* args, int32_t arg_count,
                  const char* env, int32_t env_count,
                  const char* dir,
                  const int32_t* modes, const char* filesBlob,
                  int64_t* out) {
    for (int i = 0; i < 5; i++) out[i] = -1;
    const char* files[3];
    files[0] = filesBlob;
    files[1] = files[0] + strlen(files[0]) + 1;
    files[2] = files[1] + strlen(files[1]) + 1;

    // The command line: every argument quoted as the child's C runtime
    // will split it again.
    size_t cap = 1;
    const char* p = args;
    for (int32_t i = 0; i < arg_count; i++) { size_t n = strlen(p); cap += n * 2 + 3; p += n + 1; }
    wchar_t* cmd = (wchar_t*)calloc(cap, sizeof(wchar_t));
    if (!cmd) return COS_ERR_NO_MEMORY;
    wchar_t* w = cmd;
    p = args;
    for (int32_t i = 0; i < arg_count; i++) {
        Wide a(p);
        if (i > 0) *w++ = L' ';
        quoteArg(w, a.p ? a.p : L"");
        p += strlen(p) + 1;
    }
    *w = 0;

    // The environment block: NAME=value\0 ... \0, in UTF-16.
    wchar_t* envBlock = nullptr;
    if (env_count >= 0) {
        size_t total = 2;
        const char* e = env;
        for (int32_t i = 0; i < env_count; i++) { size_t n = strlen(e); total += n + 1; e += n + 1; }
        envBlock = (wchar_t*)calloc(total, sizeof(wchar_t));
        if (!envBlock) { free(cmd); return COS_ERR_NO_MEMORY; }
        wchar_t* q = envBlock;
        e = env;
        for (int32_t i = 0; i < env_count; i++) {
            Wide v(e);
            if (v.p) { size_t n = wcslen(v.p); memcpy(q, v.p, n * sizeof(wchar_t)); q += n + 1; }
            e += strlen(e) + 1;
        }
    }

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE childEnd[3] = { nullptr, nullptr, nullptr };
    HANDLE parentEnd[3] = { nullptr, nullptr, nullptr };
    DWORD stdIds[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    int32_t rc = COS_OK;
    for (int i = 0; i < 3 && rc == COS_OK; i++) {
        switch (modes[i]) {
        case COS_STDIO_PIPE: {
            HANDLE r, wr;
            if (!CreatePipe(&r, &wr, &sa, 0)) { rc = mapError(GetLastError()); break; }
            childEnd[i] = i == 0 ? r : wr;
            parentEnd[i] = i == 0 ? wr : r;
            SetHandleInformation(parentEnd[i], HANDLE_FLAG_INHERIT, 0);
            break;
        }
        case COS_STDIO_NULL:
        case COS_STDIO_FILE: {
            Wide name(modes[i] == COS_STDIO_NULL ? "NUL" : files[i]);
            DWORD access = i == 0 ? GENERIC_READ : GENERIC_WRITE;
            DWORD disp = i == 0 ? OPEN_EXISTING : CREATE_ALWAYS;
            HANDLE h = CreateFileW(name.p, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &sa, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { rc = mapError(GetLastError()); break; }
            childEnd[i] = h;
            break;
        }
        default: {
            HANDLE h = GetStdHandle(stdIds[i]);
            HANDLE dup = nullptr;
            if (h && h != INVALID_HANDLE_VALUE) {
                DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE,
                                DUPLICATE_SAME_ACCESS);
            }
            childEnd[i] = dup;
            break;
        }
        }
    }

    PROCESS_INFORMATION pi = {};
    if (rc == COS_OK) {
        // Only the three handles are inherited, whatever else is open and
        // inheritable in this process.
        HANDLE list[3];
        DWORD listCount = 0;
        for (int i = 0; i < 3; i++) if (childEnd[i]) list[listCount++] = childEnd[i];

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = childEnd[0];
        si.StartupInfo.hStdOutput = childEnd[1];
        si.StartupInfo.hStdError = childEnd[2];
        DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
        if (attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &size) &&
            UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      list, listCount * sizeof(HANDLE), nullptr, nullptr)) {
            si.lpAttributeList = attrs;
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        }
        Wide app(path), cwd(dir);
        if (!CreateProcessW(app.p, cmd, nullptr, nullptr, TRUE, flags, envBlock,
                            dir ? cwd.p : nullptr, &si.StartupInfo, &pi)) {
            rc = mapError(GetLastError());
        }
        if (si.lpAttributeList) DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }

    for (int i = 0; i < 3; i++) if (childEnd[i]) CloseHandle(childEnd[i]);
    free(cmd);
    free(envBlock);
    if (rc != COS_OK) {
        for (int i = 0; i < 3; i++) if (parentEnd[i]) CloseHandle(parentEnd[i]);
        return rc;
    }

    CloseHandle(pi.hThread);
    AcquireSRWLockExclusive(&childrenLock);
    bool kept = false;
    for (auto& c : children) {
        if (!c.h) { c.pid = pi.dwProcessId; c.h = pi.hProcess; kept = true; break; }
    }
    ReleaseSRWLockExclusive(&childrenLock);
    if (!kept) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        for (int i = 0; i < 3; i++) if (parentEnd[i]) CloseHandle(parentEnd[i]);
        return COS_ERR_NO_MEMORY;
    }

    out[0] = pi.dwProcessId;
    for (int i = 0; i < 3; i++) {
        if (parentEnd[i]) {
            out[i + 1] = _open_osfhandle((intptr_t)parentEnd[i], i == 0 ? _O_WRONLY : _O_RDONLY);
        }
    }
    out[4] = -1;
    return COS_OK;
}

int32_t cos_try_wait(int64_t pid, int32_t exit_fd, int32_t block,
                     int32_t* kind, int32_t* code) {
    (void)exit_fd;
    HANDLE h = handleOf(pid);
    if (!h) return COS_ERR_NOT_FOUND;
    DWORD r = WaitForSingleObject(h, block ? INFINITE : 0);
    if (r == WAIT_TIMEOUT) return 0;
    if (r != WAIT_OBJECT_0) return mapError(GetLastError());
    DWORD status = 0;
    GetExitCodeProcess(h, &status);
    *kind = COS_EXIT_EXITED;
    *code = (int32_t)status;
    return 1;
}

int32_t cos_send_signal(int64_t pid, int32_t kind, int32_t posix) {
    if (posix != 0) return COS_ERR_UNSUPPORTED;
    switch (kind) {
    case COS_SIG_INTERRUPT:
    case COS_SIG_TERMINATE:
        // The child is its own process group, so this reaches it alone.
        return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid)
            ? COS_OK : mapError(GetLastError());
    case COS_SIG_KILL: {
        HANDLE h = handleOf(pid);
        if (!h) return COS_ERR_NOT_FOUND;
        return TerminateProcess(h, 1) ? COS_OK : mapError(GetLastError());
    }
    default:
        return COS_ERR_UNSUPPORTED;
    }
}

void cos_release_child(int64_t pid, int32_t exit_fd) {
    (void)exit_fd;
    AcquireSRWLockExclusive(&childrenLock);
    for (auto& c : children) {
        if (c.h && c.pid == (DWORD)pid) { CloseHandle(c.h); c.h = nullptr; break; }
    }
    ReleaseSRWLockExclusive(&childrenLock);
}

#endif

// ---- Descriptors -------------------------------------------------------

int64_t cos_read(int32_t fd, void* buf, int64_t count) {
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
        if (errno == EWOULDBLOCK) return COS_ERR_WOULD_BLOCK;
        return mapError(errno);
    }
#endif
}

void cos_flush_stdio(void) {
    fflush(stdout);
    fflush(stderr);
}

int32_t cos_wait_fd(int32_t fd, int32_t events) {
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

int64_t cos_write(int32_t fd, const void* buf, int64_t count) {
#if defined(_WIN32)
    unsigned n = count > 0x7fffffff ? 0x7fffffff : (unsigned)count;
    int r = _write(fd, buf, n);
    return r < 0 ? mapError(GetLastError()) : r;
#else
    for (;;) {
        ssize_t r = write(fd, buf, (size_t)count);
        if (r >= 0) return r;
        if (errno == EINTR) continue;
        if (errno == EWOULDBLOCK) return COS_ERR_WOULD_BLOCK;
        return mapError(errno);
    }
#endif
}

int32_t cos_close(int32_t fd) {
#if defined(_WIN32)
    return _close(fd) == 0 ? COS_OK : COS_ERR_GENERIC;
#else
    return ::close(fd) == 0 ? COS_OK : mapError(errno);
#endif
}

// ---- Signals -----------------------------------------------------------

namespace {

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
    case CTRL_BREAK_EVENT:    kind = COS_SIG_INTERRUPT; break;
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT: kind = COS_SIG_TERMINATE; break;
    case CTRL_LOGOFF_EVENT:   kind = COS_SIG_HANGUP; break;
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
    case SIGINT:   kind = COS_SIG_INTERRUPT; break;
    case SIGTERM:  kind = COS_SIG_TERMINATE; break;
    case SIGHUP:   kind = COS_SIG_HANGUP; break;
    case SIGWINCH: kind = COS_SIG_WINDOW_RESIZE; break;
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

bool initListeners() {
    static bool done = false;
    if (!done) {
        for (auto& l : listeners) { l.fd = -1; l.mask = 0; l.readFd = -1; }
        done = true;
    }
    return true;
}

}

int32_t cos_signal_listen(int32_t mask) {
    if (mask == 0 || (mask & (1 << COS_SIG_KILL))) return COS_ERR_INVALID;
    int fds[2];
#if defined(_WIN32)
    if (_pipe(fds, 256, _O_BINARY) != 0) return COS_ERR_GENERIC;
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
        cos_close(fds[0]);
        cos_close(fds[1]);
        return COS_ERR_NO_MEMORY;
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

void cos_signal_unlisten(int32_t fd) {
    lockSignals();
    initListeners();
    for (auto& l : listeners) {
        if (l.fd >= 0 && l.readFd == fd) {
            int mask = l.mask;
            int w = l.fd;
            l.fd = -1;
            l.mask = 0;
            for (int k = 1; k < 8; k++) if (mask & (1 << k)) want(k, -1);
            cos_close(w);
            break;
        }
    }
    unlockSignals();
    cos_close(fd);
}

// ---- Host --------------------------------------------------------------

int32_t cos_os_kind(void) {
#if defined(_WIN32)
    return COS_OS_WINDOWS;
#elif defined(__APPLE__)
    return COS_OS_MACOS;
#elif defined(__ANDROID__)
    return COS_OS_ANDROID;
#else
    return COS_OS_LINUX;
#endif
}

int32_t cos_arch(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return COS_ARCH_AARCH64;
#else
    return COS_ARCH_X86_64;
#endif
}

int32_t cos_os_version(char* buf, int32_t max) {
#if defined(_WIN32)
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
    OSVERSIONINFOW v = {};
    v.dwOSVersionInfoSize = sizeof(v);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn fn = ntdll ? (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    if (!fn || fn(&v) != 0) return COS_ERR_UNSUPPORTED;
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return copyOut(tmp, buf, max);
#elif defined(__APPLE__)
    char tmp[64];
    size_t n = sizeof(tmp);
    if (sysctlbyname("kern.osproductversion", tmp, &n, nullptr, 0) != 0) return mapError(errno);
    return copyOut(tmp, buf, max);
#else
    char tmp[PROP_VALUE_MAX];
    int n = __system_property_get("ro.build.version.release", tmp);
    if (n <= 0) return COS_ERR_UNSUPPORTED;
    return copyOut(tmp, (size_t)n, buf, max);
#endif
}

int32_t cos_hostname(char* buf, int32_t max) {
#if defined(_WIN32)
    wchar_t w[256];
    DWORD n = 256;
    if (!GetComputerNameExW(ComputerNameDnsHostname, w, &n)) return mapError(GetLastError());
    return narrowOut(w, (int)n, buf, max);
#else
    char tmp[256];
    if (gethostname(tmp, sizeof(tmp)) != 0) return mapError(errno);
    tmp[sizeof(tmp) - 1] = 0;
    return copyOut(tmp, buf, max);
#endif
}

void cos_cpus(int32_t* out) {
#if defined(_WIN32)
    int32_t logical = (int32_t)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    out[0] = logical; out[1] = logical; out[2] = 0;
#elif defined(__APPLE__)
    int logical = 0, perf = 0, eff = 0;
    size_t n = sizeof(int);
    sysctlbyname("hw.logicalcpu", &logical, &n, nullptr, 0);
    int levels = 0;
    n = sizeof(int);
    sysctlbyname("hw.nperflevels", &levels, &n, nullptr, 0);
    if (levels >= 2) {
        n = sizeof(int);
        sysctlbyname("hw.perflevel0.logicalcpu", &perf, &n, nullptr, 0);
        n = sizeof(int);
        sysctlbyname("hw.perflevel1.logicalcpu", &eff, &n, nullptr, 0);
    } else {
        perf = logical;
    }
    out[0] = logical; out[1] = perf; out[2] = eff;
#else
    int32_t logical = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
    out[0] = logical; out[1] = logical; out[2] = 0;
#endif
}

void cos_memory(int64_t* out) {
    out[0] = 0; out[1] = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX m = {};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
        out[0] = (int64_t)m.ullTotalPhys;
        out[1] = (int64_t)m.ullAvailPhys;
    }
#elif defined(__APPLE__)
    uint64_t total = 0;
    size_t n = sizeof(total);
    sysctlbyname("hw.memsize", &total, &n, nullptr, 0);
    out[0] = (int64_t)total;
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    if (host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        // What the kernel can hand out without paging anything out: free
        // pages, and the inactive and purgeable ones it can reclaim.
        uint64_t pages = (uint64_t)vm.free_count + vm.inactive_count + vm.purgeable_count
                       - vm.speculative_count;
        out[1] = (int64_t)(pages * (uint64_t)vm_kernel_page_size);
    }
    mach_port_deallocate(mach_task_self(), host);
#else
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        out[0] = (int64_t)si.totalram * si.mem_unit;
        out[1] = (int64_t)si.freeram * si.mem_unit;
    }
    // MemAvailable counts the cache the kernel can drop, which freeram does not.
    FILE* f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            long long kb;
            if (sscanf(line, "MemAvailable: %lld kB", &kb) == 1) { out[1] = kb * 1024; break; }
        }
        fclose(f);
    }
#endif
}

int64_t cos_page_size(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

int64_t cos_uptime(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64() * 1000000;
#elif defined(__APPLE__)
    // The continuous clock counts time asleep too, which uptime does.
    return (int64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

// ---- User --------------------------------------------------------------

int32_t cos_user_name(char* buf, int32_t max) {
#if defined(_WIN32)
    wchar_t w[257];
    DWORD n = 257;
    typedef BOOL (WINAPI *GetUserNameWFn)(LPWSTR, LPDWORD);
    HMODULE advapi = LoadLibraryW(L"advapi32.dll");
    GetUserNameWFn fn = advapi ? (GetUserNameWFn)GetProcAddress(advapi, "GetUserNameW") : nullptr;
    if (!fn || !fn(w, &n)) return COS_ERR_NOT_FOUND;
    return narrowOut(w, (int)n - 1, buf, max);
#else
    struct passwd pw, *res = nullptr;
    char tmp[4096];
    if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 || !res) return COS_ERR_NOT_FOUND;
    return copyOut(res->pw_name, buf, max);
#endif
}

int32_t cos_user_home(char* buf, int32_t max) {
#if defined(_WIN32)
    return cos_env_get("USERPROFILE", buf, max);
#else
    const char* h = getenv("HOME");
    if (h && *h) return copyOut(h, buf, max);
    struct passwd pw, *res = nullptr;
    char tmp[4096];
    if (getpwuid_r(getuid(), &pw, tmp, sizeof(tmp), &res) != 0 || !res) return COS_ERR_NOT_FOUND;
    return copyOut(res->pw_dir, buf, max);
#endif
}

// ---- Terminal ----------------------------------------------------------

#if defined(_WIN32)
namespace {
HANDLE consoleOf(int32_t fd) {
    intptr_t h = _get_osfhandle(fd);
    return h == -1 ? INVALID_HANDLE_VALUE : (HANDLE)h;
}
DWORD savedModes[3];
bool saved[3];
}
#else
namespace {
struct termios savedTermios[3];
bool saved[3];
}
#endif

int32_t cos_is_terminal(int32_t fd) {
#if defined(_WIN32)
    DWORD mode;
    return GetConsoleMode(consoleOf(fd), &mode) ? 1 : 0;
#else
    return isatty(fd) ? 1 : 0;
#endif
}

int32_t cos_term_size(int32_t fd, int32_t* out) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(consoleOf(fd), &info)) return COS_ERR_NOT_A_TERMINAL;
    out[0] = info.srWindow.Right - info.srWindow.Left + 1;
    out[1] = info.srWindow.Bottom - info.srWindow.Top + 1;
    return COS_OK;
#else
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return COS_ERR_NOT_A_TERMINAL;
    out[0] = ws.ws_col;
    out[1] = ws.ws_row;
    return COS_OK;
#endif
}

int32_t cos_read_password(const char* prompt, char* buf, int32_t max) {
    if (!buf || max <= 1) return COS_ERR_INVALID;
#if defined(_WIN32)
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return COS_ERR_NOT_A_TERMINAL;
    DWORD written, mode = 0;
    if (out != INVALID_HANDLE_VALUE && prompt) WriteFile(out, prompt, (DWORD)strlen(prompt), &written, nullptr);
    GetConsoleMode(in, &mode);
    SetConsoleMode(in, (mode & ~ENABLE_ECHO_INPUT) | ENABLE_LINE_INPUT);
    wchar_t w[1024];
    DWORD n = 0;
    BOOL ok = ReadConsoleW(in, w, 1023, &n, nullptr);
    SetConsoleMode(in, mode);
    if (out != INVALID_HANDLE_VALUE) { WriteFile(out, "\r\n", 2, &written, nullptr); CloseHandle(out); }
    CloseHandle(in);
    if (!ok) return COS_ERR_GENERIC;
    while (n > 0 && (w[n - 1] == L'\n' || w[n - 1] == L'\r')) n--;
    int32_t rc = narrowOut(w, (int)n, buf, max);
    SecureZeroMemory(w, sizeof(w));
    return rc;
#else
    // The controlling terminal, so that a password is read from the person
    // and not from whatever stdin was redirected to.
    int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0) return COS_ERR_NOT_A_TERMINAL;
    Fd tty(fd);
    struct termios before, quiet;
    if (tcgetattr(fd, &before) != 0) return COS_ERR_NOT_A_TERMINAL;
    quiet = before;
    quiet.c_lflag &= ~(tcflag_t)(ECHO | ECHONL);
    quiet.c_lflag |= ICANON;
    if (prompt) (void)!write(fd, prompt, strlen(prompt));
    tcsetattr(fd, TCSAFLUSH, &quiet);
    int32_t n = 0;
    int32_t rc = COS_OK;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0 || c == '\n' || c == '\r') break;
        if (n < max - 1) buf[n++] = c;
        else rc = COS_ERR_INVALID;
    }
    buf[n] = 0;
    tcsetattr(fd, TCSAFLUSH, &before);
    (void)!write(fd, "\n", 1);
    return rc == COS_OK ? n : rc;
#endif
}

int32_t cos_term_raw(int32_t fd) {
    if (fd < 0 || fd > 2) return COS_ERR_INVALID;
#if defined(_WIN32)
    HANDLE h = consoleOf(fd);
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return COS_ERR_NOT_A_TERMINAL;
    if (!saved[fd]) { savedModes[fd] = mode; saved[fd] = true; }
    DWORD raw = fd == 0
        ? (mode & ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_VIRTUAL_TERMINAL_INPUT
        : mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(h, raw) ? COS_OK : mapError(GetLastError());
#else
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return COS_ERR_NOT_A_TERMINAL;
    if (!saved[fd]) { savedTermios[fd] = t; saved[fd] = true; }
    cfmakeraw(&t);
    return tcsetattr(fd, TCSAFLUSH, &t) == 0 ? COS_OK : mapError(errno);
#endif
}

int32_t cos_term_restore(int32_t fd) {
    if (fd < 0 || fd > 2) return COS_ERR_INVALID;
    if (!saved[fd]) return COS_OK;
    saved[fd] = false;
#if defined(_WIN32)
    return SetConsoleMode(consoleOf(fd), savedModes[fd]) ? COS_OK : mapError(GetLastError());
#else
    return tcsetattr(fd, TCSAFLUSH, &savedTermios[fd]) == 0 ? COS_OK : mapError(errno);
#endif
}

int32_t cos_term_enable_ansi(int32_t fd) {
#if defined(_WIN32)
    HANDLE h = consoleOf(fd);
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return COS_ERR_NOT_A_TERMINAL;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? COS_OK : mapError(GetLastError());
#else
    (void)fd;
    return COS_OK;
#endif
}

} // extern "C"
