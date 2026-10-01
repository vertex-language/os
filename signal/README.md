# os/signal

Ctrl-C, termination, and graceful shutdown.

```vertex
import "os/signal"
```

## Types

- **`Kind`** (enum): A signal, by what it means rather than its number.
- **`SignalError`** (enum): SignalError is how listening for a signal fails.
- **`Listener`** (class): Listener receives the signals it was made for, in the order they came, until it is closed.

## Functions

- `func Listen(_ kinds: Kind...) throws -> Listener`: A listener for the given signals.
- `func Wait(_ kinds: Kind...) async throws -> Kind`: Waits for one of the given signals, and returns which came. Ctrl-C does not end the process while this waits: this is how a program asks for it instead.

Part of the [`os`](https://github.com/vertex-language/os) repository.
