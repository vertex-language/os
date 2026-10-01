# os/process

This process, and the child processes it starts.

```vertex
import "os/process"
```

## Types

- **`Stdio`** (enum): What a child's standard stream is connected to.
- **`Command`** (struct): A program to run, and how.
- **`Child`** (class): A running child process.
- **`ProcessError`** (enum): ProcessError is how starting, running or waiting for a process fails.
- **`PipeReader`** (class): The parent's end of a child's stdout or stderr: an io.AsyncReader, so io.Copy, io.ReadToEnd and io.AsyncBufferedReader take it.
- **`PipeWriter`** (class): The parent's end of a child's stdin: an io.AsyncWriter.
- **`ExitStatus`** (enum): How a process ended.
- **`Output`** (struct): What a finished process wrote, and how it ended.
- **`StandardInput`** (struct): This process's standard input: an io.Reader.
- **`StandardOutput`** (struct): This process's standard output or error: an io.Writer, and an io.AsyncWriter with the same writes.

## Functions

- `func Run(_ program: string, _ args: [string] = []) async throws -> Output`: Runs a program to its end and returns what it wrote, throwing `ProcessError.failed` where it ends unsuccessfully.
- `func Find(_ name: string) -> string?`: The path of the program `name` on PATH, as a shell's `which` finds it, or nil. On Windows the extensions in PATHEXT are tried too.
- `func ExecutablePath() -> string?`: The absolute path of the running executable, links resolved, or nil where the system cannot say.
- `func CurrentDir() throws -> string`: The directory relative paths are resolved against.
- `func SetCurrentDir(_ path: string) throws`: Changes the directory relative paths are resolved against, for the whole process.
- `func Exit(_ code: int32) -> Never`: Ends the process with `code`, after what `print` has buffered is written out. Never returns.
- `func Abort() -> Never`: Ends the process now: nothing buffered is written, nothing else runs.

Part of the [`os`](https://github.com/vertex-language/os) repository.
