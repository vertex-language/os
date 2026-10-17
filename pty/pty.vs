// Package pty runs a command under a pseudo-terminal: what a terminal
// emulator, remote/ssh or a test of an interactive program needs. The
// child sees a terminal on its stdin, stdout and stderr; the parent reads
// what it draws and writes what is typed.
//
//     var cmd = process.Command("/bin/zsh", ["-l"])
//     cmd.Env["TERM"] = "xterm-256color"
//     let session = try pty.Spawn(cmd, size: pty.Size(columns: 120, rows: 40))
//     try await session.Write("echo hi\r")
//     var buf = [uint8](repeating: 0, count: 4096)
//     let n = try await session.Read(into: &buf)
//     try session.Resize(pty.Size(columns: 80, rows: 24))
//     let status = try await session.Wait()
//
// The command's Stdin, Stdout and Stderr settings are ignored: all three
// are the terminal.
package pty

import (
    "os/process"
    "os/sys"
)

/// A terminal's size in character cells, and in pixels where the
/// program drawing it knows them (0 where not).
public struct Size: Equatable {
    public var Columns: int
    public var Rows: int
    public var Width: int
    public var Height: int

    public init(columns: int, rows: int, width: int = 0, height: int = 0) {
        Columns = columns
        Rows = rows
        Width = width
        Height = height
    }
}

/// How a pseudo-terminal operation fails.
public enum PtyError: Error, CustomStringConvertible {
    /// No program by that name on PATH, or no file at that path.
    case notFound(string)
    /// The program exists and this process may not run it.
    case permissionDenied(string)
    /// The platform has no pseudo-terminals here yet (Windows).
    case unsupported
    /// The session was closed.
    case closed
    /// Anything else, with the system's error code.
    case system(code: int32, context: string)

    public var description: string {
        switch self {
        case .notFound(let what): return "program not found: \(what)"
        case .permissionDenied(let what): return "permission denied: \(what)"
        case .unsupported: return "pseudo-terminals are not supported on this platform yet"
        case .closed: return "the terminal is closed"
        case .system(let code, let what): return "system error \(code): \(what)"
        }
    }
}

func errorFor(_ code: int32, _ what: string) -> PtyError {
    switch code {
    case sys.Code.notFound: return .notFound(what)
    case sys.Code.permission: return .permissionDenied(what)
    case sys.Code.unsupported: return .unsupported
    default: return .system(code: sys.LastError(), context: what)
    }
}

/// Starts `command` with a new pseudo-terminal of `size` as its stdin,
/// stdout and stderr, as the leader of a new session with the terminal as
/// its controlling terminal. Returns at once.
public func Spawn(_ command: process.Command, size: Size = Size(columns: 80, rows: 24)) throws -> Session {
    guard let path = command.resolve() else {
        throw PtyError.notFound(command.Program)
    }
    var argv: [string] = [command.Program]
    for a in command.Args { argv.append(a) }
    let args = sys.Blob(argv)
    var envCount: int32 = -1
    var envBlob: [CChar] = [0]
    if command.ClearEnv || !command.Env.isEmpty {
        let vars = command.environment()
        envCount = int32(vars.count)
        envBlob = sys.Blob(vars)
    }
    let dir = command.Dir ?? ""
    let hasDir = command.Dir != nil
    var out = [int64](repeating: -1, count: 3)
    var rc: int32 = 0
    path.withCString { p in
        dir.withCString { d in
            args.withUnsafeBufferPointer { a in
                envBlob.withUnsafeBufferPointer { e in
                    out.withUnsafeMutableBufferPointer { o in
                        rc = ptySpawn(p, a.baseAddress!, int32(argv.count),
                                      envCount >= 0 ? e.baseAddress : nil, envCount,
                                      hasDir ? d : nil,
                                      int32(size.Columns), int32(size.Rows),
                                      o.baseAddress!)
                    }
                }
            }
        }
    }
    if rc != 0 {
        throw errorFor(rc, command.Program)
    }
    let s = Session(pid: out[0], fd: int32(out[1]), exitFd: int32(out[2]), size: size)
    if size.Width > 0 || size.Height > 0 { try? s.Resize(size) }
    return s
}

/// A child running under a pseudo-terminal, and the parent's end of it.
///
/// Reading and writing wait the way `os/process` pipes do: in a task, the
/// task parks until the terminal is ready and the executor runs others.
public final class Session {
    /// The child's process id.
    public let ID: int64
    var fd: int32
    var exitFd: int32
    var status: process.ExitStatus? = nil
    /// The size the terminal was last given.
    public private(set) var Size: Size

    init(pid: int64, fd: int32, exitFd: int32, size: Size) {
        ID = pid
        self.fd = fd
        self.exitFd = exitFd
        Size = size
    }

    deinit {
        Close()
        if status == nil { _ = try? poll() }
        if exitFd >= 0 {
            ptyClose(exitFd)
            exitFd = -1
        }
    }

    /// Whether the parent's end is still open.
    public var IsOpen: bool { return fd >= 0 }

