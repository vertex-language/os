# os/host

The machine: OS, architecture, CPUs, memory.

```vertex
import "os/host"
```

## Types

- **`OSKind`** (enum): The kind of operating system.
- **`OSInfo`** (struct): The operating system and its version.
- **`Architecture`** (enum): The processor architecture the program was built for.
- **`CPUInfo`** (struct): How many processors the machine has.
- **`MemoryInfo`** (struct): Physical memory, in bytes.

## Functions

- `func Name() -> string`: The machine's host name.
- `func CPUs() -> CPUInfo`: How many processors the machine has.
- `func Memory() -> MemoryInfo`: Physical memory, total and available.
- `func UptimeNanos() -> int64`: Nanoseconds since the machine booted, time asleep included.

Part of the [`os`](https://github.com/vertex-language/os) repository.
