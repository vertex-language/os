# os/env

Environment variables.

## Example

```vertex
import "os/env"

env.Set("GREETING", "hello")
print(env.Get("GREETING") ?? "unset")
```

## Types

- **`Missing`** (struct): Missing is the error `Require` throws for a variable that is not set.

## Functions

- `func Get(_ name: string) -> string?`: The value of the variable `name`, or nil where it is not set. An empty value is "", which is not the same thing.
- `func GetBytes(_ name: string) -> [uint8]?`: The raw bytes of the variable `name`, for a value that may not be UTF-8 (a path on a system that allows any bytes in one).
- `func Require(_ name: string) throws -> string`: The value of the variable `name`, throwing `Missing` where it is not set.
- `func All() -> [(string, string)]`: Every variable, as (name, value) pairs sorted by name: a snapshot, taken when it is called.
- `func Set(_ name: string, _ value: string)`: Sets the variable `name` to `value` for this process and the children it starts afterwards.
- `func Remove(_ name: string)`: Removes the variable `name`. Not thread-safe; see `Set`.

Part of the [`os`](https://github.com/vertex-language/os) repository.
