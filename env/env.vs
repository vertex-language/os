package env

import "os/sys"

/// Missing is the error `Require` throws for a variable that is not set.
public struct Missing: Error, CustomStringConvertible {
    /// The variable that was asked for.
    public let Name: string

    public init(_ name: string) {
        Name = name
    }

    public var description: string {
        return "environment variable \(Name) is not set"
    }
}

/// The value of the variable `name`, or nil where it is not set. An empty
/// value is "", which is not the same thing.
public func Get(_ name: string) -> string? {
    if name.isEmpty {
        return nil
    }
    var out: sys.Filled = sys.Filled(Text: nil, Code: sys.Code.notFound)
    name.withCString { n in
        out = sys.Fill { buf, max in
            return envGet(n, buf, max)
        }
    }
    return out.Text
}

/// The raw bytes of the variable `name`, for a value that may not be
/// UTF-8 (a path on a system that allows any bytes in one).
public func GetBytes(_ name: string) -> [uint8]? {
    guard let v = Get(name) else {
        return nil
    }
    return sys.Bytes(v)
}

/// The value of the variable `name`, throwing `Missing` where it is not set.
public func Require(_ name: string) throws -> string {
    guard let v = Get(name) else {
        throw Missing(name)
    }
    return v
}

/// Every variable, as (name, value) pairs sorted by name: a snapshot, taken
/// when it is called.
public func All() -> [(string, string)] {
    var out: [(string, string)] = []
    let count = envCount()
    var i: int32 = 0
    while i < count {
        let index = i
        let entry = sys.Fill { buf, max in
            return envEntry(index, buf, max)
        }
        if let text = entry.Text {
            if let pair = split(text) {
                out.append(pair)
            }
        }
        i += 1
    }
    return sortedByName(out)
}

/// Sets the variable `name` to `value` for this process and the children
/// it starts afterwards.
///
/// Not thread-safe: the C library's environment has no lock, and another
/// thread reading it while this writes can crash. Call it before starting
/// tasks or threads. To give a child a different environment, set
/// `process.Command.Env` instead.
public func Set(_ name: string, _ value: string) {
    name.withCString { n in
        value.withCString { v in
            _ = envSet(n, v)
        }
    }
}

/// Removes the variable `name`. Not thread-safe; see `Set`.
public func Remove(_ name: string) {
    name.withCString { n in
        _ = envRemove(n)
    }
}

// split cuts "NAME=value" at its first '='.
func split(_ entry: string) -> (string, string)? {
    let bytes = sys.Bytes(entry)
    var i = 0
    while i < bytes.count {
        if bytes[i] == 61 && i > 0 {
            return (sys.Text(bytes, from: 0, to: i), sys.Text(bytes, from: i + 1, to: bytes.count))
        }
        i += 1
    }
    return nil
}

func sortedByName(_ pairs: [(string, string)]) -> [(string, string)] {
    return pairs.sorted { $0.0 < $1.0 }
}
