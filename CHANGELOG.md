# 更新日志（CHANGELOG）

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循语义化版本。

> 从 0.2 起 **Qt 主线**有独立版本号（`qt-0.x.y`）。C# 旧版（0.3.x）已归档删除，
> 其历史保存在 `D:\TaskPin\backup\repo-csharp-history.bundle`（机器外无需保留）。

## [qt-0.2.1] - 2026-11-09

### 新增
- **卡片层级切换**（T11）：桌面卡片可以在「全局置顶」和「桌面层」之间切换。
  桌面层 = 壁纸上方、所有普通窗口之下（`HWND_BOTTOM` + 拦截 `WM_WINDOWPOSCHANGING`），
  点它、显示它都不会把自己抬起来。四个入口同步：托盘勾选项 `卡片全局置顶`、
  卡片右键 `全局置顶显示`、设置页「卡片全局置顶」复选框、`--card-top / --card-desktop / --card-layer`。
  选择写入 `settings.json` 的 `alwaysOnTop`，重启后恢复。
- `--dump` 的 `card` 增加 `desktopLayer` / `topMost` / `zOrder`，方便排查"到底置没置顶"。
- 日历增加诊断日志：`calendar opened/select/month/today`，卡片双击日期也记一条。

### 修复
- Windows 偶尔会在"恢复一大批窗口"后静默忽略第一次 `HWND_TOPMOST`；`applyZOrder()` 现在会
  校验 `WS_EX_TOPMOST`，未生效则在下一轮事件循环重试一次并记日志（回归里曾出现过一次 `native topmost=0`）。
- 卡片不再通过 Qt 的 `setWindowFlag(WindowStaysOnTopHint)` 切层级（那会重建 HWND，菜单/焦点状态丢失），
  改为构造时不设该 flag、由 `applyZOrder()` 统一用 Win32 断言。

### 工程
- 交互回归扩到 **37 项**：新增卡片层级、桌面层真实点击、桌面层右键菜单、从桌面层双击日期开日历、
  日历看板点击选中某天、切换回置顶、显示/隐藏、以及"重启后层级恢复"。
  脚本会临时最小化挡住卡片的窗口再恢复（桌面层本来就会被普通窗口盖住）。
- 回归脚本不再最小化其它应用的窗口：旧做法会让部分 GPU 应用（Widgets / Radeon Software / Chromium）在恢复后留下黑块或残影，看着像整屏"重影"。桌面层的点击与双击改为 `PostMessage` 直接发给卡片 HWND，真实鼠标点击与右键菜单在置顶模式验证。
- 回归脚本保持**纯 ASCII**：PowerShell 5.1 按 ANSI 读取 `.ps1`，中文窗口标题字面量会被写成乱码，
  日历标题改用码点拼出来。

## [qt-0.2.0] - 2026-11-08

### 新增
- **截图编辑器补齐**：窗口检测/吸附（hover 高亮 + 单击选整窗）、马赛克、模糊、橡皮、八个控制点缩放、方向键微调（`Shift` 外扩 / `Alt` 收窄）、选中标注后移动/改色/删除。
- **输出链路**：另存为、快速保存（`Ctrl+Shift+S`）、自动保存（目录 + 命名规则）、贴到桌面、OCR 建任务、打印（`Qt6::PrintSupport`）。
- **贴图窗口**：缩放、透明度、点击穿透、分组、显示/隐藏全部、旋转翻转、1px 微调、剪贴板替换、文字贴图。
- **截屏历史**：每次输出自动留底、按条数滚动清理、缩略图浏览器（贴到桌面/复制/打印/删除/打开目录）。
- **日历**：月历（周一为首列）、每日待办/完成/顺延计数、当日清单、换月与回到今天。
- **设置窗口**：卡片 / 热键 / 截图与贴图 / AI / 其它 五页；热键录制控件；保存后即时生效。
- **中文时间解析**：`明天 15:00`、`周五下午两点`、`#标签`、`！！` → 标题 / 到期 / 优先级。
- **白板与透明白板**、**取色器**。
- **OCR**：`Windows.Media.Ocr`（`zh-Hans-CN` 可用），异步子进程，结果自动变任务。
- **AI 归纳**（可选）：OpenAI 兼容接口把 OCR 长文压成一句话标题，失败自动回落。
- **CLI 扩到 15 个动词**：`full / last / pin / pins / color / board / board-transparent / history / calendar / settings` 等。
- **配置 v2**：`hotkeys` 映射表（旧三个扁平字段自动迁移）、`lastRegion`、自动保存、贴图分组、AI、自启；原子写 + `.broken` 备份。
- **自检 53 项** + **真机交互回归脚本 25 项**（`qt/TaskPinQt/scripts/test-snip.ps1`，含截图取证）。

### 修复
- `--dump` 不再新起一个实例抢走 `TaskPinQt.Cmd` 命名管道（此前会让之后的 `--snip` 全部失效），改为向常驻实例请求快照。
- 截图内联文字框：`Esc` 现在取消本次输入而不是没反应；失焦自动提交。
- WinRT OCR 只认反斜杠路径，统一用 `QDir::toNativeSeparators` 后再传给 PowerShell。
- 日历星期表头不再被分隔线压住。

