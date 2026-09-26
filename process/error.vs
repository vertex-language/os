package process

import "os/sys"

/// ProcessError is how starting, running or waiting for a process fails.
public enum ProcessError: Error, CustomStringConvertible {
    /// No program by that name on PATH, or no file at that path.
    case notFound(string)
    /// The program exists and this process may not run it.
    case permissionDenied(string)
    /// `Run`'s program ended unsuccessfully; what it wrote is in the output.
    case failed(Output)
    /// Anything else, with the system's error code.
    case system(code: int32, context: string)

    public var description: string {
        switch self {
        case .notFound(let what):
            return "program not found: \(what)"
        case .permissionDenied(let what):
            return "permission denied: \(what)"
        case .failed(let out):
            let err = out.StderrText
            if err.isEmpty {
                return "process \(out.Status)"
            }
            return "process \(out.Status): \(err)"
        case .system(let code, let what):
            return "system error \(code): \(what)"
        }
    }
}

func errorFor(_ code: int32, _ what: string) -> ProcessError {
    switch code {
    case sys.Code.notFound:
        return .notFound(what)
    case sys.Code.permission:
        return .permissionDenied(what)
    default:
        return .system(code: sys.LastError(), context: what)
    }
}
