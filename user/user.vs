package user

import "os/sys"
import "os/env"
import "os/host"

/// The current user's login name, or nil where the system has none for
/// this process.
public func Name() -> string? {
    return sys.Fill { buf, max in
        return sys.cos_user_name(buf, max)
    }.Text
}

/// The current user's home directory: $HOME (%USERPROFILE% on Windows), or
/// the account's home where that is not set.
public func Home() -> string? {
    let h = sys.Fill { buf, max in
        return sys.cos_user_home(buf, max)
    }.Text
    if h == "" {
        return nil
    }
    return h
}

/// Where a program keeps data it can rebuild: $XDG_CACHE_HOME or ~/.cache
/// on Linux and Android, ~/Library/Caches on macOS, %LOCALAPPDATA% on
/// Windows.
public func CacheDir() -> string? {
    switch host.OS.Kind {
    case .macOS:
        return underHome("Library/Caches")
    case .windows:
        return env.Get("LOCALAPPDATA")
    default:
        return xdg("XDG_CACHE_HOME", ".cache")
    }
}

/// Where a program keeps its settings: $XDG_CONFIG_HOME or ~/.config,
/// ~/Library/Application Support, %APPDATA%.
public func ConfigDir() -> string? {
    switch host.OS.Kind {
    case .macOS:
        return underHome("Library/Application Support")
    case .windows:
        return env.Get("APPDATA")
    default:
        return xdg("XDG_CONFIG_HOME", ".config")
    }
}

/// Where a program keeps data the user would miss: $XDG_DATA_HOME or
/// ~/.local/share, ~/Library/Application Support, %APPDATA%.
public func DataDir() -> string? {
    switch host.OS.Kind {
    case .macOS:
        return underHome("Library/Application Support")
    case .windows:
        return env.Get("APPDATA")
    default:
        return xdg("XDG_DATA_HOME", ".local/share")
    }
}

/// Where a program keeps state that outlives a run but is not worth
/// backing up (history, logs): $XDG_STATE_HOME or ~/.local/state,
/// ~/Library/Application Support, %LOCALAPPDATA%.
public func StateDir() -> string? {
    switch host.OS.Kind {
    case .macOS:
        return underHome("Library/Application Support")
    case .windows:
        return env.Get("LOCALAPPDATA")
    default:
        return xdg("XDG_STATE_HOME", ".local/state")
    }
}

/// Where a program keeps sockets and other files that last as long as the
/// login: $XDG_RUNTIME_DIR, and nil on systems that have no such place.
public func RuntimeDir() -> string? {
    switch host.OS.Kind {
    case .linux, .android:
        return nonEmpty(env.Get("XDG_RUNTIME_DIR"))
    default:
        return nil
    }
}

func underHome(_ rest: string) -> string? {
    guard let h = Home() else {
        return nil
    }
    return join(h, rest)
}

// xdg is the XDG variable where it names an absolute path -- the spec
// says a relative one is to be ignored -- and home/fallback otherwise.
func xdg(_ variable: string, _ fallback: string) -> string? {
    if let v = nonEmpty(env.Get(variable)) {
        if sys.Bytes(v)[0] == 47 {
            return v
        }
    }
    return underHome(fallback)
}

func nonEmpty(_ s: string?) -> string? {
    guard let v = s else {
        return nil
    }
    return v.isEmpty ? nil : v
}

func join(_ a: string, _ b: string) -> string {
    let bytes = sys.Bytes(a)
    if !bytes.isEmpty && bytes[bytes.count - 1] == 47 {
        return a + b
    }
    return a + "/" + b
}
