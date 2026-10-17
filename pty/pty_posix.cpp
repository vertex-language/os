// POSIX pseudo-terminals: posix_openpt for the pair, fork and exec for
// the child (posix_spawn can't make the terminal a controlling one).
// Between fork and exec the child only makes async-signal-safe calls.
module;
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__APPLE__)
    #include <sys/event.h>
    #include <libproc.h>
#endif
extern "C" char** environ;
module os.pty;
import os.sys;

namespace {

// split turns a blob of count NUL-terminated strings into a
// NULL-terminated array pointing into it.
char** split(const char* blob, int32_t count) {
    char** out = (char**)calloc((size_t)count + 1, sizeof(char*));
    if (!out) return nullptr;
    const char* p = blob;
    for (int32_t i = 0; i < count; i++) {
        out[i] = (char*)p;
        p += strlen(p) + 1;
    }
    out[count] = nullptr;
    return out;
}

bool setFlags(int fd, bool nonblock) {
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) return false;
    if (nonblock) fl |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, fl) < 0) return false;
    return fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

// The terminal settings a new terminal starts with: what a shell expects
// of xterm.
void defaults(struct termios* t) {
    memset(t, 0, sizeof(*t));
    t->c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
#if defined(IUTF8)
    t->c_iflag |= IUTF8;
#endif
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CREAD | CS8 | HUPCL;
    t->c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
    t->c_cc[VEOF] = 4;
    t->c_cc[VEOL] = 255;
    t->c_cc[VEOL2] = 255;
    t->c_cc[VERASE] = 0x7f;
    t->c_cc[VWERASE] = 23;
    t->c_cc[VKILL] = 21;
    t->c_cc[VREPRINT] = 18;
    t->c_cc[VINTR] = 3;
    t->c_cc[VQUIT] = 0x1c;
    t->c_cc[VSUSP] = 26;
    t->c_cc[VSTART] = 17;
    t->c_cc[VSTOP] = 19;
    t->c_cc[VLNEXT] = 22;
    t->c_cc[VDISCARD] = 15;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
#if defined(VDSUSP)
    t->c_cc[VDSUSP] = 25;
#endif
#if defined(VSTATUS)
    t->c_cc[VSTATUS] = 20;
#endif
    cfsetispeed(t, B38400);
    cfsetospeed(t, B38400);
}

// foregroundPid is the terminal's foreground process group, which is its
// leader's pid.
pid_t foregroundPid(int fd) {
    pid_t pg = tcgetpgrp(fd);
    return pg > 0 ? pg : -1;
}

} // namespace

int32_t ptySpawn(const char* path,
                 const char* args, int32_t argCount,
                 const char* env, int32_t envCount,
                 const char* dir,
                 int32_t cols, int32_t rows,
                 int64_t* out) noexcept {
    if (!path || !args || argCount < 1 || !out) return Err::invalid;

    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return mapError(errno);
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        int e = errno;
        ::close(master);
        return mapError(e);
    }
    char slaveName[128];
#if defined(__APPLE__)
    if (ioctl(master, TIOCPTYGNAME, slaveName) != 0) {
        int e = errno;
        ::close(master);
        return mapError(e);
    }
#else
    if (ptsname_r(master, slaveName, sizeof slaveName) != 0) {
        int e = errno;
        ::close(master);
        return mapError(e);
    }
#endif
    // The child's end is opened here, so its settings and size are made
    // before the child runs: what it reads of them first is right.
    int slave = open(slaveName, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        int e = errno;
        ::close(master);
        return mapError(e);
    }
    struct termios tio;
    defaults(&tio);
    tcsetattr(slave, TCSANOW, &tio);
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)(cols > 0 ? cols : 80);
    ws.ws_row = (unsigned short)(rows > 0 ? rows : 24);
    ioctl(slave, TIOCSWINSZ, &ws);

    char** argv = split(args, argCount);
    char** envp = env ? split(env, envCount) : environ;
    if (!argv || !envp) {
        free(argv);
        if (env) free(envp);
        ::close(slave);
        ::close(master);
        return Err::noMemory;
    }

    // A failed exec reports its errno through a close-on-exec pipe; a
    // successful one closes it, and the parent reads nothing.
    int report[2];
    if (pipe(report) != 0) {
        int e = errno;
        free(argv);
        if (env) free(envp);
        ::close(slave);
        ::close(master);
        return mapError(e);
    }
    fcntl(report[0], F_SETFD, FD_CLOEXEC);
    fcntl(report[1], F_SETFD, FD_CLOEXEC);
    int maxFd = (int)sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 65536) maxFd = 65536;

    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pid_t pid = fork();
    if (pid == 0) {
        // The child. Nothing here allocates or takes a lock.
        for (int s = 1; s < NSIG; s++) signal(s, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        int keep = report[1];
        for (int fd = 3; fd < maxFd; fd++) {
            if (fd != keep) ::close(fd);
        }
        if (dir && dir[0] && chdir(dir) != 0) {
            int e = errno;
            (void)!write(keep, &e, sizeof e);
            _exit(127);
        }
        execve(path, argv, envp);
        int e = errno;
        (void)!write(keep, &e, sizeof e);
        _exit(127);
    }
    int forkErr = errno;
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    free(argv);
    if (env) free(envp);
    ::close(slave);
    ::close(report[1]);
    if (pid < 0) {
        ::close(report[0]);
        ::close(master);
        return mapError(forkErr);
    }
    int childErr = 0;
    ssize_t got;
    while ((got = read(report[0], &childErr, sizeof childErr)) < 0 && errno == EINTR) {}
    ::close(report[0]);
    if (got == (ssize_t)sizeof childErr) {
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        ::close(master);
        return mapError(childErr);
    }

    setFlags(master, true);
    out[0] = pid;
    out[1] = master;
#if defined(__APPLE__)
    int kq = kqueue();
    if (kq >= 0) {
        setFlags(kq, false);
        struct kevent ev;
        EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
            ::close(kq);
            kq = -1;
        }
    }
    out[2] = kq;
