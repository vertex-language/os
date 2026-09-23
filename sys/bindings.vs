package sys

// cos, as Vertex calls it. See cos/include/cos.h for what each does.

public enum Code {
    public static let ok: int32 = 0
    public static let generic: int32 = -1
    public static let notFound: int32 = -2
    public static let permission: int32 = -3
    public static let invalid: int32 = -4
    public static let wouldBlock: int32 = -5
    public static let unsupported: int32 = -6
    public static let noMemory: int32 = -7
    public static let interrupted: int32 = -8
    public static let brokenPipe: int32 = -9
    public static let notATerminal: int32 = -10
}

@_silgen_name("cos_last_error")
public func cos_last_error() -> int32
@_silgen_name("cos_pollable")
public func cos_pollable() -> int32

@_silgen_name("cos_env_get")
public func cos_env_get(_ name: UnsafePointer<CChar>, _ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_env_set")
public func cos_env_set(_ name: UnsafePointer<CChar>, _ value: UnsafePointer<CChar>) -> int32
@_silgen_name("cos_env_remove")
public func cos_env_remove(_ name: UnsafePointer<CChar>) -> int32
@_silgen_name("cos_env_count")
public func cos_env_count() -> int32
@_silgen_name("cos_env_entry")
public func cos_env_entry(_ index: int32, _ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32

@_silgen_name("cos_pid")
public func cos_pid() -> int32
@_silgen_name("cos_ppid")
public func cos_ppid() -> int32
@_silgen_name("cos_executable_path")
public func cos_executable_path(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_current_dir")
public func cos_current_dir(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_set_current_dir")
public func cos_set_current_dir(_ path: UnsafePointer<CChar>) -> int32
@_silgen_name("cos_exit")
public func cos_exit(_ code: int32) -> Never
@_silgen_name("cos_abort")
public func cos_abort() -> Never
@_silgen_name("cos_is_executable")
public func cos_is_executable(_ path: UnsafePointer<CChar>) -> int32

@_silgen_name("cos_spawn")
public func cos_spawn(_ path: UnsafePointer<CChar>,
                      _ args: UnsafePointer<CChar>, _ argCount: int32,
                      _ env: UnsafePointer<CChar>?, _ envCount: int32,
                      _ dir: UnsafePointer<CChar>?,
                      _ modes: UnsafePointer<int32>, _ files: UnsafePointer<CChar>,
                      _ out: UnsafeMutablePointer<int64>) -> int32
@_silgen_name("cos_try_wait")
public func cos_try_wait(_ pid: int64, _ exitFd: int32, _ block: int32,
                         _ kind: UnsafeMutablePointer<int32>, _ code: UnsafeMutablePointer<int32>) -> int32
@_silgen_name("cos_send_signal")
public func cos_send_signal(_ pid: int64, _ kind: int32, _ posix: int32) -> int32
@_silgen_name("cos_release_child")
public func cos_release_child(_ pid: int64, _ exitFd: int32)

@_silgen_name("cos_read")
public func cos_read(_ fd: int32, _ buf: UnsafeMutableRawPointer, _ count: int64) -> int64
@_silgen_name("cos_write")
public func cos_write(_ fd: int32, _ buf: UnsafeRawPointer, _ count: int64) -> int64
@_silgen_name("cos_close")
public func cos_close(_ fd: int32) -> int32

@_silgen_name("cos_signal_listen")
public func cos_signal_listen(_ mask: int32) -> int32
@_silgen_name("cos_signal_unlisten")
public func cos_signal_unlisten(_ fd: int32)

@_silgen_name("cos_os_kind")
public func cos_os_kind() -> int32
@_silgen_name("cos_arch")
public func cos_arch() -> int32
@_silgen_name("cos_os_version")
public func cos_os_version(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_hostname")
public func cos_hostname(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_cpus")
public func cos_cpus(_ out: UnsafeMutablePointer<int32>)
@_silgen_name("cos_memory")
public func cos_memory(_ out: UnsafeMutablePointer<int64>)
@_silgen_name("cos_page_size")
public func cos_page_size() -> int64
@_silgen_name("cos_uptime")
public func cos_uptime() -> int64

@_silgen_name("cos_user_name")
public func cos_user_name(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32
@_silgen_name("cos_user_home")
public func cos_user_home(_ buf: UnsafeMutablePointer<CChar>?, _ max: int32) -> int32

@_silgen_name("cos_is_terminal")
public func cos_is_terminal(_ fd: int32) -> int32
@_silgen_name("cos_term_size")
public func cos_term_size(_ fd: int32, _ out: UnsafeMutablePointer<int32>) -> int32
@_silgen_name("cos_read_password")
public func cos_read_password(_ prompt: UnsafePointer<CChar>, _ buf: UnsafeMutablePointer<CChar>, _ max: int32) -> int32
@_silgen_name("cos_term_raw")
public func cos_term_raw(_ fd: int32) -> int32
@_silgen_name("cos_term_restore")
public func cos_term_restore(_ fd: int32) -> int32
@_silgen_name("cos_term_enable_ansi")
public func cos_term_enable_ansi(_ fd: int32) -> int32

// The runtime's wait: parks the task until fd can be read (1) or written
// (2). 1 ready, 0 timed out. Outside a task the thread waits.
@_silgen_name("vertex_task_wait_fd")
public func vertex_task_wait_fd(_ fd: int32, _ events: int32, _ timeoutNanos: int64) async -> int32
