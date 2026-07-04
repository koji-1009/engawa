import Foundation
import EngawaKit

// The `process` namespace (spec/commands/process.md). Pull streams (§4.1): the host buffers
// per stream, signals process.readable when a drained buffer goes non-empty, and drains via
// process.read; process.exit fires only once both streams are drained. 8 MiB per-stream cap
// applies OS backpressure by pausing pipe reads. Sidecars are allowlisted per §7.2.
final class ProcessAdapter: Adapter, @unchecked Sendable {
    let namespace = "process"

    private let manifest: Manifest?
    private let emitter: EventEmitter
    private let lock = NSLock()
    private var procs: [Int: Proc] = [:]
    private let bufferCap = 8 * 1024 * 1024

    final class Stream {
        let handle: FileHandle
        var buffer = Data()
        var eof = false
        var paused = false
        init(_ handle: FileHandle) { self.handle = handle }
    }

    final class Proc {
        let process: Process
        let stdin: FileHandle
        let stdout: Stream
        let stderr: Stream
        var exited = false
        var exitCode = 0
        var exitEmitted = false
        init(process: Process, stdin: FileHandle, stdout: FileHandle, stderr: FileHandle) {
            self.process = process
            self.stdin = stdin
            self.stdout = Stream(stdout)
            self.stderr = Stream(stderr)
        }
    }

    init(manifest: Manifest?, emitter: EventEmitter) {
        self.manifest = manifest
        self.emitter = emitter
    }

    func handle(_ cmd: String, _ args: JSONValue) async throws -> JSONValue {
        switch cmd {
        case "spawn": return try spawn(args)
        case "stdinWrite": return try stdinWrite(args)
        case "read": return try read(args)
        case "kill": return try kill(args)
        default: throw AdapterError("ENOSYS", "unknown command: process.\(cmd)")
        }
    }

    // MARK: spawn

