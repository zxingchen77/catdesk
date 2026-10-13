import AppKit
import ServiceManagement
import Darwin

final class CatDeskApp: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private var statusItem: NSStatusItem!
    private var titleLabel: NSTextField!
    private var detailLabel: NSTextField!
    private var statsLabel: NSTextField!
    private var footerLabel: NSTextField!
    private var loginButton: NSButton!
    private var timer: Timer?
    private var helper: Process?
    private var pipe: Pipe?
    private var output = Data()
    private var state = ConnectionState()
    private var nextRetry = Date.distantPast
    private var closing = false
    private var running = false
    private var launchIssue = ""
    private var lastStatusTitle = ""
    private let stamp: DateFormatter = {
        let value = DateFormatter()
        value.dateFormat = "HH:mm:ss"
        return value
    }()

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMenu()
        buildWindow()
        buildStatusItem()
        startHelper()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            self?.refresh()
        }
        showWindow()
    }

    private func label(_ text: String, size: CGFloat, weight: NSFont.Weight = .regular) -> NSTextField {
        let view = NSTextField(wrappingLabelWithString: text)
        view.font = .systemFont(ofSize: size, weight: weight)
        view.textColor = .labelColor
        view.translatesAutoresizingMaskIntoConstraints = false
        return view
    }

    private func buildMenu() {
        let menu = NSMenu()
        let item = NSMenuItem()
        menu.addItem(item)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "显示 CatDesk", action: #selector(showWindow), keyEquivalent: "")
        appMenu.addItem(withTitle: "使用说明", action: #selector(showHelp), keyEquivalent: "?")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "退出 CatDesk", action: #selector(quit), keyEquivalent: "q")
        for entry in appMenu.items { entry.target = self }
        item.submenu = appMenu
        NSApp.mainMenu = menu
    }

    private func buildWindow() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 610, height: 465),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "CatDesk 连接器"
        window.delegate = self
        window.isReleasedWhenClosed = false
        window.center()
        let root = window.contentView!
        let heading = label("CatDesk", size: 32, weight: .bold)
        let subheading = label("连接桌面屏，自动同步时间与电脑状态。", size: 15)
        subheading.textColor = .secondaryLabelColor
        let card = NSBox()
        card.boxType = .custom
        card.fillColor = .controlBackgroundColor
        card.borderColor = .separatorColor
        card.borderWidth = 1
        card.cornerRadius = 12
        card.translatesAutoresizingMaskIntoConstraints = false
        titleLabel = label("正在寻找设备…", size: 23, weight: .semibold)
        detailLabel = label("请使用支持数据传输的 USB 线，将桌面屏接到这台 Mac。", size: 14)
        detailLabel.textColor = .secondaryLabelColor
        statsLabel = label("连接成功后，每 2 秒同步一次。", size: 13)
        statsLabel.textColor = .secondaryLabelColor
        footerLabel = label("", size: 12)
        footerLabel.textColor = .secondaryLabelColor
        for view in [titleLabel!, detailLabel!, statsLabel!, footerLabel!] { card.contentView!.addSubview(view) }
        loginButton = NSButton(checkboxWithTitle: "登录 Mac 时自动打开（可选）", target: self,
                               action: #selector(toggleLogin))
        loginButton.translatesAutoresizingMaskIntoConstraints = false
        let hint = label("关闭窗口会继续同步。需要停止时，请点击“退出”。", size: 12)
        hint.textColor = .secondaryLabelColor
        let reconnect = NSButton(title: "重新连接", target: self, action: #selector(reconnect))
        let help = NSButton(title: "使用说明", target: self, action: #selector(showHelp))
        let exit = NSButton(title: "退出", target: self, action: #selector(quit))
        for button in [reconnect, help, exit] {
            button.bezelStyle = .rounded
            button.translatesAutoresizingMaskIntoConstraints = false
        }
        for view in [heading, subheading, card, loginButton!, hint, reconnect, help, exit] { root.addSubview(view) }
        let inside = card.contentView!
        NSLayoutConstraint.activate([
            heading.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 32),
            heading.topAnchor.constraint(equalTo: root.topAnchor, constant: 27),
            subheading.leadingAnchor.constraint(equalTo: heading.leadingAnchor),
            subheading.topAnchor.constraint(equalTo: heading.bottomAnchor, constant: 8),
            card.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 32),
            card.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -32),
            card.topAnchor.constraint(equalTo: subheading.bottomAnchor, constant: 24),
            card.heightAnchor.constraint(equalToConstant: 174),
            titleLabel.leadingAnchor.constraint(equalTo: inside.leadingAnchor, constant: 20),
            titleLabel.trailingAnchor.constraint(equalTo: inside.trailingAnchor, constant: -20),
            titleLabel.topAnchor.constraint(equalTo: inside.topAnchor, constant: 19),
            detailLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            detailLabel.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
            detailLabel.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 12),
            statsLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            statsLabel.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
            statsLabel.topAnchor.constraint(equalTo: detailLabel.bottomAnchor, constant: 13),
            footerLabel.leadingAnchor.constraint(equalTo: titleLabel.leadingAnchor),
            footerLabel.trailingAnchor.constraint(equalTo: titleLabel.trailingAnchor),
            footerLabel.topAnchor.constraint(equalTo: statsLabel.bottomAnchor, constant: 9),
            loginButton.leadingAnchor.constraint(equalTo: heading.leadingAnchor),
            loginButton.topAnchor.constraint(equalTo: card.bottomAnchor, constant: 22),
            hint.leadingAnchor.constraint(equalTo: heading.leadingAnchor),
            hint.topAnchor.constraint(equalTo: loginButton.bottomAnchor, constant: 10),
            reconnect.leadingAnchor.constraint(equalTo: heading.leadingAnchor),
            reconnect.bottomAnchor.constraint(equalTo: root.bottomAnchor, constant: -27),
            help.leadingAnchor.constraint(equalTo: reconnect.trailingAnchor, constant: 12),
            help.centerYAnchor.constraint(equalTo: reconnect.centerYAnchor),
            exit.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -32),
            exit.centerYAnchor.constraint(equalTo: reconnect.centerYAnchor)
        ])
        refreshLoginStatus()
    }

    private func buildStatusItem() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem.button?.image = NSImage(systemSymbolName: "pawprint.fill", accessibilityDescription: "CatDesk")
        let menu = NSMenu()
        let status = menu.addItem(withTitle: "正在寻找设备…", action: nil, keyEquivalent: "")
        status.isEnabled = false
        menu.addItem(.separator())
        menu.addItem(withTitle: "显示连接窗口", action: #selector(showWindow), keyEquivalent: "")
        menu.addItem(withTitle: "重新连接", action: #selector(reconnect), keyEquivalent: "")
        menu.addItem(withTitle: "使用说明", action: #selector(showHelp), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "退出 CatDesk", action: #selector(quit), keyEquivalent: "q")
        for item in menu.items { item.target = self }
        statusItem.menu = menu
    }

    private func startHelper() {
        guard !closing, helper == nil else { return }
        state.reset()
        launchIssue = ""
        output.removeAll(keepingCapacity: true)
        guard let url = Bundle.main.url(forAuxiliaryExecutable: "CatDeskHelper") else {
            launchIssue = "程序文件不完整，请重新解压整个文件夹。"
            nextRetry = .distantFuture
            refresh()
            return
        }
        let process = Process()
        let channel = Pipe()
        process.executableURL = url
        process.arguments = ["--trace"]
        process.standardError = channel
        process.standardOutput = FileHandle.nullDevice
        process.standardInput = FileHandle.nullDevice
        channel.fileHandleForReading.readabilityHandler = { [weak self, weak process] handle in
            let data = handle.availableData
            if data.isEmpty { handle.readabilityHandler = nil; return }
            DispatchQueue.main.async {
                guard let self, self.helper === process else { return }
                self.receive(data)
            }
        }
        process.terminationHandler = { [weak self] ended in
            DispatchQueue.main.async {
                guard let self, self.helper === ended else { return }
                self.pipe?.fileHandleForReading.readabilityHandler = nil
                let remainder = self.pipe?.fileHandleForReading.readDataToEndOfFile() ?? Data()
                self.receive(remainder)
                self.helper = nil
                self.pipe = nil
                self.running = false
                self.nextRetry = self.state.anotherHelper ? .distantFuture : Date().addingTimeInterval(5)
                self.refresh()
            }
        }
        helper = process
        pipe = channel
        do {
            try process.run()
            running = true
        } catch {
            channel.fileHandleForReading.readabilityHandler = nil
            helper = nil
            pipe = nil
            running = false
            launchIssue = "连接程序未能启动：\(error.localizedDescription)"
            nextRetry = Date().addingTimeInterval(5)
        }
        refresh()
    }

    private func receive(_ data: Data) {
        output.append(data)
        while let newline = output.firstIndex(of: 10) {
            let line = String(decoding: output[..<newline], as: UTF8.self)
            output.removeSubrange(...newline)
            state.ingest(line)
        }
        // A broken peer must not grow the application's receive buffer forever.
        if output.count > 32_768 { output.removeAll(keepingCapacity: true) }
        refresh()
    }

    private func refresh() {
        guard titleLabel != nil else { return }
        let now = Date()
        let connected = running && state.synchronized(at: now)
        let title: String
        if connected {
            title = "已连接 · 正在同步"
            titleLabel.textColor = .systemGreen
            detailLabel.stringValue = "桌面屏已确认收到当前时间和这台 Mac 的状态。"
            statsLabel.stringValue = state.statistics
            let recent = min(state.lastTimeAck!, state.lastMacAck!)
            footerLabel.stringValue = "上次同步 \(stamp.string(from: recent)) · 每 2 秒更新"
        } else {
            title = state.anotherHelper ? "已有连接程序在运行" : "正在寻找设备…"
            titleLabel.textColor = .systemOrange
            if !launchIssue.isEmpty { detailLabel.stringValue = launchIssue }
            else if !state.issue.isEmpty { detailLabel.stringValue = state.issue }
            else { detailLabel.stringValue = "请用支持数据传输的 USB 线接到 Mac，设备需要通电。" }
            statsLabel.stringValue = state.anotherHelper
                ? "退出原连接程序后，点击“重新连接”。"
                : "拔插后会自动重连，无需选择串口。"
            footerLabel.stringValue = "如果一直没有连接，请查看“使用说明”。"
        }
        titleLabel.stringValue = title
        if title != lastStatusTitle {
            statusItem?.menu?.items.first?.title = title
            statusItem?.button?.toolTip = "CatDesk：\(title)"
            lastStatusTitle = title
        }
        if !closing, !running, helper == nil, now >= nextRetry { startHelper() }
    }

    private func stopHelper() {
        guard let process = helper else { return }
        process.terminationHandler = nil
        pipe?.fileHandleForReading.readabilityHandler = nil
        if process.isRunning {
            process.terminate()
            let until = Date().addingTimeInterval(2)
            while process.isRunning && Date() < until { Thread.sleep(forTimeInterval: 0.02) }
            if process.isRunning { _ = Darwin.kill(process.processIdentifier, SIGKILL) }
        }
        helper = nil
        pipe = nil
        running = false
    }

    @objc private func reconnect() {
        stopHelper()
        startHelper()
    }

    @objc private func showWindow() {
        window.makeKeyAndOrderFront(nil)
        NSApp.activate()
    }

    @objc private func showHelp() {
        if let url = Bundle.main.url(forResource: "使用说明", withExtension: "txt") {
            NSWorkspace.shared.open(url)
        }
    }

    private func refreshLoginStatus() {
        let status = SMAppService.mainApp.status
        loginButton.state = status == .enabled || status == .requiresApproval ? .on : .off
    }

    @objc private func toggleLogin() {
        do {
            if loginButton.state == .on {
                if SMAppService.mainApp.status != .enabled { try SMAppService.mainApp.register() }
                if SMAppService.mainApp.status == .requiresApproval {
                    let alert = NSAlert()
                    alert.messageText = "请允许 CatDesk 在登录时打开"
                    alert.informativeText = "在系统设置的“登录项与扩展”里允许 CatDesk。建议先将应用放到“应用程序”文件夹，避免移动文件后找不到它。"
                    alert.addButton(withTitle: "打开系统设置")
                    alert.addButton(withTitle: "稍后")
                    if alert.runModal() == .alertFirstButtonReturn { SMAppService.openSystemSettingsLoginItems() }
                }
            } else { try SMAppService.mainApp.unregister() }
        } catch {
            let alert = NSAlert()
            alert.messageText = "自动打开设置未能完成"
            alert.informativeText = "\(error.localizedDescription)\n你仍可以直接双击应用使用。"
            alert.runModal()
        }
        refreshLoginStatus()
    }

    @objc private func quit() { NSApp.terminate(nil) }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showWindow()
        return true
    }

    func applicationWillTerminate(_ notification: Notification) {
        closing = true
        timer?.invalidate()
        stopHelper()
    }
}

let application = NSApplication.shared
let delegate = CatDeskApp()
application.setActivationPolicy(.regular)
application.delegate = delegate
application.run()
