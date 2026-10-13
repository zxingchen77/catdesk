import Foundation
import IOKit.ps
import Darwin

// Intentionally a foreground CLI: installing a LaunchAgent is an explicit,
// separate action. All serial parsing and polling runs on this one thread.
private var stopRequested = false
private var traceEnabled = false

private struct HelperError: Error, CustomStringConvertible {
    let description: String
}

private func log(_ message: String) {
    let line = "CatDeskHelper: \(message)\n"
    FileHandle.standardError.write(Data(line.utf8))
}

private func systemError(_ operation: String) -> HelperError {
    HelperError(description: "\(operation): \(String(cString: strerror(errno)))")
}

private struct Options {
    var once = false
    var diagnostics = false
    var trace = false
    var port: String?

    init() throws {
        var args = Array(CommandLine.arguments.dropFirst())
        while !args.isEmpty {
            let option = args.removeFirst()
            switch option {
            case "--once": once = true
            case "--diagnostics": diagnostics = true
            case "--trace": trace = true
            case "--port":
                guard !args.isEmpty else { throw HelperError(description: "--port needs a device path") }
                port = args.removeFirst()
            case "--help", "-h":
                print("""
                CatDeskHelper — automatic USB time and Mac statistics for CatDesk
                Usage: CatDeskHelper [--once] [--trace] [--port /dev/cu.…]
                       CatDeskHelper --diagnostics
                --once         Connect, identify, sync once, await both acknowledgments, exit.
                --diagnostics  Print real Mac statistics as JSON without opening any serial port.
                --port PATH    Use one explicit character device (also permits PTY tests).
                --trace        Log sent/received CD1 protocol lines and bounded raw RX hex.
                Normal mode automatically discovers and reconnects supported USB serial ports.
                """)
                exit(0)
            default: throw HelperError(description: "unknown option: \(option)")
            }
        }
        if diagnostics && once {
            throw HelperError(description: "--diagnostics and --once are separate modes")
        }
    }
}

private struct MacStats: Codable {
    let epoch: Int64
    let battery: Int
    let charging: Bool
    let hasBattery: Bool
    let cpu: Int
    let memory: Int
}

private final class StatsCollector {
    private let host = mach_host_self()
    private var previousCPU: [UInt32]?
    private var pageSize: vm_size_t = 4096

    init() {
        _ = host_page_size(host, &pageSize)
        previousCPU = cpuTicks()
    }

    deinit { mach_port_deallocate(mach_task_self_, host) }

    private func cpuTicks() -> [UInt32]? {
        var info = host_cpu_load_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics(host, HOST_CPU_LOAD_INFO, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return nil }
        return [info.cpu_ticks.0, info.cpu_ticks.1, info.cpu_ticks.2, info.cpu_ticks.3]
    }

    private func cpuPercent() -> Int {
        guard let ticks = cpuTicks() else { return 0 }
        defer { previousCPU = ticks }
        guard let old = previousCPU else { return 0 }
        let delta = zip(ticks, old).map { UInt64($0 &- $1) }
        let total = delta.reduce(0, +)
        guard total > 0 else { return 0 }
        return min(100, max(0, Int((total - delta[Int(CPU_STATE_IDLE)]) * 100 / total)))
    }

    private func memoryPercent() -> Int {
        var info = vm_statistics64_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let result = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(host, HOST_VM_INFO64, $0, &count)
            }
        }
        guard result == KERN_SUCCESS else { return 0 }
        let physical = ProcessInfo.processInfo.physicalMemory
        guard physical > 0 else { return 0 }
        // Local mach/vm_statistics.h defines internal pages as anonymous,
        // external pages as file-backed, and compressor pages as physical pages
        // holding compressed data. Exclude purgeable anonymous pages and file
        // cache; this is an estimate, not Activity Monitor or memory pressure.
        let anonymous = UInt64(info.internal_page_count)
        let purgeable = UInt64(info.purgeable_count)
        let appPages = anonymous > purgeable ? anonymous - purgeable : 0
        let usedPages = appPages + UInt64(info.wire_count) + UInt64(info.compressor_page_count)
        let totalPages = physical / UInt64(pageSize)
        guard totalPages > 0 else { return 0 }
        return Int(min(usedPages, totalPages) * 100 / totalPages)
    }

    private func batteryStatus() -> (Int, Bool, Bool) {
        guard let info = IOPSCopyPowerSourcesInfo()?.takeRetainedValue(),
              let sources = IOPSCopyPowerSourcesList(info)?.takeRetainedValue() as? [CFTypeRef] else {
            return (0, false, false)
        }
        for source in sources {
            guard let raw = IOPSGetPowerSourceDescription(info, source)?.takeUnretainedValue(),
                  let description = raw as? [String: Any],
                  (description[kIOPSTypeKey] as? String) == kIOPSInternalBatteryType,
                  let current = description[kIOPSCurrentCapacityKey] as? Int,
                  let maximum = description[kIOPSMaxCapacityKey] as? Int, maximum > 0 else { continue }
            let percentage = min(100, max(0, current * 100 / maximum))
            return (percentage, description[kIOPSIsChargingKey] as? Bool ?? false, true)
        }
        // Desktop Macs have no internal battery; the wire protocol uses 0.
        return (0, false, false)
    }

    func collect() -> MacStats {
        let (battery, charging, hasBattery) = batteryStatus()
        return MacStats(epoch: Int64(Date().timeIntervalSince1970), battery: battery,
                        charging: charging, hasBattery: hasBattery,
                        cpu: cpuPercent(), memory: memoryPercent())
    }
}