    private func spawn(_ args: JSONValue) throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        guard let command = obj["command"]?.stringValue, !command.isEmpty else {
            throw AdapterError("EINVAL", "command required")
        }
        guard let manifest = manifest, let exePath = manifest.resolveSidecar(command) else {
            throw AdapterError("EPERM", "not a declared in-bundle sidecar: \(command)")
        }
        let cmdArgs = (obj["args"]?.arrayValue ?? []).compactMap { $0.stringValue }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: exePath)
        process.arguments = cmdArgs
        let stdinPipe = Pipe(), stdoutPipe = Pipe(), stderrPipe = Pipe()
        process.standardInput = stdinPipe
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        do {
            try process.run()
        } catch {
            throw AdapterError("EIO", "failed to spawn: \(error.localizedDescription)")
        }

        let pid = Int(process.processIdentifier)
        let proc = Proc(process: process,
                        stdin: stdinPipe.fileHandleForWriting,
                        stdout: stdoutPipe.fileHandleForReading,
                        stderr: stderrPipe.fileHandleForReading)

        lock.lock(); procs[pid] = proc; lock.unlock()

        proc.stdout.handle.readabilityHandler = { [weak self] fh in self?.onData(pid: pid, which: "stdout", fh: fh) }
        proc.stderr.handle.readabilityHandler = { [weak self] fh in self?.onData(pid: pid, which: "stderr", fh: fh) }
        // Look the process up by pid rather than capturing the non-Sendable Proc.
        process.terminationHandler = { [weak self] p in
            self?.onTermination(pid: pid, status: Int(p.terminationStatus))
        }

        return .object(["pid": .number(Double(pid))])
    }

    private func onTermination(pid: Int, status: Int) {
        lock.lock()
        if let proc = procs[pid] { proc.exited = true; proc.exitCode = status }
        lock.unlock()
        maybeEmitExit(pid: pid)
    }

    // MARK: stream ingest

    private func onData(pid: Int, which: String, fh: FileHandle) {
        let data = fh.availableData

        lock.lock()
        guard let proc = procs[pid] else { lock.unlock(); return }
        let stream = which == "stdout" ? proc.stdout : proc.stderr

        if data.isEmpty {
            stream.eof = true
            fh.readabilityHandler = nil
            lock.unlock()
            maybeEmitExit(pid: pid)
            return
        }

        let wasEmpty = stream.buffer.isEmpty
        stream.buffer.append(data)
        if stream.buffer.count >= bufferCap {
            stream.paused = true
            fh.readabilityHandler = nil   // stop draining the pipe → OS backpressure
        }
        lock.unlock()

        if wasEmpty {
            emitter.emit("process.readable", .object(["pid": .number(Double(pid)), "stream": .string(which)]))
        }
    }

    // MARK: read

    private func read(_ args: JSONValue) throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        guard let pidD = obj["pid"]?.numberValue else { throw AdapterError("EINVAL", "pid required") }
        let pid = Int(pidD)
        let which = obj["stream"]?.stringValue ?? "stdout"
        let maxBytes = Int(obj["maxBytes"]?.numberValue ?? 65536)

        lock.lock()
        guard let proc = procs[pid] else { lock.unlock(); throw AdapterError("ESRCH", "no such process: \(pid)") }
        let stream = which == "stdout" ? proc.stdout : proc.stderr

        let take = min(max(maxBytes, 0), stream.buffer.count)
        let (text, consumed) = Self.validUTF8Prefix(stream.buffer, upTo: take)
        if consumed > 0 { stream.buffer.removeFirst(consumed) }

        // Resume the pipe if it was paused for backpressure and has fallen below the cap.
        if stream.paused && !stream.eof && stream.buffer.count < bufferCap {
            stream.paused = false
            stream.handle.readabilityHandler = { [weak self] fh in self?.onData(pid: pid, which: which, fh: fh) }
        }
        let eof = stream.eof && stream.buffer.isEmpty
        lock.unlock()

        maybeEmitExit(pid: pid)
        return .object(["data": .string(text), "eof": .bool(eof)])
    }

    // MARK: stdinWrite / kill

    private func stdinWrite(_ args: JSONValue) throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        guard let pidD = obj["pid"]?.numberValue else { throw AdapterError("EINVAL", "pid required") }
        guard let data = obj["data"]?.stringValue else { throw AdapterError("EINVAL", "data required") }
        lock.lock()
        let proc = procs[Int(pidD)]
        lock.unlock()
        guard let proc = proc else { throw AdapterError("ESRCH", "no such process: \(Int(pidD))") }
        do { try proc.stdin.write(contentsOf: Data(data.utf8)) }
        catch { throw AdapterError("EIO", "stdin write failed: \(error.localizedDescription)") }
        return .null
    }

    private func kill(_ args: JSONValue) throws -> JSONValue {
        let obj = args.objectValue ?? [:]
        guard let pidD = obj["pid"]?.numberValue else { throw AdapterError("EINVAL", "pid required") }
        lock.lock()
        let proc = procs[Int(pidD)]
        lock.unlock()
        guard let proc = proc else { throw AdapterError("ESRCH", "no such process: \(Int(pidD))") }
        proc.process.terminate()
        return .null
    }

    // MARK: exit

    private func maybeEmitExit(pid: Int) {
        lock.lock()
        guard let proc = procs[pid] else { lock.unlock(); return }
        let drained = proc.exited && !proc.exitEmitted
            && proc.stdout.eof && proc.stderr.eof
            && proc.stdout.buffer.isEmpty && proc.stderr.buffer.isEmpty
        if drained { proc.exitEmitted = true }
        let code = proc.exitCode
        lock.unlock()

        if drained {
            emitter.emit("process.exit", .object(["pid": .number(Double(pid)), "code": .number(Double(code))]))
        }
    }

    // Longest valid-UTF-8 prefix of the first `upTo` bytes, so a read never splits a codepoint.
    private static func validUTF8Prefix(_ data: Data, upTo: Int) -> (String, Int) {
        if upTo <= 0 { return ("", 0) }
        var end = upTo
        while end > 0 {
            let slice = data.prefix(end)
            if let s = String(data: slice, encoding: .utf8) { return (s, end) }
            end -= 1   // back off over an incomplete trailing sequence (≤3 bytes)
        }
        return ("", 0)
    }
}
