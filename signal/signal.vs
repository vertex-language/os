package signal

import "os/sys"

/// A signal, by what it means rather than its number.
public enum Kind: Equatable {
    /// Ctrl-C: SIGINT, or CTRL_C_EVENT and CTRL_BREAK_EVENT on Windows.
    case interrupt
    /// A request to stop: SIGTERM, or the console closing or the system
    /// shutting down on Windows.
    case terminate
    /// The terminal went away: SIGHUP, or CTRL_LOGOFF_EVENT on Windows.
    case hangup
    /// The terminal changed size: SIGWINCH.
    case windowResize
    /// Any other POSIX signal, by number. Not on Windows.
    case posix(int32)

    /// cos's number for the kind, and 0 for `.posix`.
    public var Code: int32 {
        switch self {
        case .interrupt:
            return 1
        case .terminate:
            return 2
        case .hangup:
            return 3
        case .windowResize:
            return 4
        case .posix:
            return 0
        }
    }

    static func from(_ code: int32) -> Kind {
        switch code {
        case 1:
            return .interrupt
        case 2:
            return .terminate
        case 3:
            return .hangup
        default:
            return .windowResize
        }
    }
}

/// SignalError is how listening for a signal fails.
public enum SignalError: Error, CustomStringConvertible {
    /// `.posix(n)` can be sent (`Child.Signal`) but not listened for yet.
    case unsupported(Kind)
    case system(code: int32)

    public var description: string {
        switch self {
        case .unsupported:
            return "this signal cannot be listened for"
        case .system(let code):
            return "signal error \(code)"
        }
    }
}

/// Listener receives the signals it was made for, in the order they came,
/// until it is closed. While any listener wants a signal, the signal's
/// default action -- ending the process -- does not happen; it comes back
/// when the last listener for it closes.
///
///     let stop = try signal.Listen(.interrupt, .terminate)
///     while let s = await stop.Next() {
///         server.Shutdown()
///         break
///     }
public final class Listener {
    var fd: int32

    init(_ fd: int32) {
        self.fd = fd
    }

    deinit {
        Close()
    }

    /// The next signal, waiting for one; nil once the listener is closed.
    public func Next() async -> Kind? {
        var byte = [uint8](repeating: 0, count: 1)
        while fd >= 0 {
            var n: int64 = 0
            byte.withUnsafeMutableBytes { raw in
                n = sys.cos_read(fd, raw.baseAddress!, 1)
            }
            if n == 1 {
                return Kind.from(int32(byte[0]))
            }
            if n == int64(sys.Code.wouldBlock) {
                _ = await sys.vertex_task_wait_fd(fd, 1, -1)
                continue
            }
            if n == int64(sys.Code.interrupted) {
                continue
            }
            return nil
        }
        return nil
    }

    /// Stops listening. The default action of any signal nobody listens
    /// for any more comes back.
    public func Close() {
        if fd >= 0 {
            sys.cos_signal_unlisten(fd)
            fd = -1
        }
    }
}

/// A listener for the given signals.
public func Listen(_ kinds: Kind...) throws -> Listener {
    var mask: int32 = 0
    for k in kinds {
        let c = k.Code
        if c == 0 {
            throw SignalError.unsupported(k)
        }
        mask = mask | (1 << c)
    }
    let fd = sys.cos_signal_listen(mask)
    if fd < 0 {
        throw SignalError.system(code: sys.cos_last_error())
    }
    return Listener(fd)
}

/// Waits for one of the given signals, and returns which came. Ctrl-C does
/// not end the process while this waits: this is how a program asks for it
/// instead.
///
///     try await signal.Wait(.interrupt)
public func Wait(_ kinds: Kind...) async throws -> Kind {
    var mask: int32 = 0
    for k in kinds {
        let c = k.Code
        if c == 0 {
            throw SignalError.unsupported(k)
        }
        mask = mask | (1 << c)
    }
    let fd = sys.cos_signal_listen(mask)
    if fd < 0 {
        throw SignalError.system(code: sys.cos_last_error())
    }
    let l = Listener(fd)
    defer {
        l.Close()
    }
    guard let got = await l.Next() else {
        throw SignalError.system(code: 0)
    }
    return got
}
