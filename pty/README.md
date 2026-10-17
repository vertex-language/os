# os/pty

Running a command under a pseudo-terminal: what a terminal emulator,
`remote/ssh` or a test of an interactive program needs.

```vertex
import "os/pty"
```

## Types

- **`Session`** (class): A child running under a pseudo-terminal, and the parent's end of it: `Read`, `ReadAll`, `Write`, `Resize`, `Wait`, `TryWait`, `Signal`, `Hangup`, `Kill`, `Close`, `ForegroundName`, `ForegroundDir`, `Size`, `ID`.
- **`Size`** (struct): A terminal's size in cells, and in pixels where known.
- **`PtyError`** (enum): How a pseudo-terminal operation fails.

## Functions

- `func Spawn(_ command: process.Command, size: Size = Size(columns: 80, rows: 24)) throws -> Session`: Starts the command with a new pseudo-terminal as its stdin, stdout and stderr, as the leader of a new session with the terminal as its controlling terminal. Returns at once.

Part of the [`os`](https://github.com/vertex-language/os) repository.
