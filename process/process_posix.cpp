// Starting, waiting for and signalling children on Darwin, Linux and
// Android: see process.cpp.
module;
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__APPLE__)
    #include <crt_externs.h>
    #include <sys/event.h>
#else
    #include <sys/syscall.h>
#endif
module os.process;
import os.sys;

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

char** environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    extern char** environ;
    return environ;
#endif
}

#if !defined(__APPLE__)
// Where there is no pidfd, a thread waits for the child and writes a
// byte to a pipe when it has ended. It only watches: waitid with WNOWAIT
// leaves the child to be reaped by tryWait.
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
#endif

int32_t spawn(const char* path,
                  const char* args, int32_t argCount,
                  const char* env, int32_t envCount,
                  const char* dir,
                  const int32_t* modes, const char* filesBlob,
                  int64_t* out) noexcept {
    for (int i = 0; i < 5; i++) out[i] = -1;
    const char* files[3];
    files[0] = filesBlob;
    files[1] = files[0] + strlen(files[0]) + 1;
    files[2] = files[1] + strlen(files[1]) + 1;

    // The ends the child gets, and the ends the parent keeps.
    Fd child[3], parent[3];
    for (int i = 0; i < 3; i++) {
        switch (modes[i]) {
        case StdioMode::pipe: {
            int fds[2];
            // stdin: the child reads fds[0], the parent writes fds[1].
            if (!makePipe(fds, i == 0 ? 1 : 0)) return mapError(errno);
            child[i].fd = i == 0 ? fds[0] : fds[1];
            parent[i].fd = i == 0 ? fds[1] : fds[0];
            break;
        }
        case StdioMode::discard: {
            int f = open("/dev/null", i == 0 ? O_RDONLY : O_WRONLY);
            if (f < 0) return mapError(errno);
            child[i].fd = f;
            break;
        }
        case StdioMode::file: {
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

    char** argv = splitBlob(args, argCount);
    char** envp = envCount >= 0 ? splitBlob(env, envCount) : nullptr;
    if (!argv || (envCount >= 0 && !envp)) {
        free(argv);
        free(envp);
        return Err::noMemory;
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
            // wait on; tryWait finds it done without waiting.
            ::close(kq);
            kq = -1;
        }
    }
    out[4] = kq;
#else
    out[4] = exitDescriptor(pid);
#endif
    return Err::ok;
}

int32_t tryWait(int64_t pid, int32_t exitFd, int32_t block,
                     int32_t* kind, int32_t* code) noexcept {
    (void)exitFd;
    int status = 0;
    pid_t r;
    while ((r = waitpid((pid_t)pid, &status, block ? 0 : WNOHANG)) < 0 && errno == EINTR) {}
    if (r < 0) return mapError(errno);
    if (r == 0) return 0;
    if (WIFEXITED(status)) {
        *kind = ExitKind::exited;
        *code = WEXITSTATUS(status);
    } else {
        *kind = ExitKind::signaled;
        *code = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    }
    return 1;
}

int32_t sendSignal(int64_t pid, int32_t kind, int32_t posix) noexcept {
    int sig = posix != 0 ? posix : posixSignal(kind);
    if (sig == 0) return Err::invalid;
    return kill((pid_t)pid, sig) == 0 ? Err::ok : mapError(errno);
}

void releaseChild(int64_t pid, int32_t exitFd) noexcept {
    (void)pid;
    if (exitFd >= 0) ::close(exitFd);
}