#else
    out[2] = -1;
#endif
    return Err::ok;
}

int64_t ptyRead(int32_t fd, uint8_t* into, int64_t count) noexcept {
    if (fd < 0 || !into || count < 0) return Err::invalid;
    while (true) {
        ssize_t n = read(fd, into, (size_t)count);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return Err::wouldBlock;
        // Linux and macOS both answer EIO once the child's end is closed
        // by everyone: that is the end of the stream.
        if (errno == EIO) return 0;
        return mapError(errno);
    }
}

int64_t ptyWrite(int32_t fd, const uint8_t* from, int64_t count) noexcept {
    if (fd < 0 || !from || count < 0) return Err::invalid;
    while (true) {
        ssize_t n = write(fd, from, (size_t)count);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return Err::wouldBlock;
        if (errno == EIO) return Err::brokenPipe;
        return mapError(errno);
    }
}

int32_t ptyResize(int32_t fd, int32_t cols, int32_t rows, int32_t width, int32_t height) noexcept {
    if (fd < 0 || cols <= 0 || rows <= 0) return Err::invalid;
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = (unsigned short)(width > 0 ? width : 0);
    ws.ws_ypixel = (unsigned short)(height > 0 ? height : 0);
    return ioctl(fd, TIOCSWINSZ, &ws) == 0 ? Err::ok : mapError(errno);
}

int32_t ptyForeground(int32_t fd, char* buf, int32_t max) noexcept {
    pid_t pid = foregroundPid(fd);
    if (pid < 0) return Err::notFound;
#if defined(__APPLE__)
    char name[2 * MAXCOMLEN + 1];
    int n = proc_name(pid, name, sizeof name);
    if (n <= 0) return Err::notFound;
    return copyOutN(name, (size_t)n, buf, max);
#else
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
    int f = open(path, O_RDONLY | O_CLOEXEC);
    if (f < 0) return mapError(errno);
    char name[256];
    ssize_t n = read(f, name, sizeof name - 1);
    ::close(f);
    if (n <= 0) return Err::notFound;
    while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == 0)) n--;
    return copyOutN(name, (size_t)n, buf, max);
#endif
}

int32_t ptyForegroundDir(int32_t fd, char* buf, int32_t max) noexcept {
    pid_t pid = foregroundPid(fd);
    if (pid < 0) return Err::notFound;
#if defined(__APPLE__)
    struct proc_vnodepathinfo info;
    int n = proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof info);
    if (n != (int)sizeof info) return Err::notFound;
    return copyOut(info.pvi_cdir.vip_path, buf, max);
#else
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/cwd", (int)pid);
    char dir[4096];
    ssize_t n = readlink(path, dir, sizeof dir - 1);
    if (n <= 0) return mapError(errno);
    return copyOutN(dir, (size_t)n, buf, max);
#endif
}

int32_t ptyTryWait(int64_t pid, int32_t* kind, int32_t* code) noexcept {
    int status = 0;
    pid_t r;
    while ((r = waitpid((pid_t)pid, &status, WNOHANG)) < 0 && errno == EINTR) {}
    if (r < 0) return mapError(errno);
    if (r == 0) return 0;
    if (WIFEXITED(status)) {
        *kind = PtyExit::exited;
        *code = WEXITSTATUS(status);
    } else {
        *kind = PtyExit::signaled;
        *code = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    }
    return 1;
}

int32_t ptySignal(int64_t pid, int32_t sig) noexcept {
    if (pid <= 0 || sig <= 0) return Err::invalid;
    // The whole session's foreground group gets it, as from a key.
    if (kill(-(pid_t)pid, sig) == 0) return Err::ok;
    return kill((pid_t)pid, sig) == 0 ? Err::ok : mapError(errno);
}

void ptyClose(int32_t fd) noexcept {
    if (fd >= 0) ::close(fd);
}