private final class InstanceLock {
    private var descriptor: Int32 = -1

    init() throws {
        let path = "/tmp/catdesk-helper-\(getuid()).lock"
        descriptor = Darwin.open(path, O_RDWR | O_CREAT | O_NOFOLLOW, mode_t(0o600))
        guard descriptor >= 0 else { throw systemError("open instance lock") }
        if flock(descriptor, LOCK_EX | LOCK_NB) != 0 {
            Darwin.close(descriptor)
            descriptor = -1
            throw HelperError(description: "another CatDeskHelper is running; stop it before flashing or testing")
        }
        _ = ftruncate(descriptor, 0)
        let pid = "\(getpid())\n"
        _ = pid.withCString { Darwin.write(descriptor, $0, strlen($0)) }
    }

    deinit {
        if descriptor >= 0 {
            _ = flock(descriptor, LOCK_UN)
            Darwin.close(descriptor)
        }
        // Keep the inode: unlinking an advisory lock can permit two instances.
    }
}

private final class SerialPort {
    let path: String
    let descriptor: Int32
    private var lineBytes: [UInt8] = []

    init(path: String) throws {
        self.path = path
        var fileInfo = stat()
        guard stat(path, &fileInfo) == 0 else { throw systemError("stat \(path)") }
        guard fileInfo.st_mode & mode_t(S_IFMT) == mode_t(S_IFCHR) else {
            throw HelperError(description: "\(path) is not a character device")
        }
        let fd = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else { throw systemError("open \(path)") }
        var configured = false
        defer { if !configured { Darwin.close(fd) } }
        var options = termios()
        guard tcgetattr(fd, &options) == 0 else { throw systemError("tcgetattr") }
        cfmakeraw(&options)
        options.c_cflag |= tcflag_t(CLOCAL | CREAD)
        options.c_cflag &= ~tcflag_t(HUPCL | CCTS_OFLOW | CRTS_IFLOW | CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW)
        guard cfsetispeed(&options, speed_t(B115200)) == 0,
              cfsetospeed(&options, speed_t(B115200)) == 0,
              tcsetattr(fd, TCSANOW, &options) == 0 else { throw systemError("configure 115200 baud") }
        // No modem-control ioctl: no DTR/RTS assertion, toggling, or reset.
        // Exclusive access prevents another program opening this port midway.
        guard ioctl(fd, TIOCEXCL) == 0 else { throw systemError("exclusive serial access") }
        _ = tcflush(fd, TCIFLUSH)
        descriptor = fd
        configured = true
    }

    deinit { Darwin.close(descriptor) }

    func send(_ line: String) throws {
        if traceEnabled { log("TX \(line.debugDescription)") }
        let bytes = Array((line + "\n").utf8)
        let deadline = Date().timeIntervalSince1970 + 1
        try bytes.withUnsafeBytes { buffer in
            var offset = 0
            while offset < buffer.count {
                let written = Darwin.write(descriptor, buffer.baseAddress!.advanced(by: offset), buffer.count - offset)
                if written > 0 { offset += written; continue }
                if written < 0 && errno != EAGAIN && errno != EINTR {
                    throw systemError("serial write")
                }
                if Date().timeIntervalSince1970 >= deadline || stopRequested {
                    throw HelperError(description: "serial write timed out")
                }
                var event = pollfd(fd: descriptor, events: Int16(POLLOUT), revents: 0)
                _ = poll(&event, 1, 20)
                if event.revents & Int16(POLLHUP | POLLERR | POLLNVAL) != 0 {
                    throw HelperError(description: "USB serial disconnected")
                }
            }
        }
    }

