// Pseudo-terminals, for package os/pty: a child started with a terminal
// for its stdin, stdout and stderr, and the parent holding the other end.
//
// The parent's end (the master) is non-blocking: a read with nothing yet
// returns Err::wouldBlock and the Vertex side waits on the descriptor
// through the runtime. The child's exit is a descriptor too (kqueue on
// macOS, as os/process has it), so waiting for it parks a task rather
// than a thread.
//
// Windows has ConPTY, which isn't written yet: every call there returns
// Err::unsupported.
module;
#include <stdint.h>
#include <stddef.h>
export module os.pty;
import os.sys;

// How a child ended, for ptyTryWait's kind.
export namespace PtyExit {
    constexpr int32_t exited = 1;
    constexpr int32_t signaled = 2;
}

// ptySpawn starts path under a new pseudo-terminal of cols × rows cells.
// args and env are blobs of NUL-terminated strings, one after another;
// args includes argv[0]; env is null to inherit this process's. dir may
// be null. The child is a session leader with the terminal as its
// controlling terminal, its signals at their defaults.
//
// out is three: the child's pid, the master descriptor, and the exit
// descriptor (-1 where the platform has none). Returns 0 or an error.
export int32_t ptySpawn(const char* path,
                        const char* args, int32_t argCount,
                        const char* env, int32_t envCount,
                        const char* dir,
                        int32_t cols, int32_t rows,
                        int64_t* out) noexcept;

// ptyRead reads what the child wrote: a count, 0 once every copy of the
// child's end is closed (the child and what it started have ended), or
// Err::wouldBlock.
export int64_t ptyRead(int32_t fd, uint8_t* into, int64_t count) noexcept;

// ptyWrite writes what the child will read as typed: a count (perhaps
// short), or Err::wouldBlock.
export int64_t ptyWrite(int32_t fd, const uint8_t* from, int64_t count) noexcept;

// ptyResize tells the terminal, and so the child (SIGWINCH), its new size
// in cells and, where known, pixels.
export int32_t ptyResize(int32_t fd, int32_t cols, int32_t rows, int32_t width, int32_t height) noexcept;

// ptyForeground writes the name of the terminal's foreground process
// (the shell, or what it is running) into buf: the length it needs, or an
// error.
export int32_t ptyForeground(int32_t fd, char* buf, int32_t max) noexcept;

// ptyForegroundDir writes the working directory of the terminal's
// foreground process into buf: the length it needs, or an error.
export int32_t ptyForegroundDir(int32_t fd, char* buf, int32_t max) noexcept;

// ptyTryWait reports whether the child has ended: 1 with kind (PtyExit)
// and code filled, 0 while it runs. Reaps it.
export int32_t ptyTryWait(int64_t pid, int32_t* kind, int32_t* code) noexcept;

// ptySignal sends the child's process group a POSIX signal.
export int32_t ptySignal(int64_t pid, int32_t sig) noexcept;

// ptyClose closes a descriptor this module handed out.
export void ptyClose(int32_t fd) noexcept;
