// os test suite.
package main

import (
    "io"
    "os/env"
    "os/host"
    "os/process"
    "os/signal"
    "os/term"
    "os/user"
)

var failures: int32 = 0

func check(_ ok: bool, _ what: string) {
    if ok {
        print("ok    \(what)")
    } else {
        print("FAIL  \(what)")
        failures += 1
    }
}

func testEnv() {
    check(env.Get("HOME") != nil, "env Get HOME")
    check(env.Get("OS_CHECK_SURELY_UNSET") == nil, "env Get unset is nil")
    env.Set("OS_CHECK_VAR", "hello world")
    check(env.Get("OS_CHECK_VAR") == "hello world", "env Set then Get")
    env.Set("OS_CHECK_EMPTY", "")
    check(env.Get("OS_CHECK_EMPTY") == "", "env empty value is not nil")
    var found = false
    for (k, v) in env.All() {
        if k == "OS_CHECK_VAR" && v == "hello world" {
            found = true
        }
    }
    check(found, "env All includes a set variable")
    env.Remove("OS_CHECK_VAR")
    check(env.Get("OS_CHECK_VAR") == nil, "env Remove")
    do {
        _ = try env.Require("OS_CHECK_VAR")
        check(false, "env Require throws for unset")
    } catch let e as env.Missing {
        check(e.Name == "OS_CHECK_VAR", "env Require throws Missing")
    } catch {
        check(false, "env Require throws Missing")
    }
    var long = "x"
    var i = 0
    while i < 1000 {
        long += "y"
        i += 1
    }
    env.Set("OS_CHECK_LONG", long)
    check(env.Get("OS_CHECK_LONG") == long, "env Get grows its buffer")
}

func testHost() {
    check(!host.Name().isEmpty, "host Name")
    let os = host.OS
    check(os.Kind == .macOS, "host OS Kind")
    check(!os.Version.isEmpty, "host OS Version (\(os.Version))")
    check(host.Arch == .aarch64, "host Arch")
    let cpus = host.CPUs()
    check(cpus.Logical > 0, "host CPUs Logical (\(cpus.Logical))")
    check(cpus.Performance + cpus.Efficiency == cpus.Logical,
          "host CPUs Performance + Efficiency (\(cpus.Performance) + \(cpus.Efficiency))")
    let mem = host.Memory()
    check(mem.Total > 0 && mem.Available > 0 && mem.Available <= mem.Total, "host Memory")
    check(host.PageSize == 16384 || host.PageSize == 4096, "host PageSize")
    check(host.UptimeNanos() > 0, "host Uptime")
}

func testUser() {
    check(user.Name() != nil, "user Name")
    check(user.Home() == env.Get("HOME"), "user Home is $HOME")
    let home = user.Home() ?? ""
    check(user.CacheDir() == home + "/Library/Caches", "user CacheDir")
    check(user.ConfigDir() == home + "/Library/Application Support", "user ConfigDir")
    check(user.RuntimeDir() == nil, "user RuntimeDir is nil on macOS")
}

func testTerm() {
    env.Set("NO_COLOR", "1")
    check(term.Style().Bold().Foreground(.red).Render("x") == "x", "term NO_COLOR renders plain")
    env.Remove("NO_COLOR")
    env.Set("CLICOLOR_FORCE", "1")
    check(term.Style().Bold().Foreground(.red).Render("x") == "\u{1b}[1;31mx\u{1b}[0m", "term CLICOLOR_FORCE renders escapes")
    env.Remove("CLICOLOR_FORCE")
    // stdout is a pipe under a test runner and a terminal by hand; a
    // terminal can report no size (a pty nobody sized), a pipe never has one.
    if !term.IsTerminal(.stdout) {
        check(term.GetSize(.stdout) == nil, "term Size is nil for a pipe")
    }
}

func testCurrentProcess() {
    check(process.ID > 0, "process ID")
    check(process.ParentID > 0, "process ParentID")
    check(process.Args.count >= 1, "process Args")
    check(process.ExecutablePath() != nil, "process ExecutablePath")
    do {
        let before = try process.CurrentDir()
        try process.SetCurrentDir("/tmp")
        let now = try process.CurrentDir()
        check(now == "/tmp" || now == "/private/tmp", "process SetCurrentDir")
        try process.SetCurrentDir(before)
    } catch {
        check(false, "process CurrentDir: \(error)")
    }
    check(process.Find("sh") != nil, "process Find sh")
    check(process.Find("surely-no-such-program-xyz") == nil, "process Find missing")
}

