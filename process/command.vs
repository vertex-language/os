package process

import "os/sys"
import "os/env"
import "os/host"
import "os/signal"

/// What a child's standard stream is connected to.
public enum Stdio: Equatable {
    /// This process's own stream (the default).
    case inherit
    /// A pipe this process reads or writes: `Child.Stdin`, `Stdout`, `Stderr`.
    case pipe
    /// Nothing: reads see the end at once, writes are discarded.
    case null
    /// A file: read from for stdin, created or truncated for the others.
    case file(string)

    var code: int32 {
        switch self {
        case .inherit:
            return 0
        case .pipe:
            return 1
        case .null:
            return 2
        case .file:
            return 3
        }
    }

    var path: string {
        switch self {
        case .file(let p):
            return p
        default:
            return ""
        }
    }
}

/// A program to run, and how. Arguments are an array and are never read by
/// a shell, so nothing in them can be injected; to use a shell, name it:
/// `Command("/bin/sh", ["-c", script])`.
///
///     var cmd = process.Command("ffmpeg", ["-i", "in.mp4", "out.webm"])
///     cmd.Dir = "/tmp/work"
///     cmd.Env["FFREPORT"] = "1"
///     cmd.Stdout = .pipe
///     let child = try cmd.Spawn()
public struct Command {
    /// The program: a path, or a name looked up on PATH.
    public var Program: string
    public var Args: [string]
    /// The directory the child starts in; this process's where nil.
    public var Dir: string?
    /// Variables set for the child, over the environment it inherits.
    public var Env: [string: string]
    /// Start the child with only `Env`, inheriting nothing.
    public var ClearEnv: bool
    public var Stdin: Stdio
    public var Stdout: Stdio
    public var Stderr: Stdio

    public init(_ program: string, _ args: [string] = []) {
        Program = program
        Args = args
        Dir = nil
        Env = [:]
        ClearEnv = false
        Stdin = .inherit
        Stdout = .inherit
        Stderr = .inherit
    }

    /// Starts the child and returns at once.
    public func Spawn() throws -> Child {
        guard let path = resolve() else {
            throw ProcessError.notFound(Program)
        }
        var argv: [string] = [Program]
        for a in Args {
            argv.append(a)
        }
        let args = sys.Blob(argv)
        var envCount: int32 = -1
        var envBlob: [CChar] = [0]
        if ClearEnv || !Env.isEmpty {
            let vars = environment()
            envCount = int32(vars.count)
            envBlob = sys.Blob(vars)
        }
        let modes: [int32] = [Stdin.code, Stdout.code, Stderr.code]
        var out = [int64](repeating: -1, count: 5)
        var rc: int32 = 0
        let dir = Dir ?? ""
        let hasDir = Dir != nil
        let files = sys.Blob([Stdin.path, Stdout.path, Stderr.path])
        path.withCString { p in
            dir.withCString { d in
                args.withUnsafeBufferPointer { a in
                    envBlob.withUnsafeBufferPointer { e in
                        modes.withUnsafeBufferPointer { m in
                            files.withUnsafeBufferPointer { f in
                                out.withUnsafeMutableBufferPointer { o in
                                    rc = sys.cos_spawn(p, a.baseAddress!, int32(argv.count),
                                                       envCount >= 0 ? e.baseAddress : nil, envCount,
                                                       hasDir ? d : nil,
                                                       m.baseAddress!, f.baseAddress!,
                                                       o.baseAddress!)
                                }
                            }
                        }
                    }
                }
            }
        }
        if rc != 0 {
            throw errorFor(rc, Program)
        }
        return Child(pid: out[0], stdin: int32(out[1]), stdout: int32(out[2]), stderr: int32(out[3]),
                     exitFd: int32(out[4]))
    }

    /// Runs the child to its end, capturing its stdout and stderr (whatever
    /// they were set to) with stdin from nothing unless it was set. Does
    /// not throw for a non-zero exit: that is in `Status`.
    public func Output() async throws -> Output {
        var cmd = self
        cmd.Stdout = .pipe
        cmd.Stderr = .pipe
        if cmd.Stdin == .inherit {
            cmd.Stdin = .null
        }
        let child = try cmd.Spawn()
        // stderr is drained alongside stdout, so a child that fills one
        // pipe while this reads the other does not stop.
        let errPipe = child.Stderr!
        let errTask = Task { () async -> Drained in
            return await drain(errPipe)
        }
        let stdout = try await child.Stdout!.ReadAll()
        let stderr = await errTask.value
        let status = try await child.Wait()
        if stderr.Failed {
            throw ProcessError.system(code: stderr.Code, context: "read stderr")
        }
        return process.Output(Status: status, Stdout: stdout, Stderr: stderr.Bytes)
    }

    /// Runs the child to its end with its streams as set, and returns how
    /// it ended.
    public func Status() async throws -> ExitStatus {
        let child = try Spawn()
        return try await child.Wait()
    }

    // resolve is Program as a path to execute: as it is where it names a
    // directory, and found on PATH -- the child's, where Env sets one --
    // where it is a bare name.
    func resolve() -> string? {
        if hasSeparator(Program) {
            return Program
        }
        return Find(Program, path: Env["PATH"] ?? env.Get("PATH") ?? "")
    }

    // environment is the child's, as NAME=value strings.
    func environment() -> [string] {
        var vars: [string: string] = [:]
        if !ClearEnv {
            for (k, v) in env.All() {
                vars[k] = v
            }
        }
        for (k, v) in Env {
            vars[k] = v
        }
        var out: [string] = []
        for (k, v) in vars {
            out.append(k + "=" + v)
        }
        return out
    }
}

struct Drained {
    let Bytes: [uint8]
    let Failed: bool
    let Code: int32
}

