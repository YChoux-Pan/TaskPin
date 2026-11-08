# C++/Qt 重写计划（TaskPinQt）

C# 版本（`repo/`）保留为**功能参照**，新实现放在 `qt/TaskPinQt/`，逐模块迁移、逐模块验收。

## 环境（本机实测通过）

| 组件 | 版本 / 路径 |
|---|---|
| Qt | 6.4.3 · `D:\AutoEx\Qt\Qt_6.4.3\6.4.3\mingw_64`（MinGW 套件） |
| 编译器 | MinGW-W64 11.2.0 · `...\Tools\mingw1120_64\bin\g++.exe` |
| CMake | 3.24.2 · `...\Tools\CMake_64\bin\cmake.exe` |
| 构建器 | Ninja 1.10.2 · `...\Tools\Ninja\ninja.exe` |
| 界面 | Qt Widgets（不引入 QML，保持与旧版一致的绘制式界面） |
| 数据库 | Qt SQL + `QSQLITE` 驱动（schema 与 C# 版一致） |

构建：`D:\TaskPin\qt\TaskPinQt\build.cmd` → 产物 `build\TaskPinQt.exe`（已通过 windeployqt 带齐运行库）。

## 迁移状态

| 模块 | C# 实现 | Qt 状态 |
|---|---|---|
| 单实例 | 命名互斥体 | ✅ QLocalServer |
| 托盘 + 菜单 | NotifyIcon | ✅ QSystemTrayIcon |
| 全局热键 | RegisterHotKey + WndProc | ✅ RegisterHotKey + native event filter |
| 任务库 / 顺延 / 优先级 | SQLite (Microsoft.Data.Sqlite) | ✅ QSqlDatabase（schema 兼容） |
| 桌面卡片（绘制/拖拽/滚轮/右键） | 分层窗口 + GDI+ | ✅ QWidget 透明绘制 |
| 速记输入 | QuickInputForm | ✅ QuickAddDialog |
| 回收站 | RecycleBinForm | ✅ RecycleDialog |
| 截图浮层 + 标注 | SnipOverlay + Annotations | ✅ 冻结画面 / 选区 / 11 工具 / 复制 |
| 贴图窗口 | PinWindow（ULW） | ⏳ |
| 截屏历史 | HistoryForm | ⏳ |
| 取色器 | ColorPickerOverlay | ⏳ |
| 日历 | CalendarForm | ⏳ |
| 白板 / 透明白板 | WhiteboardForm / TransparentBoard | ⏳ |
| 离线 OCR | Windows.Media.Ocr (WinRT) | ⏳ 需 C++/WinRT 或 Windows Data OCR |
| 扫码 | ZXing.Net | ⏳ 需 zxing-cpp |
| AI 归纳 | HttpClient | ⏳ |
| 命令行通道 | 命名管道 | ✅ QLocalServer（`--snip --quick --toggle --quit`）|
| 设置窗口 | SettingsForm | ⏳ |

## 与旧版的数据隔离

- Qt 版数据目录：`%LOCALAPPDATA%\TaskPin\TaskPinQt\`
- C# 版数据目录：`%LOCALAPPDATA%\TaskPin\`
- 两者 schema 相同，后续会加一个「从 C# 版导入」的动作。

'## 截图编辑器（Qt 版）现状

已完成：冻结桌面（多屏合成 + devicePixelRatio 还原）、遮罩、拖拽选区、四角手柄、尺寸标签、
放大镜（8× + 坐标 + 色值）、11 个工具图标 + 7 色板 + 线宽 + 撤销/重做/复制/保存/贴图/OCR/关闭、
矩形 / 椭圆 / 箭头 / 直线 / 画笔 / 荧光 / 文字 / 序号、Esc 取消、空格全屏、Enter 复制。

实测（模拟鼠标 + CLI 触发）：选区 301×141 → 画红色矩形 → Enter → 剪贴板得到 376×176 图像
（301×1.25=376，DPI 换算正确），标注已合成。

下一步：窗口检测/吸附、马赛克/橡皮、贴图窗口、OCR、保存对话框联调。

## 已知差异 / 待办'

- 卡片背景目前用 `QWidget` + `WA_TranslucentBackground` 绘制；贴图窗口若要逐像素透明，需要 `WS_EX_LAYERED` + `UpdateLayeredWindow`（与 C# 版相同思路）。
- 截图浮层需要多屏拼接、DPI 感知的冻结画面；Qt 的 `QScreen::grabWindow(0)` 在 125% 缩放下需要按 `devicePixelRatio` 还原。
- OCR 需要引入 C++/WinRT（`winrt::Windows::Media::Ocr`），构建配置会稍复杂，计划单独一个里程碑。
