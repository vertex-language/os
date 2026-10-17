// check-pty: os/pty's checks. A child under a pseudo-terminal sees a
// terminal, its size, what is typed, and a resize; its exit is reported.
//
//     vsc run check-pty
package main

import (
    "os/process"
    "os/pty"
)

var failures = 0

func check(_ ok: bool, _ what: string) {
    if ok {
        print("ok   \(what)")
    } else {
        print("FAIL \(what)")
        failures += 1
    }
}

func text(_ b: [uint8]) -> string {
    return string(decoding: b, as: UTF8.self)
}

func main() async -> int32 {
    // A terminal on stdout, with the size it was given.
    do {
        let s = try pty.Spawn(process.Command("/bin/sh", ["-c", "test -t 1 && echo tty; stty size"]),
                              size: pty.Size(columns: 101, rows: 33))
        let out = text(try await s.ReadAll())
        check(out.contains("tty"), "child's stdout is a terminal")
        check(out.contains("33 101"), "child sees the size given (\(out.debugDescription))")
        let st = try await s.Wait()
        check(st == .exited(0), "exit status 0 (\(st))")
    } catch {
        check(false, "spawn sh: \(error)")
    }

    // What is written is read as typed; output gets CRLF from the line
    // discipline.
    do {
        let s = try pty.Spawn(process.Command("/bin/cat", []), size: pty.Size(columns: 80, rows: 24))
        try await s.Write("hello\r")
        var got: [uint8] = []
        var buf = [uint8](repeating: 0, count: 256)
        while !text(got).contains("hello\r\nhello\r\n") {
            let n = try await s.Read(into: &buf)
            if n == 0 { break }
            got.append(contentsOf: buf[0..<n])
        }
        check(text(got).contains("hello\r\nhello\r\n"), "echo and cat's copy (\(text(got).debugDescription))")
        check(s.ForegroundName() == "cat", "foreground is cat (\(s.ForegroundName() ?? "nil"))")
        check(s.ForegroundDir() != nil, "foreground dir (\(s.ForegroundDir() ?? "nil"))")
        try s.Resize(pty.Size(columns: 40, rows: 10))
        check(s.Size.Columns == 40, "resize")
        try await s.Write([4])  // ^D: end of input
        let st = try await s.Wait()
        check(st == .exited(0), "cat ends on ^D (\(st))")
    } catch {
        check(false, "cat: \(error)")
    }

    // Ctrl-C reaches the foreground program as SIGINT.
    do {
        let s = try pty.Spawn(process.Command("/bin/sleep", ["30"]))
        try await s.Write([3])
        let st = try await s.Wait()
        check(st == .signaled(2), "^C interrupts (\(st))")
    } catch {
        check(false, "sleep: \(error)")
    }

    // A program that isn't there.
    do {
        _ = try pty.Spawn(process.Command("/no/such/program", []))
        check(false, "missing program throws")
    } catch {
        check(true, "missing program throws (\(error))")
    }

    // The working directory and environment the command asks for.
    do {
        var cmd = process.Command("/bin/sh", ["-c", "pwd; echo $VT_CHECK"])
        cmd.Dir = "/tmp"
        cmd.Env["VT_CHECK"] = "yes"
        let s = try pty.Spawn(cmd)
        let out = text(try await s.ReadAll())
        check(out.contains("/tmp") || out.contains("/private/tmp"), "Dir (\(out.debugDescription))")
        check(out.contains("yes"), "Env")
        _ = try await s.Wait()
    } catch {
        check(false, "dir/env: \(error)")
    }

    if failures > 0 {
        print("\(failures) failed")
        return 1
    }
    print("all ok")
    return 0
}
