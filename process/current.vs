package process

import "os/sys"

/// The program's arguments, the program's own name first.
public var Args: [string] {
    return CommandLine.arguments
}

/// This process's id.
public var ID: int32 {
    return sys.cos_pid()
}

/// The id of the process that started this one; 0 on Windows, which does
/// not keep one.
public var ParentID: int32 {
    return sys.cos_ppid()
}

/// The absolute path of the running executable, links resolved, or nil
/// where the system cannot say.
public func ExecutablePath() -> string? {
    return sys.Fill { buf, max in
        return sys.cos_executable_path(buf, max)
    }.Text
}

/// The directory relative paths are resolved against.
public func CurrentDir() throws -> string {
    let f = sys.Fill { buf, max in
        return sys.cos_current_dir(buf, max)
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
        rc = sys.cos_set_current_dir(p)
    }
    if rc != 0 {
        throw errorFor(rc, path)
    }
}

/// Ends the process with `code`, after what `print` has buffered is
/// written out. Never returns.
public func Exit(_ code: int32) -> Never {
    sys.cos_exit(code)
}

/// Ends the process now: nothing buffered is written, nothing else runs.
public func Abort() -> Never {
    sys.cos_abort()
}
