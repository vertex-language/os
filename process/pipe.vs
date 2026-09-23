package process

import "os/sys"

/// The parent's end of a child's stdout or stderr.
///
/// Reading waits the way `net/tcp` does: in a task, the task parks until
/// the pipe has something and the executor runs other tasks meanwhile.
public final class PipeReader {
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
                n = sys.cos_read(fd, raw.baseAddress!, want)
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
            throw ProcessError.system(code: sys.cos_last_error(), context: "read pipe")
        }
        return 0
    }

    /// Everything until the end of the stream.
    public func ReadAll() async throws -> [uint8] {
        var out: [uint8] = []
        var buf = [uint8](repeating: 0, count: 16384)
        while true {
            let n = try await Read(into: &buf)
            if n == 0 {
                return out
            }
            var i = 0
            while i < n {
                out.append(buf[i])
                i += 1
            }
        }
    }

    /// Everything until the end of the stream, as UTF-8 text.
    public func ReadText() async throws -> string {
        return textOf(try await ReadAll())
    }

    /// The stream a line at a time.
    ///
    ///     let lines = child.Stdout!.Lines()
    ///     while let line = try await lines.Next() { print(line) }
    public func Lines() -> LineReader {
        return LineReader(self)
    }

    /// Closes the parent's end. A child writing to it afterwards gets
    /// SIGPIPE, or EPIPE where it ignores that.
    public func Close() {
        if fd >= 0 {
            _ = sys.cos_close(fd)
            fd = -1
        }
    }
}

/// A pipe read a line at a time. A line is what comes before "\n", with a
/// "\r" before it dropped too; the last line need not end in one.
public final class LineReader {
    let pipe: PipeReader
    var pending: [uint8]
    var start: int
    var done: bool

    init(_ pipe: PipeReader) {
        self.pipe = pipe
        pending = []
        start = 0
        done = false
    }

    /// The next line, waiting for it; nil at the end of the stream.
    public func Next() async throws -> string? {
        var buf = [uint8](repeating: 0, count: 4096)
        while true {
            var i = start
            while i < pending.count {
                if pending[i] == 10 {
                    var end = i
                    if end > start && pending[end - 1] == 13 {
                        end -= 1
                    }
                    let line = sys.Text(pending, from: start, to: end)
                    start = i + 1
                    compact()
                    return line
                }
                i += 1
            }
            if done {
                if start < pending.count {
                    let line = sys.Text(pending, from: start, to: pending.count)
                    start = pending.count
                    return line
                }
                return nil
            }
            let n = try await pipe.Read(into: &buf)
            if n == 0 {
                done = true
                continue
            }
            var j = 0
            while j < n {
                pending.append(buf[j])
                j += 1
            }
        }
    }

    // compact drops what has been handed out once it is most of the buffer.
    func compact() {
        if start < 4096 || start * 2 < pending.count {
            return
        }
        var rest: [uint8] = []
        var i = start
        while i < pending.count {
            rest.append(pending[i])
            i += 1
        }
        pending = rest
        start = 0
    }
}

/// The parent's end of a child's stdin.
public final class PipeWriter {
    var fd: int32

    init(_ fd: int32) {
        self.fd = fd
    }

    deinit {
        Close()
    }

    /// Writes all of `bytes`, waiting where the pipe is full.
    public func Write(_ bytes: [uint8]) async throws {
        var off = 0
        while off < bytes.count {
            if fd < 0 {
                throw ProcessError.system(code: 0, context: "write to a closed pipe")
            }
            var n: int64 = 0
            let left = int64(bytes.count - off)
            let at = off
            bytes.withUnsafeBytes { raw in
                n = sys.cos_write(fd, raw.baseAddress! + at, left)
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
            throw ProcessError.system(code: sys.cos_last_error(), context: "write pipe")
        }
    }

    /// Writes `text` as UTF-8.
    public func Write(_ text: string) async throws {
        try await Write(sys.Bytes(text))
    }

    /// Closes the parent's end: the child reads the end of its stdin.
    public func Close() {
        if fd >= 0 {
            _ = sys.cos_close(fd)
            fd = -1
        }
    }
}
