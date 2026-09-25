# os

[![package: vs-package](https://img.shields.io/badge/package-vs--package-f4f4f5?style=flat-square&labelColor=e4e4e7&color=18181b)](https://github.com/vertex-language)
[![packages: env | process | signal | user | host | term | pty](https://img.shields.io/badge/packages-env%20%7C%20process%20%7C%20signal%20%7C%20user%20%7C%20host%20%7C%20term%20%7C%20pty-f4f4f5?style=flat-square&labelColor=e4e4e7&color=18181b)](https://github.com/vertex-language/os)
[![status: macOS tested](https://img.shields.io/badge/status-macOS%20tested-f4f4f5?style=flat-square&labelColor=e4e4e7&color=18181b)](https://github.com/vertex-language/os)

Operating system interfaces: environment variables, processes, signals, user profiles, host diagnostics, and terminal control.

> **Status.** `env`, `process`, `signal`, `user`, `host` and `term` are
> implemented, and `tests/check` passes on macOS (aarch64).

---

## Quick Start

Run any entry point with:

```bash
vsc run main.vs
```

Or run the test suite:

```bash
vsc run check
```

---

## The boundary

**`fs` covers files and directories. `os` covers the running process, its
environment, and the machine it runs on.**

Go's `os` package puts files, the environment, processes, signals and users
in one namespace. Rust splits them into `std::env`, `std::process` and
`std::fs`, and Vertex does the same. `fs` is its own repository already.
`os` is a repository of small, focused packages that share one native
bridge (`cos`).

| Package | What it is | Closest equivalents |
| --- | --- | --- |
| **`os/env`** | environment variables | Rust `std::env`, Deno `Deno.env`, Go `os.LookupEnv` |
| **`os/process`** | this process, and the child processes it starts | Rust `std::process`, Swift `Subprocess`, Deno `Deno.Command`, Go `os/exec` |
| **`os/signal`** | Ctrl-C, termination, and graceful shutdown | tokio `signal`, Go `os/signal`, Deno `Deno.addSignalListener` |
| **`os/user`** | the current user and their standard directories | Rust `dirs`, Go `os.UserCacheDir` |
| **`os/host`** | the machine: OS, architecture, CPUs, memory | Rust `sysinfo`, Node `os`, Go `runtime.NumCPU` |
| **`os/term`** | whether a stream is a terminal, its size, raw mode, passwords, color | Go `x/term`, Rust `crossterm`/`anstream`, Node `tty` |
| **`os/pty`** | running a command under a pseudo-terminal | Rust `portable-pty`, Go `creack/pty` |

---

## Design rules

These are the conventions that modern standard libraries have converged
on. Each package below follows them.

1. **Package names are singular and name the thing.** `process`, not
   `exec` or `subprocess`. `signal`, not `signals`.
2. **An absent value is `nil`, never a sentinel.** `env.Get` returns
   `string?`. Go's `Getenv` returns `""` for an unset variable, and
   `LookupEnv` was added later to fix that.
3. **Arguments are arrays, never a shell string.** `process.Command("git",
   ["log", file])` can't be injected. To use a shell, write it out:
   `Command("/bin/sh", ["-c", script])`.
4. **Builders for anything with options.** A `Command` value has labeled
   fields, as in Rust `Command`, Swift `Subprocess` and Deno `Command`.
   There are no flag bitmasks.
5. **Async wherever the process waits.** Waiting for a child, reading its
   pipes and waiting for a signal all park the task through the runtime's
   `vertex_task_wait_fd`. They never block the thread. Queries that return
   immediately (`env.Get`, `host.CPUs()`) are synchronous.
6. **Portable first, with an escape hatch.** The names are portable
   (`.interrupt`, `Terminate()`, `user.CacheDir()`). POSIX- or
   Windows-only behavior is spelled out explicitly (`.posix(n)`,
   `host.OS.Kind == .windows`).
7. **Verbs follow the house table.** `Get` reads a value that may be
   absent, `Require` reads a value that must be there, `Find` is a lookup
   that may fail, `Spawn` starts something, `Wait` waits for it, and
   `Close` releases it.
8. **All native code is in `cos`.** No other package has to bind
   libc to get an environment variable, a CPU count or a subprocess
   again.

---

## `os/env`

```swift
import "os/env"

let home  = env.Get("HOME")                  // string?: unset is nil, not ""
let port  = env.Get("PORT") ?? "8080"
let token = try env.Require("HF_TOKEN")      // throws env.Missing("HF_TOKEN")
let raw   = env.GetBytes("PATH")             // [uint8]?: the value's bytes

for (key, value) in env.All() { … }          // a snapshot, sorted by key

env.Set("RUST_LOG", "debug")                 // see the hazard below
env.Remove("TMPDIR")
```

| API | Notes |
| --- | --- |
| `Get(_ name) -> string?` | `nil` when the variable is unset or isn't valid UTF-8 |
| `GetBytes(_ name) -> [uint8]?` | the value's bytes. Today these go through `Get`, so a value that isn't valid UTF-8 is `nil` |
| `Require(_ name) throws -> string` | throws `env.Missing(name)` |
| `All() -> [(string, string)]` | a snapshot. Entries that aren't UTF-8 are skipped |
| `Set(_ name, _ value)`, `Remove(_ name)` | **not thread-safe** |

**The hazard.** POSIX `setenv` isn't safe while other threads are reading
the environment. Rust 2024 made `set_var` `unsafe` because of this. Until
Vertex has an `unsafe` marker, `Set` and `Remove` are documented as safe
only before the program starts any tasks or threads. The safe way to give
a child a different environment is `Command.Env`.

---

## `os/process`

### The current process

```swift
import "os/process"

process.Args                     // [string]: what CommandLine.arguments was
process.ID                       // int32: this process's pid
process.ParentID                 // int32 (0 on Windows)
process.ExecutablePath()         // string?: Rust current_exe, Go os.Executable
try process.CurrentDir()         // string
try process.SetCurrentDir(p)
process.Exit(2)                  // -> Never: flushes stdout, then exits
process.Abort()                  // -> Never: no flushing, no cleanup
```

### Running a program

```swift
// One line: run, capture, and throw on a non-zero exit.
let head = try await process.Run("git", ["rev-parse", "HEAD"])
print(head.StdoutText)

// Capture without throwing on a non-zero exit (Rust Command::output).
let out = try await process.Command("make", ["test"]).Output()
if !out.Status.Success { print(out.StderrText) }

// Everything else is a Command.
var cmd = process.Command("ffmpeg", ["-i", "in.mp4", "out.webm"])
cmd.Dir = "/tmp/work"
cmd.Env["FFREPORT"] = "1"        // added to the inherited environment
cmd.ClearEnv = false             // true: start from an empty environment
cmd.Stdin  = .null
cmd.Stdout = .pipe
cmd.Stderr = .inherit

let child = try cmd.Spawn()
var lines = child.Stdout!.Lines()                    // an io.AsyncBufferedReader
while let line = try await lines.ReadLine() { print(line) }
let status = try await child.Wait()          // .exited(code) | .signaled(signal)

child.Terminate()                // SIGTERM, or CTRL_BREAK on Windows
child.Kill()                     // SIGKILL, or TerminateProcess on Windows
let done = try child.TryWait()   // ExitStatus?: doesn't wait

try child.Signal(.hangup)        // any signal.Kind, or .posix(n)

let git = process.Find("git")    // string?: a PATH lookup, like `which` (PATHEXT on Windows)
```

| Type | Members |
| --- | --- |
| `Command` | `Program`, `Args`, `Dir: string?`, `Env: [string: string]`, `ClearEnv`, `Stdin`/`Stdout`/`Stderr: Stdio`, `Spawn()`, `Output() async`, `Status() async` |
| `Stdio` | `.inherit` (default), `.pipe`, `.null`, `.file(string)` |
| `Child` | `ID`, `Stdin: PipeWriter?`, `Stdout`/`Stderr: PipeReader?`, `Wait() async`, `TryWait()`, `Terminate()`, `Kill()`, `Signal(_:)` |
| `PipeReader` | an `io.AsyncReader` and `io.Closer`: `Read(into:) async`, `ReadToEnd(limit:) async`, `ReadText(limit:) async`, `Lines()` (an `io.AsyncBufferedReader`), `Close()` |
| `PipeWriter` | an `io.AsyncWriter` and `io.Closer`: `Write(_:) async` (bytes or text), `Close()`, which is how the child sees EOF |
| `Stdin` / `Stdout` / `Stderr` | this process's standard streams: an `io.Reader`, and `io.Writer`s that write out what `print` buffered first, so output stays in order. Each meets the async protocol too |
| `ExitStatus` | `.exited(int32)`, `.signaled(signal.Kind)`, `Success`, `Code: int32?` |
| `Output` | `Status`, `Stdout: [uint8]`, `Stderr: [uint8]`, `StdoutText`, `StderrText` |
| `ProcessError` | `.notFound(program)`, `.permissionDenied(program)`, `.failed(status, stderr)` (from `Run`), `.system(code, context)` |

**Differences from the design.**

- Paths are `string` rather than `fs.Path`, so `os` doesn't depend on
  another repository yet.
- `Lines()` returns an `io.AsyncBufferedReader`, read with `ReadLine()`,
  not an `AsyncSequence`, because core doesn't declare `AsyncSequence` or
  `for await` yet.

**Implementation.**

- **macOS** uses `posix_spawn` with `POSIX_SPAWN_CLOEXEC_DEFAULT`, so
  only fds 0–2 reach the child. The child's signal handling is reset to
  the defaults and its mask cleared. Pipes are non-blocking and read
  through `vertex_task_wait_fd`. `Wait` waits on a kqueue `EVFILT_PROC`
  descriptor, never on a blocking `waitpid`.
- **Android** uses `fork` immediately followed by `exec`. Between them it
  only sets up descriptors, the working directory and signals. A failed
  `exec` reports its errno through a close-on-exec pipe. `Wait` uses a
  pidfd, or a watcher thread (`waitid(WNOWAIT)`) on kernels older than
  5.3.
- `Output()` drains stderr in a task while it reads stdout. Without that,
  a child that fills one pipe while the parent reads the other would stop.
- **Windows** uses `CreateProcessW` with `STARTUPINFOEX`. The handle list
  keeps the child from inheriting unrelated handles. Arguments are quoted
  with the `CommandLineToArgvW` rules, so an argument array reaches the
  child unchanged.

`PipeReader` and `PipeWriter` conform to `io.AsyncReader` and
`io.AsyncWriter`, so `io.Copy(from: &pipe, to: &file)` works without code
specific to processes.

---

## `os/signal`

```swift
import "os/signal"

let got = try await signal.Wait(.interrupt, .terminate)  // which one came

let stop = try signal.Listen(.interrupt, .terminate)     // graceful shutdown
while let s = await stop.Next() {
    server.Shutdown()
    break
}
stop.Close()
```

| Kind | POSIX | Windows |
| --- | --- | --- |
| `.interrupt` | `SIGINT` | `CTRL_C_EVENT` |
| `.terminate` | `SIGTERM` | `CTRL_CLOSE_EVENT`, `CTRL_SHUTDOWN_EVENT` |
| `.hangup` | `SIGHUP` | `CTRL_LOGOFF_EVENT` |
| `.windowResize` | `SIGWINCH` | console buffer-size event |
| `.posix(int32)` | can be sent with `Child.Signal`, not listened for yet | unsupported |

`Listen` returns a `Listener` rather than an `AsyncSequence`, for the same
reason `Lines()` does. While a `Wait` or `Listener` is active, the signal's default action (exiting
on Ctrl-C) is replaced. It comes back when the last listener ends. The
bridge's handler does only what is async-signal-safe: it writes a byte to
each listener's own pipe. The Vertex side waits on that pipe as it does
on any other descriptor. A child starts with the default handling
whatever this process is listening for.

---

## `os/user`

```swift
import "os/user"

user.Name()          // string?: the login name
user.Home()          // string?
user.CacheDir()      // string?
user.ConfigDir()
user.DataDir()
user.StateDir()
user.RuntimeDir()    // string?: nil where the platform has none
```

| | Linux / Android | macOS | Windows |
| --- | --- | --- | --- |
| `CacheDir` | `$XDG_CACHE_HOME`, `~/.cache` | `~/Library/Caches` | `{FOLDERID_LocalAppData}` |
| `ConfigDir` | `$XDG_CONFIG_HOME`, `~/.config` | `~/Library/Application Support` | `{FOLDERID_RoamingAppData}` |
| `DataDir` | `$XDG_DATA_HOME`, `~/.local/share` | `~/Library/Application Support` | `{FOLDERID_RoamingAppData}` |
| `StateDir` | `$XDG_STATE_HOME`, `~/.local/state` | `~/Library/Application Support` | `{FOLDERID_LocalAppData}` |
| `RuntimeDir` | `$XDG_RUNTIME_DIR` | `nil` | `nil` |

These return the base directory, and the program adds its own name to
it. An XDG variable that holds a relative path is ignored, as the spec
requires.

---

## `os/host`

```swift
import "os/host"

host.Name()                 // string: the hostname
host.OS                     // OSInfo: .Kind (.macOS, .windows, .linux, .android), .Version ("15.3.1")
host.Arch                   // .aarch64 | .x86_64
host.CPUs()                 // CPUInfo: .Logical, .Performance, .Efficiency
host.Memory()               // MemoryInfo: .Total, .Available, in bytes
host.PageSize               // int
host.UptimeNanos()          // int64, time asleep included (time.Duration once os depends on time)
```

`CPUs().Performance` and `CPUs().Efficiency` matter on Apple Silicon and
Intel hybrid chips, for sizing thread pools. On machines where every core
is the same, `Performance == Logical` and `Efficiency == 0`.

---

## `os/term`

```swift
import "os/term"

term.IsTerminal(.stdout)                     // bool
term.GetSize(.stdout)                        // term.Size? (.Columns, .Rows)
let pw = try term.ReadPassword(prompt: "Password: ")   // reads /dev/tty, echo off

try term.Raw(.stdin) {                       // raw mode for a scope, restored on exit or throw
    …
}

let err = term.Style(for: .stderr).Bold().Foreground(.red)
print(err.Render("error:") + " file not found")
term.ColorEnabled(.stderr)                   // the decision Render uses
```

Color follows the conventions in widest use:
- `NO_COLOR` set to any value turns color off ([no-color.org](https://no-color.org)).
- `CLICOLOR_FORCE` set to any value except `0` turns color on, even when
  the output isn't a terminal.
- Otherwise color is on only when the stream is a terminal and `TERM`
  isn't `dumb`.
- On Windows, virtual-terminal processing is enabled on first use.

Until `io` lands, streams are identified by `term.Stream` (`.stdin`,
`.stdout`, `.stderr`). After that, `term.IsTerminal` also accepts any
`io` handle backed by a descriptor.

---

## `os/pty`

> Not started.

```swift
import "os/pty"

var session = try pty.Spawn(process.Command("vim", []), size: (Columns: 120, Rows: 40))
try await session.Write("ihello\u{1b}:wq\r")
let screen = try await session.ReadAll()
session.Resize(Columns: 80, Rows: 24)
let status = try await session.Wait()
```

It uses `openpty` with `posix_spawn` on POSIX and ConPTY on Windows. It is
for terminal emulators, `remote/ssh`, and tests of interactive programs.
Tier 3 in the roadmap. It lands after the others.

---

## Layout

```
os/
  package.vs
  cos/                    one bridge for every os package
    include/cos.h
    cos.cpp               #if defined(_WIN32) / __APPLE__ / __ANDROID__
  env/      *.vs
  process/  *.vs
  signal/   *.vs
  user/     *.vs
  host/     *.vs
  term/     *.vs
  sys/      bindings.vs, text.vs   internal: every @_silgen_name, and shared helpers
  tests/check/main.vs
  examples/run/main.vs    os-run: runs a command and reports how it ended
```

```swift
.target(name: "cos", path: "cos", publicHeadersPath: "include"),
.target(name: "os_sys",     dependencies: ["cos"], path: "sys"),
.target(name: "os_env",     dependencies: ["os_sys"], path: "env"),
.target(name: "os_host",    dependencies: ["os_sys"], path: "host"),
.target(name: "os_user",    dependencies: ["os_sys", "os_env", "os_host"], path: "user"),
.target(name: "os_term",    dependencies: ["os_sys", "os_env"], path: "term"),
.target(name: "os_signal",  dependencies: ["os_sys"], path: "signal"),
.target(name: "os_process", dependencies: ["os_sys", "os_env", "os_host", "os_signal"], path: "process"),
```

`cos` follows [vsc/stdlib/GUIDELINES.md](https://github.com/vertex-language/vsc/blob/main/stdlib/GUIDELINES.md):
functions are named `cos_*`, it uses plain C types, returns negative error
codes, and never blocks a thread. The runtime isn't extended for any of
this. The only thing `os` needs from the runtime is `vertex_task_wait_fd`.

---

## Platforms

| | macOS (aarch64) | Windows (x86_64) | Android (aarch64) |
| --- | --- | --- | --- |
| Built and tested | yes, `tests/check` | not compiled yet | not compiled yet |
| Pipes and waits | parked on the executor | block the thread (`cos_pollable() == 0`) | parked on the executor |
| Child exit | kqueue `EVFILT_PROC` | `WaitForSingleObject` | pidfd, or a watcher thread |

`vsc build -target x86_64-windows` fails in every repository with a C++
bridge, `fs` and `net` included, because the Windows headers aren't found.
Until that's fixed, the Windows branch of `cos` hasn't been compiled. It
follows the same header, and the argument quoting follows the
`CommandLineToArgvW` rules.

On Windows, `Command.Output()` reads stdout to its end before it reads
stderr, because the runtime there runs one task at a time and pipe reads
block. A child that writes a lot to both streams can stall until the
runtime has worker threads on Windows.

## Build and test

```bash
vsc run check
```

## What it replaced

| Where | Workaround | Now |
| --- | --- | --- |
| `net/tcp/bindings.vs` | `vertex_pal_getenv`, `vertex_pal_cpus` (the runtime's private layer) to work out the worker pool's size again | `vertex_task_workers()`, a new runtime entry point: the pool is the executor's to count, so the runtime answers rather than `os` |
| `ui/tests/lifecycle/main.vs` | `@_silgen_name("popen")`, `("pclose")`, `("clock_gettime_nsec_np")` | `process.Command` with a piped stdout, and `time.Instant` |
| `remote/rdpviewer/main.vs` | `@_silgen_name("getpass")`, `("getenv")` | `env.Get`, `term.ReadPassword(prompt:)` |
| `ui/examples/*`, `remote/tests/*` | reading `CommandLine.arguments` by hand | left for `cli` |

## Roadmap

| Step | Packages |
| --- | --- |
| 1 | ✅ `os/env`, `os/process`, `os/host`, `os/term`, and `net/tcp`, `ui` and `remote` moved off their bindings |
| 2 | ✅ `os/signal`, `os/user` |
| 3 | `os/pty` |

## Open questions

1. **`CommandLine.arguments`.** Keep it as an alias for Swift
   compatibility, or deprecate it in `.vs` code in favor of
   `process.Args`?
2. **`env.Set` without `unsafe`.** Keep the documented hazard for now, or
   hold `Set` and `Remove` back until the language has an `unsafe`
   marker?
3. **Kill on drop.** Should an unwaited `Child` be killed when it's
   released, as with tokio's `kill_on_drop`? Or should that wait for
   structured concurrency, so a task group's cancellation owns the child?
4. **`os/dylib`.** Loading shared libraries at runtime (`dlopen` and
   `LoadLibraryW`). Should it be part of this repository, or part of a
   future `runtime/` package with `runtime/debug`?

---

## License

See [LICENSE](LICENSE).
