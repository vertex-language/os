// cos: the operating system's process, environment, host, user and
// terminal calls, as a C ABI every os package calls. It is the only part
// of the os repository that includes a platform header.
//
// Nothing here waits on a descriptor. Pipes and signal listeners are
// non-blocking; a read that has nothing yet returns COS_ERR_WOULD_BLOCK,
// and the Vertex side waits through the runtime (vertex_task_wait_fd), so
// a waiting task parks and its thread runs others. A child's exit is a
// descriptor too (kqueue on macOS, a pidfd or a watcher's pipe on
// Android), which becomes readable when the child is done.
//
// Windows has no readiness for pipes. There, cos_pollable() is 0, reads
// and waits block the calling thread, and the Vertex side calls them
// without waiting first -- slower, never wrong, as the runtime's own
// Windows fallback is.
//
// Strings in and out are UTF-8. A function that fills a caller's buffer
// returns the length the whole value needs (not counting the NUL); where
// that is more than the buffer holds, nothing useful was written and the
// caller asks again with a larger one.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
#define COS_NORETURN [[noreturn]]
extern "C" {
#else
#define COS_NORETURN _Noreturn
#endif

// Error codes. Every cos_* function that can fail returns a negative one.
enum {
    COS_OK                 = 0,
    COS_ERR_GENERIC        = -1,
    COS_ERR_NOT_FOUND      = -2,
    COS_ERR_PERMISSION     = -3,
    COS_ERR_INVALID        = -4,
    COS_ERR_WOULD_BLOCK    = -5,
    COS_ERR_UNSUPPORTED    = -6,
    COS_ERR_NO_MEMORY      = -7,
    COS_ERR_INTERRUPTED    = -8,
    COS_ERR_BROKEN_PIPE    = -9,
    COS_ERR_NOT_A_TERMINAL = -10
};

// The platform the program was built for.
enum {
    COS_OS_MACOS   = 1,
    COS_OS_WINDOWS = 2,
    COS_OS_LINUX   = 3,
    COS_OS_ANDROID = 4
};

enum {
    COS_ARCH_AARCH64 = 1,
    COS_ARCH_X86_64  = 2
};

// What a child's standard stream is connected to.
enum {
    COS_STDIO_INHERIT = 0,
    COS_STDIO_PIPE    = 1,
    COS_STDIO_NULL    = 2,
    COS_STDIO_FILE    = 3
};

// How a child ended, as cos_try_wait reports it.
enum {
    COS_EXIT_EXITED   = 1,
    COS_EXIT_SIGNALED = 2
};

// The portable signals. A listener's mask is (1 << kind) for each.
enum {
    COS_SIG_INTERRUPT     = 1,
    COS_SIG_TERMINATE     = 2,
    COS_SIG_HANGUP        = 3,
    COS_SIG_WINDOW_RESIZE = 4,
    COS_SIG_KILL          = 5   // only as a thing to send, never listened for
};

// Last OS error code: errno, or GetLastError on Windows.
int32_t cos_last_error(void);

// 1 where descriptors this bridge hands out can be waited on through the
// runtime, 0 where reads and waits block the thread instead (Windows).
int32_t cos_pollable(void);

// ---- Environment -------------------------------------------------------

// The value of name into buf, or COS_ERR_NOT_FOUND where it is unset.
int32_t cos_env_get(const char* name, char* buf, int32_t max);
int32_t cos_env_set(const char* name, const char* value);
int32_t cos_env_remove(const char* name);
// The environment as a snapshot: count entries, then each "NAME=value".
int32_t cos_env_count(void);
int32_t cos_env_entry(int32_t index, char* buf, int32_t max);

// ---- This process ------------------------------------------------------

int32_t cos_pid(void);
int32_t cos_ppid(void);
int32_t cos_executable_path(char* buf, int32_t max);
int32_t cos_current_dir(char* buf, int32_t max);
int32_t cos_set_current_dir(const char* path);
// Ends the process, flushing C stdio (where print's output is buffered).
COS_NORETURN void cos_exit(int32_t code);
// Ends the process now: nothing is flushed, nothing runs.
COS_NORETURN void cos_abort(void);
// 1 where path names a file this process may execute.
int32_t cos_is_executable(const char* path);

// ---- Child processes ---------------------------------------------------

// Starts path with arguments and, unless env_count is -1, exactly the
// environment given. args and env are blobs of NUL-terminated strings,
// one after another; args includes argv[0]. dir may be NULL. modes are
// three, for stdin, stdout and stderr, and files is a blob of three
// paths the same way, the i-th used where modes[i] is COS_STDIO_FILE.
//
// out is five: pid, stdin's write end, stdout's read end, stderr's read
// end (-1 where not piped), and the exit descriptor (-1 where the
// platform has none, which cos_pollable says). Returns 0 or an error.
int32_t cos_spawn(const char* path,
                  const char* args, int32_t arg_count,
                  const char* env, int32_t env_count,
                  const char* dir,
                  const int32_t* modes, const char* files,
                  int64_t* out);

// Whether the child has ended: 1 with kind and code filled, 0 where it
// is still running. block waits for it (only where there is no exit
// descriptor to wait on instead). Reaps the child; call it once it ends.
int32_t cos_try_wait(int64_t pid, int32_t exit_fd, int32_t block,
                     int32_t* kind, int32_t* code);
// Sends a portable signal (COS_SIG_*) or, where posix is non-zero, that
// POSIX signal number.
int32_t cos_send_signal(int64_t pid, int32_t kind, int32_t posix);
// Releases what the bridge holds for a child after it has been waited
// for, and closes its exit descriptor.
void    cos_release_child(int64_t pid, int32_t exit_fd);

// ---- Descriptors -------------------------------------------------------

// Non-blocking where cos_pollable is 1: COS_ERR_WOULD_BLOCK when nothing
// is ready. 0 from a read is the end of the stream.
int64_t cos_read(int32_t fd, void* buf, int64_t count);
int64_t cos_write(int32_t fd, const void* buf, int64_t count);
int32_t cos_close(int32_t fd);

// ---- Signals -----------------------------------------------------------

// A descriptor that receives one byte, the kind, each time a signal in
// mask arrives. While any listener wants a kind, its default action (end
// the process) does not happen. Returns the descriptor or an error.
int32_t cos_signal_listen(int32_t mask);
void    cos_signal_unlisten(int32_t fd);

// ---- Host --------------------------------------------------------------

int32_t cos_os_kind(void);
int32_t cos_arch(void);
int32_t cos_os_version(char* buf, int32_t max);
int32_t cos_hostname(char* buf, int32_t max);
// logical, performance and efficiency processors.
void    cos_cpus(int32_t* out);
// total and available bytes.
void    cos_memory(int64_t* out);
int64_t cos_page_size(void);
// nanoseconds since boot.
int64_t cos_uptime(void);

// ---- User --------------------------------------------------------------

int32_t cos_user_name(char* buf, int32_t max);
int32_t cos_user_home(char* buf, int32_t max);

// ---- Terminal ----------------------------------------------------------

int32_t cos_is_terminal(int32_t fd);
// columns and rows, or COS_ERR_NOT_A_TERMINAL.
int32_t cos_term_size(int32_t fd, int32_t* out);
// Writes prompt to the terminal and reads a line with echo off.
int32_t cos_read_password(const char* prompt, char* buf, int32_t max);
// Raw mode on fd until cos_term_restore: the state it had is kept here.
int32_t cos_term_raw(int32_t fd);
int32_t cos_term_restore(int32_t fd);
// Lets the console interpret ANSI escapes (Windows); 0 elsewhere.
int32_t cos_term_enable_ansi(int32_t fd);

#ifdef __cplusplus
}
#endif
