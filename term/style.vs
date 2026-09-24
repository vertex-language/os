package term

/// A terminal color: the eight standard ones and their bright forms.
public enum Color: Equatable {
    case black
    case red
    case green
    case yellow
    case blue
    case magenta
    case cyan
    case white
    case brightBlack
    case brightRed
    case brightGreen
    case brightYellow
    case brightBlue
    case brightMagenta
    case brightCyan
    case brightWhite

    var code: int {
        switch self {
        case .black: return 30
        case .red: return 31
        case .green: return 32
        case .yellow: return 33
        case .blue: return 34
        case .magenta: return 35
        case .cyan: return 36
        case .white: return 37
        case .brightBlack: return 90
        case .brightRed: return 91
        case .brightGreen: return 92
        case .brightYellow: return 93
        case .brightBlue: return 94
        case .brightMagenta: return 95
        case .brightCyan: return 96
        case .brightWhite: return 97
        }
    }
}

/// How text is drawn: a foreground and background color and attributes,
/// built up a call at a time and applied with `Render`.
///
///     let warn = term.Style(for: .stderr).Bold().Foreground(.yellow)
///     print(warn.Render("warning:") + " disk almost full")
///
/// `Render` adds escapes only where `ColorEnabled` says the stream takes
/// them, so styled output is plain text in a pipe or a file.
public struct Style {
    var stream: Stream
    var fg: Color?
    var bg: Color?
    var bold: bool
    var dim: bool
    var italic: bool
    var underline: bool

    /// A plain style for text written to the stream, stdout unless said.
    public init(for stream: Stream = .stdout) {
        self.stream = stream
        fg = nil
        bg = nil
        bold = false
        dim = false
        italic = false
        underline = false
    }

    public func Foreground(_ c: Color) -> Style {
        var s = self
        s.fg = c
        return s
    }

    public func Background(_ c: Color) -> Style {
        var s = self
        s.bg = c
        return s
    }

    public func Bold() -> Style {
        var s = self
        s.bold = true
        return s
    }

    public func Dim() -> Style {
        var s = self
        s.dim = true
        return s
    }

    public func Italic() -> Style {
        var s = self
        s.italic = true
        return s
    }

    public func Underline() -> Style {
        var s = self
        s.underline = true
        return s
    }

    /// The text wrapped in this style's escapes, or the text as it is where
    /// the stream does not take color.
    public func Render(_ text: string) -> string {
        if !ColorEnabled(stream) {
            return text
        }
        var codes: [int] = []
        if bold {
            codes.append(1)
        }
        if dim {
            codes.append(2)
        }
        if italic {
            codes.append(3)
        }
        if underline {
            codes.append(4)
        }
        if let c = fg {
            codes.append(c.code)
        }
        if let c = bg {
            codes.append(c.code + 10)
        }
        if codes.isEmpty {
            return text
        }
        var seq = "\u{1b}["
        var i = 0
        while i < codes.count {
            if i > 0 {
                seq += ";"
            }
            seq += "\(codes[i])"
            i += 1
        }
        return seq + "m" + text + "\u{1b}[0m"
    }
}
