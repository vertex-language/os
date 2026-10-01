# os/sys

Raw operating-system calls under the os package: reading, writing and closing file descriptors, and the platform's last error code.

```vertex
import "os/sys"
```

## Types

- **`Code`** (enum): Code is the error codes every os call returns, negative, as sys.cpp's Err has them.
- **`Filled`** (struct): Filled is what a buffer-filling os call gave back: the text, or the negative code it failed with.

## Functions

- `func LastError() -> int32`: LastError is the last OS error code: errno, or GetLastError on Windows.
- `func Pollable() -> bool`: Pollable reports whether descriptors the os packages hand out can be waited on through the runtime. Where not (Windows), reads and waits block the thread.
- `func Read(_ fd: int32, _ buf: UnsafeMutableRawPointer, _ count: int64) -> int64`: Read is non-blocking where Pollable: Code.wouldBlock when nothing is ready. 0 is the end of the stream.
- `func Write(_ fd: int32, _ buf: UnsafeRawPointer, _ count: int64) -> int64`
- `func Close(_ fd: int32) -> int32`
- `func WaitFd(_ fd: int32, _ events: int32) -> int32`: WaitFd holds the thread until fd can be read (events 1) or written (2).
- `func FlushStdio()`: FlushStdio writes out what C stdio holds for stdout and stderr, so that a write straight to fd 1 or 2 comes after print's output.
- `func vertex_task_wait_fd(_ fd: int32, _ events: int32, _ timeoutNanos: int64) async -> int32`
- `func Fill(_ call: (UnsafeMutablePointer<CChar>?, int32) -> int32) -> Filled`: Fill calls a C++ function that fills a buffer and returns the length the whole value needs, growing the buffer until it fits.
- `func Blob(_ items: [string]) -> [CChar]`: Blob lays strings end to end, each NUL-terminated, as process.cpp's spawn takes its arguments and environment.
- and 2 more

Part of the [`os`](https://github.com/vertex-language/os) repository.