func testChildren() async {
    do {
        let out = try await process.Run("echo", ["hello", "world"])
        check(out.StdoutText == "hello world\n", "process Run captures stdout")
        check(out.Status == .exited(0), "process Run status")
    } catch {
        check(false, "process Run: \(error)")
    }

    do {
        _ = try await process.Run("sh", ["-c", "echo oops >&2; exit 3"])
        check(false, "process Run throws on failure")
    } catch let e as process.ProcessError {
        if case .failed(let out) = e {
            check(out.Status == .exited(3), "process Run failed status")
            check(out.StderrText == "oops\n", "process Run failed stderr")
        } else {
            check(false, "process Run throws failed")
        }
    } catch {
        check(false, "process Run throws ProcessError")
    }

    do {
        _ = try await process.Run("surely-no-such-program-xyz")
        check(false, "process Run missing program")
    } catch let e as process.ProcessError {
        if case .notFound = e {
            check(true, "process Run missing program is notFound")
        } else {
            check(false, "process Run missing program is notFound")
        }
    } catch {
        check(false, "process Run missing program is notFound")
    }

    do {
        // Arguments reach the child as they are: no shell splits them.
        let out = try await process.Run("printf", ["%s|", "a b", "$HOME", "'q'"])
        check(out.StdoutText == "a b|$HOME|'q'|", "process arguments are not shell-split")
    } catch {
        check(false, "process arguments: \(error)")
    }

    do {
        var cmd = process.Command("sh", ["-c", "echo $OS_CHILD_VAR; pwd"])
        cmd.Env["OS_CHILD_VAR"] = "from parent"
        cmd.Dir = "/tmp"
        let out = try await cmd.Output()
        check(out.StdoutText == "from parent\n/tmp\n" || out.StdoutText == "from parent\n/private/tmp\n",
              "process Command Env and Dir")
    } catch {
        check(false, "process Command Env and Dir: \(error)")
    }

    do {
        var cmd = process.Command("/usr/bin/env")
        cmd.ClearEnv = true
        cmd.Env["ONLY"] = "this"
        let out = try await cmd.Output()
        check(out.StdoutText == "ONLY=this\n", "process Command ClearEnv")
    } catch {
        check(false, "process Command ClearEnv: \(error)")
    }

    do {
        var cmd = process.Command("cat")
        cmd.Stdin = .pipe
        cmd.Stdout = .pipe
        let child = try cmd.Spawn()
        try await child.Stdin!.Write("one\ntwo\r\nthree")
        child.Stdin!.Close()
        var got: [string] = []
        for try await line in child.Stdout!.Lines() {
            got.append(line)
        }
        let status = try await child.Wait()
        check(got.count == 3 && got[0] == "one" && got[1] == "two" && got[2] == "three", "process pipes and Lines")
        check(status.Success, "process cat exits 0")
    } catch {
        check(false, "process pipes: \(error)")
    }

    do {
        // Larger than a pipe's buffer on both streams at once: Output must
        // drain stderr while it reads stdout, or the child stops.
        let script = "i=0; while [ $i -lt 20000 ]; do echo out-line-$i; echo err-line-$i >&2; i=$((i+1)); done"
        let out = try await process.Command("sh", ["-c", script]).Output()
        check(out.Stdout.count > 200000 && out.Stderr.count > 200000, "process Output drains both pipes")
    } catch {
        check(false, "process Output large: \(error)")
    }

    do {
        var cmd = process.Command("sleep", ["30"])
        cmd.Stdin = .null
        let child = try cmd.Spawn()
        check(try child.TryWait() == nil, "process TryWait while running")
        child.Terminate()
        let status = try await child.Wait()
        check(status == .signaled(15), "process Terminate (\(status))")
    } catch {
        check(false, "process Terminate: \(error)")
    }

    do {
        let child = try process.Command("sh", ["-c", "exit 7"]).Spawn()
        let status = try await child.Wait()
        check(status.Code == 7, "process Wait exit code")
        check(try child.TryWait() == .exited(7), "process TryWait after Wait")
    } catch {
        check(false, "process Wait: \(error)")
    }
}

func testIo() async {
    do {
        // A child's output through io: copied into memory as it arrives.
        var cmd = process.Command("printf", ["one\ntwo\n"])
        cmd.Stdout = .pipe
        let child = try cmd.Spawn()
        var pipe = child.Stdout!
        var mem = io.Cursor()
        let n = try await io.Copy(from: &pipe, to: &mem)
        _ = try await child.Wait()
        check(n == 8 && io.Text(mem.Bytes) == "one\ntwo\n", "io.Copy from a child's stdout")

        // Into a child's stdin through io, and read back as text.
        var cat = process.Command("cat")
        cat.Stdin = .pipe
        cat.Stdout = .pipe
        let c = try cat.Spawn()
        var input = c.Stdin!
        var src = io.Cursor(io.Bytes("through cat"))
        _ = try await io.Copy(from: &src, to: &input)
        input.Close()
        check(try await c.Stdout!.ReadText() == "through cat", "io.Copy into a child's stdin")
        _ = try await c.Wait()

        // print and process.Stdout interleave in order.
        print("ok    print before process.Stdout")
        var out = process.Stdout
        try await io.WriteText(&out, "ok    process.Stdout after print\n")
    } catch {
        check(false, "io: \(error)")
    }
}

func testSignalListen() async {
    do {
        let l = try signal.Listen(.hangup)
        // Without the listener, SIGHUP would end this process.
        _ = try await process.Run("kill", ["-HUP", "\(process.ID)"])
        let got = await l.Next()
        check(got == .hangup, "signal Listen receives hangup")
        l.Close()
    } catch {
        check(false, "signal Listen: \(error)")
    }
}

func testSignalWait() async {
    do {
        var cmd = process.Command("sh", ["-c", "sleep 0.2; kill -INT \(process.ID)"])
        cmd.Stdin = .null
        let child = try cmd.Spawn()
        let got = try await signal.Wait(.interrupt, .terminate)
        check(got == .interrupt, "signal Wait interrupt")
        _ = try await child.Wait()
    } catch {
        check(false, "signal Wait: \(error)")
    }
}

func main() async -> int32 {
    testEnv()
    testHost()
    testUser()
    testTerm()
    testCurrentProcess()
    await testChildren()
    await testIo()
    await testSignalListen()
    await testSignalWait()
    if failures > 0 {
        print("\(failures) failed")
        return 1
    }
    print("all passed")
    return 0
}