    /// Reads what the child wrote into `buffer`, waiting for at least a
    /// byte. 0 is the end: the child, and everything it started that
    /// held the terminal, has ended, or the session was closed.
    public func Read(into buffer: inout [uint8]) async throws -> int {
        if buffer.isEmpty { return 0 }
        while fd >= 0 {
            let want = int64(buffer.count)
            let me = fd
            let n = buffer.withUnsafeMutableBufferPointer { bp in ptyRead(me, bp.baseAddress!, want) }
            if n >= 0 { return int(n) }
            if n == int64(sys.Code.wouldBlock) {
                _ = await sys.vertex_task_wait_fd(fd, 1, -1)
                continue
            }
            throw errorFor(int32(n), "read terminal")
        }
        return 0
    }

    /// Reads until the end of the stream, everything the child wrote.
    public func ReadAll(limit: int = 64 * 1024 * 1024) async throws -> [uint8] {
        var all: [uint8] = []
        var buf = [uint8](repeating: 0, count: 16384)
        while true {
            let n = try await Read(into: &buf)
            if n == 0 { break }
            all.append(contentsOf: buf[0..<n])
            if all.count > limit { break }
        }
        return all
    }

    /// Writes bytes for the child to read as typed, all of them, waiting
    /// while the terminal's buffer is full.
    public func Write(_ bytes: [uint8]) async throws {
        var off = 0
        while off < bytes.count {
            if fd < 0 { throw PtyError.closed }
            let me = fd
            let start = off
            let n = bytes.withUnsafeBufferPointer { bp in ptyWrite(me, bp.baseAddress! + start, int64(bytes.count - start)) }
            if n >= 0 {
                off += int(n)
                continue
            }
            if n == int64(sys.Code.wouldBlock) {
                _ = await sys.vertex_task_wait_fd(fd, 2, -1)
                continue
            }
            throw errorFor(int32(n), "write terminal")
        }
    }

    /// Writes text, as UTF-8.
    public func Write(_ text: string) async throws {
        try await Write([uint8](text.utf8))
    }

    /// Tells the terminal, and so the child, its new size.
    public func Resize(_ size: Size) throws {
        if fd < 0 { throw PtyError.closed }
        let rc = ptyResize(fd, int32(size.Columns), int32(size.Rows), int32(size.Width), int32(size.Height))
        if rc != 0 { throw errorFor(rc, "resize terminal") }
        Size = size
    }

    /// The name of what the terminal is running in the foreground: the
    /// shell, or the program it started ("vim", "top"). nil once closed.
    public func ForegroundName() -> string? {
        return text(of: { b, n in ptyForeground(self.fd, b, n) })
    }

    /// The working directory of what the terminal is running in the
    /// foreground: where a new terminal opened beside it should start.
    public func ForegroundDir() -> string? {
        return text(of: { b, n in ptyForegroundDir(self.fd, b, n) })
    }

    func text(of fill: (UnsafeMutablePointer<CChar>?, int32) -> int32) -> string? {
        if fd < 0 { return nil }
        var buf = [CChar](repeating: 0, count: 1024)
        var need = buf.withUnsafeMutableBufferPointer { fill($0.baseAddress, int32($0.count)) }
        if need < 0 { return nil }
        if int(need) >= buf.count {
            buf = [CChar](repeating: 0, count: int(need) + 1)
            need = buf.withUnsafeMutableBufferPointer { fill($0.baseAddress, int32($0.count)) }
            if need < 0 { return nil }
        }
        return string(cString: buf)
    }

    /// Waits for the child to end. In a task, the task parks meanwhile.
    public func Wait() async throws -> process.ExitStatus {
        if let s = status { return s }
        while true {
            if let s = try poll() { return s }
            if exitFd >= 0 {
                _ = await sys.vertex_task_wait_fd(exitFd, 1, -1)
            } else {
                // No exit descriptor here: look again shortly.
                try? await Task.sleep(nanoseconds: 20_000_000)
            }
        }
    }

    /// How the child ended, or nil while it runs. Never waits.
    public func TryWait() throws -> process.ExitStatus? {
        if let s = status { return s }
        return try poll()
    }

    func poll() throws -> process.ExitStatus? {
        var kind: int32 = 0
        var code: int32 = 0
        let rc = ptyTryWait(ID, &kind, &code)
        if rc < 0 { throw errorFor(rc, "wait") }
        if rc == 0 { return nil }
        let s: process.ExitStatus = kind == PtyExit.signaled ? .signaled(code) : .exited(code)
        status = s
        if exitFd >= 0 {
            ptyClose(exitFd)
            exitFd = -1
        }
        return s
    }

    /// Sends the child's process group a POSIX signal. Does nothing once
    /// the child has been waited for.
    public func Signal(_ posix: int32) {
        if status == nil { _ = ptySignal(ID, posix) }
    }

    /// Hangs up: SIGHUP, as closing a terminal window does.
    public func Hangup() { Signal(1) }

    /// Stops the child at once: SIGKILL.
    public func Kill() { Signal(9) }

    /// Closes the parent's end. The child sees the terminal hang up.
    public func Close() {
        if fd >= 0 {
            ptyClose(fd)
            fd = -1
        }
    }
}
