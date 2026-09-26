package process

import "io"
import "os/sys"

/// This process's standard input: an io.Reader. A read waits for input,
/// holding the thread, as reading a terminal or a pipe does; it meets
/// io.AsyncReader too, with the same wait.
public struct StandardInput: io.Reader, io.AsyncReader {
    public init() {}

    public mutating func Read(into buffer: inout [uint8]) throws -> int {
        if buffer.isEmpty {
            return 0
        }
        while true {
            var n: int64 = 0
            let want = int64(buffer.count)
            buffer.withUnsafeMutableBytes { raw in
                n = sys.Read(0, raw.baseAddress!, want)
            }
            if n >= 0 {
                return int(n)
            }
            if n == int64(sys.Code.interrupted) {
                continue
            }
            if n == int64(sys.Code.wouldBlock) {
                // Someone made fd 0 non-blocking: wait for it as a
                // blocking read would have.
                _ = sys.WaitFd(0, 1)
                continue
            }
            throw ProcessError.system(code: sys.LastError(), context: "read stdin")
        }
    }
}

/// This process's standard output or error: an io.Writer, and an
/// io.AsyncWriter with the same writes. What `print` has buffered is
/// written out first, so output stays in order.
public struct StandardOutput: io.Writer, io.AsyncWriter {
    let fd: int32

    init(_ fd: int32) {
        self.fd = fd
    }

    public mutating func Write(_ bytes: borrowing [uint8]) throws {
        sys.FlushStdio()
        var off = 0
        while off < bytes.count {
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
            if n == int64(sys.Code.interrupted) {
                continue
            }
            if n == int64(sys.Code.wouldBlock) {
                _ = sys.WaitFd(fd, 2)
                continue
            }
            throw ProcessError.system(code: sys.LastError(), context: fd == 1 ? "write stdout" : "write stderr")
        }
    }

    /// Writes `text` as UTF-8.
    public mutating func Write(_ text: string) throws {
        try Write(sys.Bytes(text))
    }

    /// Writes out what `print` has buffered.
    public mutating func Flush() throws {
        sys.FlushStdio()
    }
}

/// This process's standard input.
public var Stdin: StandardInput {
    return StandardInput()
}

/// This process's standard output.
public var Stdout: StandardOutput {
    return StandardOutput(1)
}

/// This process's standard error.
public var Stderr: StandardOutput {
    return StandardOutput(2)
}
