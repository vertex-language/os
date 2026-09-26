package host

import "os/sys"

/// The kind of operating system.
public enum OSKind: Equatable {
    case macOS
    case windows
    case linux
    case android
}

/// The operating system and its version.
public struct OSInfo {
    public let Kind: OSKind
    /// The product version: "15.3.1" on macOS, "10.0.26100" on Windows,
    /// "14" on Android. "" where the system does not say.
    public let Version: string
}

/// The processor architecture the program was built for.
public enum Architecture: Equatable {
    case aarch64
    case x86_64
}

/// How many processors the machine has.
public struct CPUInfo {
    /// Logical processors the process may run on.
    public let Logical: int
    /// Performance cores. On a machine whose cores are all alike, all of them.
    public let Performance: int
    /// Efficiency cores (Apple Silicon's E-cores); 0 where there are none.
    public let Efficiency: int
}

/// Physical memory, in bytes.
public struct MemoryInfo {
    public let Total: int64
    /// What can be handed out without swapping: free memory, and the cache
    /// the kernel can drop.
    public let Available: int64
}

/// The machine's host name.
public func Name() -> string {
    return sys.Fill { buf, max in
        return hostName(buf, max)
    }.Text ?? ""
}

/// The operating system and its version.
public var OS: OSInfo {
    let version = sys.Fill { buf, max in
        return osVersion(buf, max)
    }.Text ?? ""
    return OSInfo(Kind: kind(), Version: version)
}

/// The processor architecture the program was built for.
public var Arch: Architecture {
    return archKind() == ArchCode.aarch64 ? .aarch64 : .x86_64
}

/// How many processors the machine has.
public func CPUs() -> CPUInfo {
    var out = [int32](repeating: 0, count: 3)
    out.withUnsafeMutableBufferPointer { p in
        cpuCounts(p.baseAddress!)
    }
    let logical = out[0] > 0 ? int(out[0]) : 1
    return CPUInfo(Logical: logical, Performance: int(out[1]), Efficiency: int(out[2]))
}

/// Physical memory, total and available.
public func Memory() -> MemoryInfo {
    var out = [int64](repeating: 0, count: 2)
    out.withUnsafeMutableBufferPointer { p in
        memoryBytes(p.baseAddress!)
    }
    return MemoryInfo(Total: out[0], Available: out[1])
}

/// The size of a page of memory, in bytes.
public var PageSize: int {
    return int(pageSize())
}

/// Nanoseconds since the machine booted, time asleep included.
public func UptimeNanos() -> int64 {
    return uptimeNanos()
}

func kind() -> OSKind {
    switch osKind() {
    case OSCode.windows:
        return .windows
    case OSCode.linux:
        return .linux
    case OSCode.android:
        return .android
    default:
        return .macOS
    }
}