### 工程
- `Annotation` 从 `SnipOverlay` 拆出（`ui/Annotation.*`），工具栏仍留在浮层内（下一步再拆）。
- 新增 `platform/WindowPicker`、`WindowFx`、`HotkeyText`、`HotkeyEdit`、`Autostart`、`core/TimeParser`、`FileNaming`、`History`、`Ocr`、`AiClient`。
- **仓库合并**：Qt 主线并入 `TaskPin` 仓库根（`TaskPinQt/` + `docs/`），不再需要第二个仓库；
  `docs/screenshots` 存 1280px 存证截图，`docs/reports` 存两份测试报告，`docs/artifacts` 为可重跑的原始输出（gitignore）。
- **删除 C# 版**：源码 / 构建产物 / 安装脚本 / 本机数据全部移除，历史与旧数据先归档到
  `D:\TaskPin\backup\`（`repo-csharp-history.bundle`、`csharp-data/`）。

## [0.3.4] - 2026-11-08

### 修复
- **崩溃**：连续右键卡片/贴图会递归弹出菜单，最终栈溢出（`0xc00000fd` in USER32）。现在弹出前加了重入锁，同一时刻只允许一个菜单。
- **双击日期不灵**：之前判断写的是 `WM_LBUTTONDOWN` + `WParam != 0`，等于单击就触发。改为处理 `WM_LBUTTONDBLCLK`，真正的双击才开日历。
- **AI 队列 SQL 报错**：`SelectAll` 与追加条件之间缺分隔，拼成 `FROM taskWHERE …`，导致每 40 秒报一次 `near "=": syntax error`。已修正。
- 贴图右键菜单同样加了重入锁。

### 新增
- **回收站**：删除任务只做软删除（`state='dropped'`），数据仍在 SQLite 里；右键卡片 →「回收站…」可查看、恢复或彻底删除。列表不进入日历与今日任务。
- 卡片右键菜单项「回收站…」。

## [0.3.3] - 2026-11-08

### 修复
- 卡片 / 贴图右键菜单改为 **Win32 原生 `TrackPopupMenuEx`**：托管的 `ContextMenuStrip` 挂在不抢焦点的窗口上会「画得出来但点不动」。
- 日历改由隐藏的可激活宿主窗口持有，并用 `AttachThreadInput` 强夺前台，解决「要先点任务栏才能操作」。

## [0.3.2] - 2026-11-08

### 修复
- **工具按钮错位**：`SetTool((ToolKind)(i + 1))` 让整排按钮右移一位（点矩形画出圆），改为 `(ToolKind)i`。
- 卡片左上角右键无效：分层窗口 alpha=0 的像素不接收鼠标，画布底色改为 alpha=1；并接管 `WM_NCRBUTTONUP`/`WM_CONTEXTMENU` 防止弹出系统菜单。
- 日历改为始终置顶。

## [0.3.1] - 2026-11-08

### 新增
- 截图工具栏改为**矢量图标**两行布局，悬停显示「名称 + 快捷键」。
- 卡片外观：背景色 / 文字色可在设置中修改（默认仍是深色简洁风）。

### 修复
- 文字标注：失焦即提交、`Shift+Enter` 换行、双击已有文字可再次编辑。

## [0.3.0] - 2026-11-08

### 新增
- 离线二维码 / 条码扫描（ZXing.Net 0.16.9），`--scan`、托盘、截图编辑器 `Ctrl+Q`。
- 功能矩阵校准（61 项，48 完成 / 7 部分 / 4 明确不做），新增 `docs/ACCEPTANCE.md`。

## [0.2.9] - 2026-11-08

### 新增
- 白板模式（冻结画面手写）、截屏历史打印、`--selftest` 增加逻辑断言。

## [0.2.8] - 2026-11-08

### 新增
- 月度任务日历（待办 / 完成 / 顺延计数 + 当日清单）。
- 日历 / 设置 / 历史窗口统一 DWM 深色标题栏。

## [0.2.7] - 2026-11-08

### 新增
- 剪贴板图片监听：可自动识别成任务（去抖 + 自噬抑制）。
- `Ctrl+Shift+S` 快速保存到自动保存目录。
- `settings.json` 损坏时备份为 `.broken`，不再静默覆盖。

## [0.2.6] - 2026-11-08

### 新增
- 自动保存 + 命名规则（`{yyyy}{MM}{dd}-{HH}{mm}{ss}` 等变量）。
- 延迟截图（`--delay N`、托盘、设置项）。
- 选区方向键 1px 微调。

## [0.2.5] - 2026-11-08

### 新增
- 五页设置对话框：卡片 / 热键 / 贴图与标注 / AI / 其它。
- 热键录制器，保存即生效，被占用自动降级。

## [0.2.4] - 2026-11-08

### 新增
- 命名管道 CLI：`--snip --full --last --paste --paste-text --paste-file --toggle-images --pick-color --history --show --quit`。
- 屏幕取色器（10× 放大，复制 HEX）。
- 文字 / 图片文件 → 贴图。

## [0.2.1] - 2026-11-08

### 新增
- 贴图能力：不透明度、点击穿透、分组 + 闪烁、缩放档位、旋转翻转、1px 微调、剪贴板替换。
- 截屏历史、上一次区域复用、全部贴图开关。

## [0.2.0] - 2026-11-08

### 新增
- 截图编辑器：放大镜、窗口检测与吸附、11 种标注工具、撤销恢复。
- 贴图窗口、离线 OCR（Windows.Media.Ocr）、任务卡片。

## [0.1.0] - 2026-11-07

### 新增
- 项目起步：SQLite 任务库、桌面卡片、快捷键注册、托盘。
- 用 Qt `.qm` 反查工具提取 Snipaste 2738 条 UI 文案，形成功能矩阵。
