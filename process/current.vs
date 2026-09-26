package process

import "os/sys"

/// The program's arguments, the program's own name first.
public var Args: [string] {
    return CommandLine.arguments
}

/// This process's id.
public var ID: int32 {
    return processId()
}

/// The id of the process that started this one; 0 on Windows, which does
/// not keep one.
public var ParentID: int32 {
    return parentProcessId()
}

/// The absolute path of the running executable, links resolved, or nil
/// where the system cannot say.
public func ExecutablePath() -> string? {
    return sys.Fill { buf, max in
        return executablePath(buf, max)
    }.Text
}

/// The directory relative paths are resolved against.
public func CurrentDir() throws -> string {
    let f = sys.Fill { buf, max in
        return currentDir(buf, max)
    }
    guard let d = f.Text else {
        throw errorFor(f.Code, "current directory")
    }
    return d
}

/// Changes the directory relative paths are resolved against, for the
/// whole process.
public func SetCurrentDir(_ path: string) throws {
    var rc: int32 = 0
    path.withCString { p in
        rc = changeDir(p)
    }
    if rc != 0 {
        throw errorFor(rc, path)
    }
}

/// Ends the process with `code`, after what `print` has buffered is
/// written out. Never returns.
public func Exit(_ code: int32) -> Never {
    exitProcess(code)
}

/// Ends the process now: nothing buffered is written, nothing else runs.
public func Abort() -> Never {
    abortProcess()
}
