package process

/// How a process ended.
public enum ExitStatus: Equatable, CustomStringConvertible {
    /// It exited, with this code.
    case exited(int32)
    /// A signal ended it (POSIX only), with the signal's number.
    case signaled(int32)

    /// Whether it exited with 0.
    public var Success: bool {
        switch self {
        case .exited(let code):
            return code == 0
        case .signaled:
            return false
        }
    }

    /// The exit code, or nil where a signal ended it.
    public var Code: int32? {
        switch self {
        case .exited(let code):
            return code
        case .signaled:
            return nil
        }
    }

    public var description: string {
        switch self {
        case .exited(let code):
            return "exited with \(code)"
        case .signaled(let sig):
            return "ended by signal \(sig)"
        }
    }
}

/// What a finished process wrote, and how it ended.
public struct Output {
    public let Status: ExitStatus
    public let Stdout: [uint8]
    public let Stderr: [uint8]

    /// Stdout as UTF-8 text.
    public var StdoutText: string {
        return textOf(Stdout)
    }

    /// Stderr as UTF-8 text.
    public var StderrText: string {
        return textOf(Stderr)
    }
}

func textOf(_ bytes: [uint8]) -> string {
    var chars: [CChar] = []
    for b in bytes {
        if b != 0 {
            chars.append(CChar(truncatingIfNeeded: b))
        }
    }
    chars.append(0)
    return string(cString: chars)
}
