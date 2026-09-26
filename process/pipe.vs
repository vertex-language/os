package process

import "io"
import "os/sys"

/// The parent's end of a child's stdout or stderr: an io.AsyncReader, so
/// io.Copy, io.ReadToEnd and io.AsyncBufferedReader take it.
///
/// Reading waits the way `net/tcp` does: in a task, the task parks until
/// the pipe has something and the executor runs other tasks meanwhile.
public final class PipeReader: io.AsyncReader, io.Closer {
    var fd: int32

    init(_ fd: int32) {
        self.fd = fd
    }

    deinit {
        Close()
    }

    /// Reads what is there into `buffer`, waiting for at least a byte.
    /// Returns how many bytes it read; 0 is the end of the stream (the
    /// child closed it, or ended).
    public func Read(into buffer: inout [uint8]) async throws -> int {
        if buffer.isEmpty {
            return 0
        }
        while fd >= 0 {
            var n: int64 = 0
            let want = int64(buffer.count)
            buffer.withUnsafeMutableBytes { raw in
                n = sys.Read(fd, raw.baseAddress!, want)
            }
            if n >= 0 {
                return int(n)
            }
            if n == int64(sys.Code.wouldBlock) {
                _ = await sys.vertex_task_wait_fd(fd, 1, -1)
                continue
            }
            if n == int64(sys.Code.interrupted) {
                continue
            }
            throw ProcessError.system(code: sys.LastError(), context: "read pipe")
        }
        return 0
    }

    /// Everything until the end of the stream; throws io.IoError.tooLarge
    /// past `limit` bytes (64 MiB where none is given).
    public func ReadToEnd(limit: int = 64 * 1024 * 1024) async throws -> [uint8] {
        var me = self
        return try await io.ReadToEnd(&me, limit: limit)
    }

    /// Everything until the end of the stream, as UTF-8 text.
    public func ReadText(limit: int = 64 * 1024 * 1024) async throws -> string {
        var me = self
        return try await io.ReadText(&me, limit: limit)
    }

    /// The stream a line at a time.
    ///
    ///     for try await line in child.Stdout!.Lines() { print(line) }
    public func Lines() -> io.AsyncLines<PipeReader> {
        return io.AsyncLines(self)
    }

    /// Closes the parent's end. A child writing to it afterwards gets
    /// SIGPIPE, or EPIPE where it ignores that.
    public func Close() {
        if fd >= 0 {
            _ = sys.Close(fd)
            fd = -1
        }
    }
}

/// The parent's end of a child's stdin: an io.AsyncWriter.
public final class PipeWriter: io.AsyncWriter, io.Closer {
    var fd: int32

    init(_ fd: int32) {
        self.fd = fd
    }

    deinit {
        Close()
    }

    /// Writes all of `bytes`, waiting where the pipe is full.
    public func Write(_ bytes: borrowing [uint8]) async throws {
        var off = 0
        while off < bytes.count {
            if fd < 0 {
                throw ProcessError.system(code: 0, context: "write to a closed pipe")
            }
            var n: int64 = 0
            let left = int64(bytes.count - off)
            let at = off
            bytes.withUnsafeBytes { raw in
                n = sys.Write(fd, raw.baseAddress! + at, left)
            }
            if n >= 0 {
                off += int(n)
                continue
            }
            if n == int64(sys.Code.wouldBlock) {
                _ = await sys.vertex_task_wait_fd(fd, 2, -1)
                continue
            }
            if n == int64(sys.Code.interrupted) {
                continue
            }
            throw ProcessError.system(code: sys.LastError(), context: "write pipe")
        }
    }

    /// Writes `text` as UTF-8.
    public func Write(_ text: string) async throws {
        try await Write(sys.Bytes(text))
    }

    /// Nothing: every Write is in the pipe when it returns.
    public func Flush() {}

    /// Closes the parent's end: the child reads the end of its stdin.
    public func Close() {
        if fd >= 0 {
            _ = sys.Close(fd)
            fd = -1
        }
    }
}
