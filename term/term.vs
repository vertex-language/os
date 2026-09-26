package term

import (
    "os/env"
    "os/sys"
)

/// One of the process's standard streams.
public enum Stream: Equatable {
    case stdin
    case stdout
    case stderr

    var fd: int32 {
        switch self {
        case .stdin:
            return 0
        case .stdout:
            return 1
        case .stderr:
            return 2
        }
    }
}

/// A terminal's size in character cells.
public struct Size {
    public let Columns: int
    public let Rows: int
}

/// TermError is how a terminal operation fails.
public enum TermError: Error, CustomStringConvertible {
    case notATerminal
    case tooLong
    case system(code: int32)

    public var description: string {
        switch self {
        case .notATerminal:
            return "not a terminal"
        case .tooLong:
            return "input longer than the buffer"
        case .system(let code):
            return "terminal error \(code)"
        }
    }
}

/// Whether the stream is connected to a terminal rather than a file or pipe.
public func IsTerminal(_ s: Stream) -> bool {
    return isTerminal(s.fd) == 1
}

/// The terminal's size, or nil where the stream is not a terminal.
public func GetSize(_ s: Stream) -> Size? {
    var out = [int32](repeating: 0, count: 2)
    var rc: int32 = 0
    out.withUnsafeMutableBufferPointer { p in
        rc = termSize(s.fd, p.baseAddress!)
    }
    if rc != 0 {
        return nil
    }
    return Size(Columns: int(out[0]), Rows: int(out[1]))
}

/// Prints `prompt` and reads a line from the terminal with echo off. It
/// reads the controlling terminal, not stdin, so a password comes from the
/// person at the keyboard even when stdin is redirected. Blocks the thread
/// until the line is entered.
public func ReadPassword(prompt: string) throws -> string {
    var buf = [CChar](repeating: 0, count: 1024)
    var n: int32 = 0
    prompt.withCString { p in
        buf.withUnsafeMutableBufferPointer { b in
            n = readPassword(p, b.baseAddress!, 1024)
        }
    }
    if n == sys.Code.notATerminal {
        throw TermError.notATerminal
    }
    if n == sys.Code.invalid {
        throw TermError.tooLong
    }
    if n < 0 {
        throw TermError.system(code: sys.LastError())
    }
    let text = string(cString: buf)
    var i = 0
    while i < buf.count {
        buf[i] = 0
        i += 1
    }
    return text
}

/// Puts the terminal in raw mode -- no echo, no line buffering, no signals
/// from keys -- for as long as `body` runs, and puts it back after, however
/// `body` ends.
public func Raw(_ s: Stream, _ body: () throws -> Void) throws {
    let rc = makeRaw(s.fd)
    if rc == sys.Code.notATerminal {
        throw TermError.notATerminal
    }
    if rc != 0 {
        throw TermError.system(code: sys.LastError())
    }
    defer {
        _ = restoreMode(s.fd)
    }
    try body()
}

/// Whether text written to the stream should carry color, by the rules in
/// widest use: NO_COLOR set turns it off; CLICOLOR_FORCE set to anything
/// but "0" turns it on; otherwise it is on for a terminal whose TERM is not
/// "dumb".
public func ColorEnabled(_ s: Stream) -> bool {
    if env.Get("NO_COLOR") != nil {
        return false
    }
    if let force = env.Get("CLICOLOR_FORCE") {
        if force != "0" {
            return true
        }
    }
    if !IsTerminal(s) {
        return false
    }
    if env.Get("TERM") == "dumb" {
        return false
    }
    // Windows consoles interpret escapes only once asked to.
    _ = enableAnsi(s.fd)
    return true
}
