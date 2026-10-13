# CatDesk 原创生活 RPG 视觉版

当前实机为 [情侣随机走动固件](couple-walk-2026-10-06/catdesk-couple-walk.bin)，784,176 字节，SHA-256 `fba9ef73dbb9a6f9bc953d1594c454a55b4a4c134304b6a36ba5360af93ee327`；1MiB 应用区剩余 264,400 字节（25.2%）。仅 `app-flash` 写 0x10000 应用区并完成校验。进入房间即可见两人随机走动；新增情侣相框已移除，原低频沙发事件和周年结尾保留。同源[软件预览](../assets/couple-walk-2026-10-06/couple_walk.gif)与验收边界见[进度记录](../CATDESK_PROGRESS.md)。

上一轮为 [情侣小人固件](couple-2026-10-06/catdesk-couple.bin)，782,336 字节，SHA-256 `12bafb5f8edbb87d17bafcc0badd3a006153fe9e05ceab8cb91ea6448238c5ad`；1MiB 应用区剩余 266,240 字节（25.4%），比 Welcome 增加 3,120 字节。仅 `app-flash` 写 0x10000 应用区并完成校验。包含原 Welcome、房间生活和本轮静态人物、常驻合照、低频沙发访客与 2.5 秒周年结尾；同源[软件预览](../assets/couple-2026-10-06/couple_anniversary.gif)与验收边界见[进度记录](../CATDESK_PROGRESS.md)。`couple-2026-10-06/` 保存应用、SHA 清单和 manifest。

上一轮为 [Welcome 固件](welcome-2026-10-06/catdesk-welcome.bin)，779,216 字节，SHA-256 `e62ce9d7047585464bcfcdc6afd0c4735c526dcc71aaefe3f5396c720f7189d2`；应用区剩余 269,360 字节（25.7%）。2026-10-06 只写应用分区，数据校验成功；实机流程为 4 秒开场 → 等待触摸 → 双猫跑门/开门暖光/800ms tile wipe → 原主页。它包含下述房间候选源码。`welcome-2026-10-06/` 保存二进制、SHA 清单、保护文件清单和 manifest。

原独立的[昼夜与猫生活候选固件](candidate-2026-10-06/catdesk-room-candidate.bin)：773,312 字节，应用分区剩余 275,264 字节（26.3%）。SHA-256 `baed4b7023c976c8ef655d050774a2f94e2b5fd7797e4ae246245bb0628cc706`。该独立 `.bin` 未单独烧录；其源码已包含在当前 Welcome 固件。房间候选功能仍未完成 1–2 小时实机验收；原候选文件和礼物回退版保持原样。详见 `../CATDESK_PROGRESS.md`。

原实机回退固件为 `catdesk-gift-2026-10-07.bin`，与 `firmware/build-rpg-final/catdesk.bin` 完全一致，大小 **770,288 字节**。SHA-256 见 `SHA256SUMS`，应用分区余量约 **26.5%**。以下为该版历史记录。

场景已重新构图为大片木地板、短北墙、三分之四视角家具；双猫 44 帧重画为 25–29 px 高的宠物 NPC。业务模型、保存格式、USB、Helper、LCD/GPIO/RGB 硬件参数保持原样。仅家具入口坐标随物件移动，以及周年当天点相框直接进入原有事件。

本次还有真正的双页任务本、厚木框羊皮纸菜单、开口清晰的 bitmap 数字、35px 宽的家具微字日期、木槽 Options、离散变暗的周年剧情。没有使用 Stardew Valley 的任何资产。

本轮在原布局上补充串灯/照片、垂挂植物、毛毯/双靠枕、CAT纸箱、杯/花束/两人相框、奶油绿绣花地毯和集中摆放的四件猫用品；主要31色。流星、飞蛾偶发且只局部刷新，睡觉z保留，猫状态机与原功能入口保持不变。

所有截图/动画由当前固件同一渲染器生成：`../assets/world_preview.png`（无文字世界）、`home_preview.png`、`popups_preview.png`、`anniversary_preview.gif`、`cats_live_preview.gif`。它们不代表实机照片。

针对用户报告的整幅画面移位，本版把应用层呈现改为双缓冲直接刷新和同核绘制，避免每次局部刷新补拷全屏；启动首帧和保存后请求一次VSYNC重同步，保存失败至少间隔2秒重试。严格 C/ASan 回归、逐像素刷新、持久化与 Helper 自动检查见 `../CATDESK_PROGRESS.md`。修正呈现模式后78秒运行检查无crash或reboot loop，RAM约214KB、PSRAM约4.9MB。没有erase-flash，只写0x10000应用分区，Bootloader和Partition不改。

用户已实机确认新版“画面稳定，点击正常”。本轮78秒启动观察无crash/reboot loop，RAM约214KB、PSRAM约4.9MB；64秒真实Helper检查31/31 TIME与31/31 MAC ACK通过，无超时/重连。

已知限制：天气为mock，待办固定预设，软件亮度，无断电保持时钟，专注不跨断电保存；断电后接运行中的Helper校时。当前macOS15.7.5已实测，macOS26未实测。所有窗口逐项手指操作、滑块具体观感及真实拔插USB未逐项获得人工反馈。
