# os/term

Whether a stream is a terminal, its size, raw mode, passwords, color.

```vertex
import "os/term"
```

## Types

- **`Color`** (enum): A terminal color: the eight standard ones and their bright forms.
- **`Style`** (struct): How text is drawn: a foreground and background color and attributes, built up a call at a time and applied with `Render`.
- **`Stream`** (enum): One of the process's standard streams.
- **`Size`** (struct): A terminal's size in character cells.
- **`TermError`** (enum): TermError is how a terminal operation fails.

## Functions

- `func IsTerminal(_ s: Stream) -> bool`: Whether the stream is connected to a terminal rather than a file or pipe.
- `func GetSize(_ s: Stream) -> Size?`: The terminal's size, or nil where the stream is not a terminal.
- `func ReadPassword(prompt: string) throws -> string`: Prints `prompt` and reads a line from the terminal with echo off.
- `func Raw(_ s: Stream, _ body: () throws -> Void) throws`: Puts the terminal in raw mode -- no echo, no line buffering, no signals from keys -- for as long as `body` runs, and puts it back after, however `body` ends.
- `func ColorEnabled(_ s: Stream) -> bool`: Whether text written to the stream should carry color, by the rules in widest use: NO_COLOR set turns it off; CLICOLOR_FORCE set to anything but "0" turns it on; otherwise it is on for a terminal whose TERM is not "dumb".

Part of the [`os`](https://github.com/vertex-language/os) repository.