func drain(_ pipe: PipeReader) async -> Drained {
    do {
        let bytes = try await pipe.ReadAll()
        return Drained(Bytes: bytes, Failed: false, Code: 0)
    } catch {
        return Drained(Bytes: [], Failed: true, Code: sys.cos_last_error())
    }
}

/// A running child process.
public final class Child {
    /// The child's process id.
    public let ID: int64
    /// Its stdin, where that was `.pipe`.
    public let Stdin: PipeWriter?
    /// Its stdout, where that was `.pipe`.
    public let Stdout: PipeReader?
    /// Its stderr, where that was `.pipe`.
    public let Stderr: PipeReader?
    var exitFd: int32
    var status: ExitStatus?

    init(pid: int64, stdin: int32, stdout: int32, stderr: int32, exitFd: int32) {
        ID = pid
        Stdin = stdin >= 0 ? PipeWriter(stdin) : nil
        Stdout = stdout >= 0 ? PipeReader(stdout) : nil
        Stderr = stderr >= 0 ? PipeReader(stderr) : nil
        self.exitFd = exitFd
        status = nil
    }

    deinit {
        // A child nobody waited for is reaped where it has ended, so it
        // does not stay a zombie; one still running is left to run.
        if status == nil {
            _ = try? self.TryWait()
        }
        if exitFd >= 0 {
            sys.cos_release_child(ID, exitFd)
            exitFd = -1
        }
    }

    /// Waits for the child to end. In a task, the task parks meanwhile.
    public func Wait() async throws -> ExitStatus {
        if let s = status {
            return s
        }
        // Closing stdin first means a child reading it to its end is not
        // left waiting for more.
        Stdin?.Close()
        while true {
            if let s = try poll(block: exitFd < 0) {
                return s
            }
            if exitFd >= 0 {
                _ = await sys.vertex_task_wait_fd(exitFd, 1, -1)
            }
        }
    }

    /// How the child ended, or nil while it is still running. Never waits.
    public func TryWait() throws -> ExitStatus? {
        if let s = status {
            return s
        }
        return try poll(block: false)
    }

    /// Asks the child to stop: SIGTERM, or CTRL_BREAK on Windows. Does
    /// nothing once the child has been waited for, when its id may already
    /// belong to another process.
    public func Terminate() {
        if status == nil {
            _ = sys.cos_send_signal(ID, 2, 0)
        }
    }

    /// Stops the child at once: SIGKILL, or TerminateProcess on Windows.
    /// Does nothing once the child has been waited for.
    public func Kill() {
        if status == nil {
            _ = sys.cos_send_signal(ID, 5, 0)
        }
    }

    /// Sends the child a signal.
    public func Signal(_ kind: signal.Kind) throws {
        if status != nil {
            return
        }
        var posix: int32 = 0
        if case .posix(let n) = kind {
            posix = n
        }
        let rc = sys.cos_send_signal(ID, kind.Code, posix)
        if rc != 0 {
            throw errorFor(rc, "signal")
        }
    }

    func poll(block: bool) throws -> ExitStatus? {
        var kind: int32 = 0
        var code: int32 = 0
        let rc = sys.cos_try_wait(ID, exitFd, block ? 1 : 0, &kind, &code)
        if rc < 0 {
            throw errorFor(rc, "wait")
        }
        if rc == 0 {
            return nil
        }
        let s: ExitStatus = kind == 2 ? .signaled(code) : .exited(code)
        status = s
        if exitFd >= 0 {
            sys.cos_release_child(ID, exitFd)
            exitFd = -1
        }
        return s
    }
}

/// Runs a program to its end and returns what it wrote, throwing
/// `ProcessError.failed` where it ends unsuccessfully.
///
///     let head = try await process.Run("git", ["rev-parse", "HEAD"])
///     print(head.StdoutText)
public func Run(_ program: string, _ args: [string] = []) async throws -> Output {
    let out = try await Command(program, args).Output()
    if !out.Status.Success {
        throw ProcessError.failed(out)
    }
    return out
}

/// The path of the program `name` on PATH, as a shell's `which` finds it,
/// or nil. On Windows the extensions in PATHEXT are tried too.
public func Find(_ name: string) -> string? {
    return Find(name, path: env.Get("PATH") ?? "")
}

func Find(_ name: string, path: string) -> string? {
    if name.isEmpty {
        return nil
    }
    let windows = host.OS.Kind == .windows
    if hasSeparator(name) {
        return executable(name) ? name : nil
    }
    var exts: [string] = [""]
    if windows {
        for e in splitOn(env.Get("PATHEXT") ?? ".COM;.EXE;.BAT;.CMD", 59) {
            exts.append(e)
        }
    }
    for dir in splitOn(path, windows ? 59 : 58) {
        let base = dir.isEmpty ? "." : dir
        for e in exts {
            let candidate = base + (windows ? "\\" : "/") + name + e
            if executable(candidate) {
                return candidate
            }
        }
    }
    return nil
}

func executable(_ path: string) -> bool {
    var ok: int32 = 0
    path.withCString { p in
        ok = sys.cos_is_executable(p)
    }
    return ok == 1
}

func hasSeparator(_ s: string) -> bool {
    for b in s.utf8 {
        if b == 47 || b == 92 {
            return true
        }
    }
    return false
}

func splitOn(_ s: string, _ sep: uint8) -> [string] {
    let bytes = sys.Bytes(s)
    var out: [string] = []
    var start = 0
    var i = 0
    while i <= bytes.count {
        if i == bytes.count || bytes[i] == sep {
            out.append(sys.Text(bytes, from: start, to: i))
            start = i + 1
        }
        i += 1
    }
    return out
}
