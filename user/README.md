# os/user

The current user and their standard directories.

```vertex
import "os/user"
```

## Functions

- `func Name() -> string?`: The current user's login name, or nil where the system has none for this process.
- `func Home() -> string?`: The current user's home directory: $HOME (%USERPROFILE% on Windows), or the account's home where that is not set.
- `func CacheDir() -> string?`: Where a program keeps data it can rebuild: $XDG_CACHE_HOME or ~/.cache on Linux and Android, ~/Library/Caches on macOS, %LOCALAPPDATA% on Windows.
- `func ConfigDir() -> string?`: Where a program keeps its settings: $XDG_CONFIG_HOME or ~/.config, ~/Library/Application Support, %APPDATA%.
- `func DataDir() -> string?`: Where a program keeps data the user would miss: $XDG_DATA_HOME or ~/.local/share, ~/Library/Application Support, %APPDATA%.
- `func StateDir() -> string?`: Where a program keeps state that outlives a run but is not worth backing up (history, logs): $XDG_STATE_HOME or ~/.local/state, ~/Library/Application Support, %LOCALAPPDATA%.
- `func RuntimeDir() -> string?`: Where a program keeps sockets and other files that last as long as the login: $XDG_RUNTIME_DIR, and nil on systems that have no such place.

Part of the [`os`](https://github.com/vertex-language/os) repository.
