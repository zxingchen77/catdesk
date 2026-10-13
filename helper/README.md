# CatDesk Helper

轻量 macOS 15+ 命令行工具。自动识别 CatDesk USB 串口，每两秒校准 UTC 时间并发送真实电量、充电状态、CPU 和内存数据；拔插后自动重连。无需 Python、第三方包或常驻图形界面。

可转发的图形版由 `python3 helper/package_macos.py` 在仓库根目录构建到 `exports/`。它包含同时支持 Apple 芯片与 Intel 的 `CatDesk连接器.app`、中文说明和 ZIP，接收方不需要运行构建脚本或安装开发工具。macOS 15+ 双击应用自动开始同步，窗口只在收到 TIME 和 MAC 两种真实回复后显示“已连接”；菜单栏入口、自动重连与可选登录启动均内置。关闭窗口保持同步，退出应用释放串口。首次打开未经 Apple 公证的转发包可能需要在系统“隐私与安全性”里选择“仍要打开”，详见包内说明。

打包工具只进行本地签名，未宣称 Developer ID 签名或 Apple 公证；当前 ARM Mac 真机验证与 Intel 编译验证需分别记录。构建时检测重复的 Swift module map，仅用本次编译的 VFS overlay 屏蔽，不修改系统工具链。

```sh
./helper/build.sh
./helper/run.sh --diagnostics  # 仅输出本机数据，不打开串口
./helper/run.sh --once         # 完成一次身份确认、时间/数据同步后退出
./helper/run.sh                # 保持同步，Ctrl-C 退出
```

默认只扫描 `/dev/cu.wchusbserial*`、`/dev/cu.usbserial*`、`/dev/cu.usbmodem*`，优先 WCH。需要指定设备或测试 PTY 时，可传 `--port /dev/cu.…`。收到精确的 `CD1 READY CATDESK 1` 身份回复后才发送时间和 Mac 数据，普通 ESP 日志会被忽略。工具不发送 DTR/RTS 复位操作，禁用关闭串口时的 HUPCL，并独占已打开的串口。

排查通信时可加 `--trace`，它只输出 CD1 候选行和最多 100 字节的原始十六进制；不会打印整段 ESP 日志。接收端容忍控制台层产生的重复尾部 CR，身份正文仍要求精确匹配。

**烧录前先退出 Helper。** 前台进程按 Ctrl-C；已安装登录服务则运行：

```sh
launchctl bootout "gui/$(id -u)/com.catdesk.helper"
```

烧录完成后再运行 `helper/run.sh`，或重新执行安装脚本。单实例锁避免两个 Helper 同时抢占串口。

可选登录自动启动，只在明确运行安装脚本时安装：

```sh
./helper/install.sh
./helper/uninstall.sh
```

安装位置为当前用户的 `~/Library/Application Support/CatDesk`；LaunchAgent 名称为 `com.catdesk.helper`，日志位于 `~/Library/Logs/CatDesk/helper.log`。不需要管理员权限。

CPU 为相邻采样间隔中全机 CPU 的忙碌比例，按 0–100% 显示。内存使用 Mach 页计数估算：`max(anonymous − purgeable, 0) + wired + compressor`，除以物理内存页数并限制到 0–100%。它排除文件缓存和可清除匿名页，不等同于 Activity Monitor 精确值或内存压力。电量来自 IOKit 内置电池；无电池的台式 Mac 发送 0%，`--diagnostics` 同时显示 `hasBattery: false`。

设备在完全断电期间没有可靠的离线时钟。USB 连接运行中的 Helper 后会使用 Mac 当前时间校准；未连接时，设备只能从构建时间或最近保存的设备时间继续计时。为了在断电后准确显示 10 月 7 日的周年内容，请连接 Mac 同步时间，或保持设备跨午夜供电。

协议为 115200 baud、8N1、LF 分行、ASCII：

```text
CD1 HELLO
CD1 READY CATDESK 1
CD1 TIME <UTC epoch seconds>
CD1 OK TIME
CD1 MAC <battery 0..100> <charging 0|1> <CPU 0..100> <memory 0..100>
CD1 OK MAC
```

TIME/MAC 各自六秒未获回复会重连。`--once` 未找到有效设备时以非零状态退出。

已通过的 PTY 回归测试保存在 `helper/test_helper.py`。先构建并退出正在运行的 Helper，然后执行 `python3 helper/test_helper.py`；它只打开测试用伪终端，覆盖身份拒绝、ESP 日志过滤、分段 CRLF、时间和数据范围、双回复确认、单实例、两秒心跳与丢失回复后的重连。Python 仅用于这项可选测试，Helper 本身无 Python 依赖。