    func readLines(waitMilliseconds: Int32 = 100) throws -> [String] {
        var event = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
        let polled = poll(&event, 1, waitMilliseconds)
        if polled < 0 && errno != EINTR { throw systemError("serial poll") }
        if event.revents & Int16(POLLHUP | POLLERR | POLLNVAL) != 0 {
            throw HelperError(description: "USB serial disconnected")
        }
        var lines: [String] = []
        var chunk = [UInt8](repeating: 0, count: 2048)
        while true {
            let received = Darwin.read(descriptor, &chunk, chunk.count)
            if received < 0 {
                if errno == EAGAIN || errno == EINTR { break }
                throw systemError("serial read")
            }
            if received == 0 { break }
            for byte in chunk.prefix(received) {
                if byte == 10 {
                    if traceEnabled, let raw = String(bytes: lineBytes, encoding: .utf8), raw.contains("CD1") {
                        let bounded = Array(lineBytes.prefix(100))
                        let hex = bounded.map { String(format: "%02X", $0) }.joined(separator: " ")
                        let repr = String(decoding: bounded, as: UTF8.self).debugDescription
                        log("RX raw \(repr) hex=\(hex)\(lineBytes.count > 100 ? " …" : "")")
                    }
                    // Some console layers produce CR CR LF. Strip only end CRs;
                    // no prefix, spaces, or internal control bytes are accepted.
                    while lineBytes.last == 13 { lineBytes.removeLast() }
                    if let line = String(bytes: lineBytes, encoding: .utf8) { lines.append(line) }
                    lineBytes.removeAll(keepingCapacity: true)
                } else if lineBytes.count < 8192 {
                    lineBytes.append(byte)
                } else {
                    lineBytes.removeAll(keepingCapacity: true)
                }
            }
        }
        return lines
    }
}

private func candidatePorts(explicit: String?) -> [String] {
    if let explicit { return [explicit] }
    let prefixes = ["cu.wchusbserial", "cu.usbserial", "cu.usbmodem"]
    let names = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
    return names.filter { name in prefixes.contains { name.hasPrefix($0) } }
        .sorted { a, b in
            let aRank = prefixes.firstIndex { a.hasPrefix($0) } ?? prefixes.count
            let bRank = prefixes.firstIndex { b.hasPrefix($0) } ?? prefixes.count
            return aRank == bRank ? a < b : aRank < bRank
        }.map { "/dev/" + $0 }
}

private func identify(_ port: SerialPort) throws -> Bool {
    let start = Date().timeIntervalSince1970
    var nextHello = start
    while Date().timeIntervalSince1970 - start < 3 && !stopRequested {
        let now = Date().timeIntervalSince1970
        if now >= nextHello {
            try port.send("CD1 HELLO")
            nextHello = now + 0.8
        }
        for line in try port.readLines() {
            // ESP-IDF boot/log lines and every other board identity are ignored.
            if line == "CD1 READY CATDESK 1" { return true }
        }
    }
    return false
}

private func synchronize(_ port: SerialPort, collector: StatsCollector, once: Bool) throws {
    var nextUpdate = 0.0
    var lastTimeAck = Date().timeIntervalSince1970
    var lastMacAck = lastTimeAck
    var timeAcknowledged = false, macAcknowledged = false
    while !stopRequested {
        let now = Date().timeIntervalSince1970
        if now >= nextUpdate {
            let stats = collector.collect()
            try port.send("CD1 TIME \(stats.epoch)")
            try port.send("CD1 MAC \(stats.battery) \(stats.charging ? 1 : 0) \(stats.cpu) \(stats.memory)")
            nextUpdate = now + 2
        }
        for line in try port.readLines() {
            if line == "CD1 OK TIME" {
                lastTimeAck = Date().timeIntervalSince1970
                timeAcknowledged = true
            } else if line == "CD1 OK MAC" {
                lastMacAck = Date().timeIntervalSince1970
                macAcknowledged = true
            }
        }
        if once && timeAcknowledged && macAcknowledged { return }
        if now - lastTimeAck > 6 || now - lastMacAck > 6 {
            throw HelperError(description: "CatDesk acknowledgment timed out; reconnecting")
        }
    }
}

private func run(_ options: Options) throws {
    traceEnabled = options.trace
    let collector = StatsCollector()
    if options.diagnostics {
        Thread.sleep(forTimeInterval: 0.25)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(collector.collect())
        print(String(decoding: data, as: UTF8.self))
        return
    }
    let instanceLock = try InstanceLock()
    defer { withExtendedLifetime(instanceLock) {} }
    let onceDeadline = Date().timeIntervalSince1970 + 12
    var cooldown: [String: Double] = [:]
    log("searching for CatDesk USB serial; stop the helper before firmware flashing")
    while !stopRequested {
        for path in candidatePorts(explicit: options.port) {
            if stopRequested { break }
            let now = Date().timeIntervalSince1970
            if !options.once && now < (cooldown[path] ?? 0) { continue }
            do {
                let port = try SerialPort(path: path)
                guard try identify(port) else {
                    cooldown[path] = now + 15
                    if options.port != nil { log("no CatDesk identity on \(path)") }
                    continue
                }
                log("connected to \(path)")
                try synchronize(port, collector: collector, once: options.once)
                if options.once { log("time and Mac statistics acknowledged"); return }
            } catch {
                log("\(path): \(error)")
                cooldown[path] = Date().timeIntervalSince1970 + 1
            }
        }
        if options.once && Date().timeIntervalSince1970 >= onceDeadline {
            throw HelperError(description: "no responsive CatDesk found within 12 seconds")
        }
        Thread.sleep(forTimeInterval: 0.5)
    }
}

signal(SIGINT) { _ in stopRequested = true }
signal(SIGTERM) { _ in stopRequested = true }
signal(SIGPIPE, SIG_IGN)
do { try run(Options()) }
catch { log(String(describing: error)); exit(1) }
