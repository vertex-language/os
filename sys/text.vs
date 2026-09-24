package sys

// Filled is what a buffer-filling cos call gave back: the text, or the
// negative code it failed with.
public struct Filled {
    public let Text: string?
    public let Code: int32

    public init(Text: string?, Code: int32) {
        self.Text = Text
        self.Code = Code
    }
}

// Fill calls a cos function that fills a buffer and returns the length
// the whole value needs, growing the buffer until it fits.
public func Fill(_ call: (UnsafeMutablePointer<CChar>?, int32) -> int32) -> Filled {
    var size = 256
    while true {
        var buf = [CChar](repeating: 0, count: size)
        var n: int32 = 0
        buf.withUnsafeMutableBufferPointer { p in
            n = call(p.baseAddress, int32(size))
        }
        if n < 0 {
            return Filled(Text: nil, Code: n)
        }
        if int(n) < size {
            return Filled(Text: string(cString: buf), Code: 0)
        }
        size = int(n) + 1
    }
}

// Blob lays strings end to end, each NUL-terminated, as cos_spawn takes
// its arguments and environment.
public func Blob(_ items: [string]) -> [CChar] {
    var out: [CChar] = []
    for s in items {
        for b in s.utf8 {
            out.append(CChar(truncatingIfNeeded: b))
        }
        out.append(0)
    }
    if out.isEmpty {
        out.append(0)
    }
    return out
}

// Bytes is a string's UTF-8.
public func Bytes(_ s: string) -> [uint8] {
    return [uint8](s.utf8)
}

// Text is the bytes from start up to end as a string.
public func Text(_ bytes: [uint8], from start: int, to end: int) -> string {
    if start >= end {
        return ""
    }
    return string(decoding: bytes[start..<end], as: UTF8.self)
}

// Pollable is whether a descriptor from cos can be waited on through the
// runtime, rather than read and waited on by blocking the thread.
public func Pollable() -> bool {
    return cos_pollable() == 1
}
