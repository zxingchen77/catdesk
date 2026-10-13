import Foundation

struct ConnectionState {
    var port = ""
    var lastTimeAck: Date?
    var lastMacAck: Date?
    var statistics = ""
    var issue = ""
    var anotherHelper = false

    mutating func reset() {
        self = ConnectionState()
    }

    mutating func ingest(_ line: String, at now: Date = Date()) {
        let prefix = "CatDeskHelper: "
        guard line.hasPrefix(prefix) else { return }
        let message = String(line.dropFirst(prefix.count))
        if message.hasPrefix("connected to ") {
            port = String(message.dropFirst("connected to ".count))
            lastTimeAck = nil
            lastMacAck = nil
            issue = ""
            anotherHelper = false
        } else if message.contains("another CatDeskHelper is running") {
            issue = "已有另一个 CatDesk 连接程序在运行，请先退出它。"
            anotherHelper = true
            lastTimeAck = nil
            lastMacAck = nil
        } else if message.hasPrefix("/dev/") {
            issue = message.contains("timed out")
                ? "设备暂时没有回应，正在自动重新连接。"
                : "串口暂时不可用，正在自动重新连接。"
            lastTimeAck = nil
            lastMacAck = nil
        } else if let value = quotedValue(message, prefix: "RX raw ", suffix: " hex=") {
            let response = value.trimmingCharacters(in: CharacterSet(charactersIn: "\r\n"))
            if response == "CD1 OK TIME" { lastTimeAck = now; issue = "" }
            if response == "CD1 OK MAC" { lastMacAck = now; issue = "" }
        } else if let value = quotedValue(message, prefix: "TX ", suffix: nil) {
            let fields = value.split(separator: " ")
            if fields.count == 6, fields[0] == "CD1", fields[1] == "MAC",
               let battery = Int(fields[2]), let charging = Int(fields[3]),
               let cpu = Int(fields[4]), let memory = Int(fields[5]) {
                statistics = "电量 \(battery)%\(charging == 1 ? " · 充电中" : "")   CPU \(cpu)%   内存 \(memory)%"
            }
        }
    }

    func synchronized(at now: Date = Date()) -> Bool {
        guard !port.isEmpty, let time = lastTimeAck, let mac = lastMacAck else { return false }
        let timeAge = now.timeIntervalSince(time)
        let macAge = now.timeIntervalSince(mac)
        return (0...6).contains(timeAge) && (0...6).contains(macAge)
    }

    private func quotedValue(_ message: String, prefix: String, suffix: String?) -> String? {
        guard message.hasPrefix(prefix) else { return nil }
        var value = String(message.dropFirst(prefix.count))
        if let suffix, let range = value.range(of: suffix) { value = String(value[..<range.lowerBound]) }
        return (try? JSONSerialization.jsonObject(with: Data(value.utf8), options: .fragmentsAllowed)) as? String
    }
}
