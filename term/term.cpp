// The terminal, for package os/term.
module;
#include <stdint.h>
#include <string.h>
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
#endif
export module os.term;
import os.sys;

#if defined(_WIN32)
HANDLE consoleOf(int32_t fd) {
    intptr_t h = _get_osfhandle(fd);
    return h == -1 ? INVALID_HANDLE_VALUE : (HANDLE)h;
}
DWORD savedModes[3];
#else
struct termios savedTermios[3];
#endif
bool saved[3];

export int32_t isTerminal(int32_t fd) noexcept {
#if defined(_WIN32)
    DWORD mode;
    return GetConsoleMode(consoleOf(fd), &mode) ? 1 : 0;
#else
    return isatty(fd) ? 1 : 0;
#endif
}

// termSize writes two, columns and rows, or is Err::notATerminal.
export int32_t termSize(int32_t fd, int32_t* out) noexcept {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(consoleOf(fd), &info)) return Err::notATerminal;
    out[0] = info.srWindow.Right - info.srWindow.Left + 1;
    out[1] = info.srWindow.Bottom - info.srWindow.Top + 1;
    return Err::ok;
#else
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return Err::notATerminal;
    out[0] = ws.ws_col;
    out[1] = ws.ws_row;
    return Err::ok;
#endif
}

// readPassword writes prompt to the terminal and reads a line with echo
// off.
export int32_t readPassword(const char* prompt, char* buf, int32_t max) noexcept {
    if (!buf || max <= 1) return Err::invalid;
#if defined(_WIN32)
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return Err::notATerminal;
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
    if (!ok) return Err::generic;
    while (n > 0 && (w[n - 1] == L'\n' || w[n - 1] == L'\r')) n--;
    int32_t rc = narrowOut(w, (int32_t)n, buf, max);
    SecureZeroMemory(w, sizeof(w));
    return rc;
#else
    // The controlling terminal, so that a password is read from the person
    // and not from whatever stdin was redirected to.
    int fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (fd < 0) return Err::notATerminal;
    struct termios before, quiet;
    if (tcgetattr(fd, &before) != 0) {
        ::close(fd);
        return Err::notATerminal;
    }
    quiet = before;
    quiet.c_lflag &= ~(tcflag_t)(ECHO | ECHONL);
    quiet.c_lflag |= ICANON;
    if (prompt) (void)!write(fd, prompt, strlen(prompt));
    tcsetattr(fd, TCSAFLUSH, &quiet);
    int32_t n = 0;
    int32_t rc = Err::ok;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0 || c == '\n' || c == '\r') break;
        if (n < max - 1) buf[n++] = c;
        else rc = Err::invalid;
    }
    buf[n] = 0;
    tcsetattr(fd, TCSAFLUSH, &before);
    (void)!write(fd, "\n", 1);
    ::close(fd);
    return rc == Err::ok ? n : rc;
#endif
}

// makeRaw puts fd in raw mode until restore: the state it had is kept here.
export int32_t makeRaw(int32_t fd) noexcept {
    if (fd < 0 || fd > 2) return Err::invalid;
#if defined(_WIN32)
    HANDLE h = consoleOf(fd);
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return Err::notATerminal;
    if (!saved[fd]) { savedModes[fd] = mode; saved[fd] = true; }
    DWORD raw = fd == 0
        ? (mode & ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_VIRTUAL_TERMINAL_INPUT
        : mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    return SetConsoleMode(h, raw) ? Err::ok : mapError(GetLastError());
#else
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return Err::notATerminal;
    if (!saved[fd]) { savedTermios[fd] = t; saved[fd] = true; }
    cfmakeraw(&t);
    return tcsetattr(fd, TCSAFLUSH, &t) == 0 ? Err::ok : mapError(errno);
#endif
}

export int32_t restoreMode(int32_t fd) noexcept {
    if (fd < 0 || fd > 2) return Err::invalid;
    if (!saved[fd]) return Err::ok;
    saved[fd] = false;
#if defined(_WIN32)
    return SetConsoleMode(consoleOf(fd), savedModes[fd]) ? Err::ok : mapError(GetLastError());
#else
    return tcsetattr(fd, TCSAFLUSH, &savedTermios[fd]) == 0 ? Err::ok : mapError(errno);
#endif
}

// enableAnsi lets the console interpret ANSI escapes (Windows); a no-op
// elsewhere.
export int32_t enableAnsi(int32_t fd) noexcept {
#if defined(_WIN32)
    HANDLE h = consoleOf(fd);
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return Err::notATerminal;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) ? Err::ok : mapError(GetLastError());
#else
    (void)fd;
    return Err::ok;
#endif
}
