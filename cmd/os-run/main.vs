// os-run: runs a command and reports how it ended.
//
//     os-run git status --short
package main

import "os/process"

func main() async -> int32 {
    let args = process.Args
    if args.count < 2 {
        print("usage: os-run <program> [args...]")
        return 2
    }
    var rest: [string] = []
    var i = 2
    while i < args.count {
        rest.append(args[i])
        i += 1
    }
    do {
        let status = try await process.Command(args[1], rest).Status()
        print("\(args[1]) \(status)")
        return status.Code ?? 1
    } catch {
        print("os-run: \(error)")
        return 1
    }
}
