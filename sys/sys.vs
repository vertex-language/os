package sys

// What the os packages share from sys.cpp (module os.sys), for their
// Vertex side. Each os package calls its own module for the rest.

// Code is the error codes every os call returns, negative, as sys.cpp's
// Err has them.
public enum Code {
    public static let ok: int32 = Err.ok
    public static let generic: int32 = Err.generic
    public static let notFound: int32 = Err.notFound
    public static let permission: int32 = Err.permission
    public static let invalid: int32 = Err.invalid
    public static let wouldBlock: int32 = Err.wouldBlock
    public static let unsupported: int32 = Err.unsupported
    public static let noMemory: int32 = Err.noMemory
    public static let interrupted: int32 = Err.interrupted
    public static let brokenPipe: int32 = Err.brokenPipe
    public static let notATerminal: int32 = Err.notATerminal
}

// LastError is the last OS error code: errno, or GetLastError on Windows.
public func LastError() -> int32 {
    return lastError()
}

// Pollable reports whether descriptors the os packages hand out can be
// waited on through the runtime. Where not (Windows), reads and waits
// block the thread.
public func Pollable() -> bool {
    return pollable() != 0
}

// Read is non-blocking where Pollable: Code.wouldBlock when nothing is
// ready. 0 is the end of the stream.
public func Read(_ fd: int32, _ buf: UnsafeMutableRawPointer, _ count: int64) -> int64 {
    return readFd(fd, buf, count)
}

public func Write(_ fd: int32, _ buf: UnsafeRawPointer, _ count: int64) -> int64 {
    return writeFd(fd, buf, count)
}

public func Close(_ fd: int32) -> int32 {
    return closeFd(fd)
}

// WaitFd holds the thread until fd can be read (events 1) or written (2).
public func WaitFd(_ fd: int32, _ events: int32) -> int32 {
    return waitFd(fd, events)
}

// FlushStdio writes out what C stdio holds for stdout and stderr, so that
// a write straight to fd 1 or 2 comes after print's output.
public func FlushStdio() {
    flushStdio()
}

// The runtime's wait: parks the task until fd can be read (1) or written
// (2). 1 ready, 0 timed out. Outside a task the thread waits.
@_silgen_name("vertex_task_wait_fd")
public func vertex_task_wait_fd(_ fd: int32, _ events: int32, _ timeoutNanos: int64) async -> int32
