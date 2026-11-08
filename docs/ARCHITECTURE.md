# TaskPin 架构说明

> 本文是**架构的唯一事实源**。代码与本文不一致时，以本文为准并修代码；架构变更必须先改本文。
> 最后更新：2026-11-08（单一 Qt 实现线 0.2；C# 版已归档删除，仅差扫码）

---

## 1. 一条实现线（C# 版已归档）

| 实现线 | 位置 | 语言 / 框架 | 状态 | 定位 |
|---|---|---|---|---|
| **Mainline（唯一）** | `D:\TaskPin\repo\TaskPinQt\` + `%LOCALAPPDATA%\TaskPin\TaskPinQt\` | C++17 / Qt 6.4.3 / MinGW 11.2 | 0.2，功能对齐（仅差扫码） | 所有开发都在这里 |
| ~~Legacy~~ | 已删除；历史在 `D:\TaskPin\backup\repo-csharp-history.bundle` | C# / .NET 9 / WinForms | 0.3.4（归档） | 只做考古，不再参与构建 |

2026-11-08 起仓库内只保留 Qt 实现线。C# 版源码、构建产物、安装脚本与本机数据均已删除，
其 27 个提交压成 bundle 存于 `backup\`（`git clone <bundle>` 可恢复）。

运行期数据目录（只剩一个）：

```
%LOCALAPPDATA%\TaskPin\TaskPinQt\   ← taskpin.db / settings.json / taskpinqt.log / history\snip\ / ocr\
```

> 注意：Qt 的数据目录仍嵌在 `%LOCALAPPDATA%\TaskPin\` 之下（`QStandardPaths::AppLocalDataLocation`
> 的 org/app 组合决定）。C# 的兄弟文件已删，目录名保留以免迁移现有数据。

---

## 2. 总体架构（Qt 主线）

```
┌──────────────────────────────────────────────────────────────────────┐
│                         TaskPinQt.exe（单进程）                        │
│                                                                      │
│  ┌────────────── app 层（装配 / 生命周期） ──────────────┐             │
│  │  main.cpp       单实例 · CLI 转发 · 日志 · 区域设置     │             │
│  │  AppHost        托盘 · 热键 · 命令通道 · 窗口/OCR/AI 编排│            │
│  └───────────────┬───────────────────────┬──────────────┘             │
│                  │                       │                            │
│  ┌───────────────▼──────── ui 层 ────────▼──────────────┐             │
│  │  CardWindow        桌面卡片                            │             │
│  │  SnipOverlay       截图浮层（冻结画面 + 12 工具 + 工具栏）│           │
│  │  Annotation        标注模型（绘制 / 命中 / 马赛克取样）  │             │
│  │  PinWindow         贴图窗口 + 分组                     │             │
│  │  HistoryDialog     截屏历史浏览器                      │             │
│  │  CalendarWindow    月历                                │             │
│  │  SettingsDialog    五页设置（含热键录制）               │             │
│  │  ColorPickerOverlay取色器                              │             │
│  │  WhiteboardWindow  白板 / 透明白板                     │             │
│  │  QuickAddDialog    速记输入                            │             │
│  └───────────────┬──────────────────────────────────────┘             │
│                  │ 只调用 core / platform，绝不反向                    │
│  ┌───────────────▼──────── core 层 ─────────────────────┐             │
│  │  Store       任务存储（SQLite，CRUD + 按天聚合 + 信号）│             │
│  │  Task        实体（POD）                               │             │
│  │  Settings    JSON 配置（v2，热键表，原子写，.broken）  │             │
│  │  TimeParser  中文时间解析（标题/到期/优先级/标签）      │             │
│  │  FileNaming  文件名规则                                │             │
│  │  History     截屏历史目录 + 滚动保留                   │             │
│  │  Ocr         Windows.Media.Ocr 子进程封装              │             │
│  │  AiClient    OpenAI 兼容归纳（QNetworkAccessManager）  │             │
│  └───────────────┬──────────────────────────────────────┘             │
│                  │                                                    │
│  ┌───────────────▼────── platform 层（Win32 边界） ─────┐              │
│  │  Hotkeys        RegisterHotKey + 隐藏消息窗口         │              │
│  │  HotkeyText     组合键文本 ↔ 修饰键/虚拟键            │              │
│  │  HotkeyEdit     热键录制控件（QLineEdit 子类）        │              │
│  │  ScreenCapture  多屏冻结画面（DPR 还原）              │              │
│  │  WindowPicker   EnumWindows 命中光标下窗口            │              │
│  │  WindowFx       点击穿透 / 不抢焦点 / 置顶 / 抢前台    │              │
│  │  Autostart      HKCU Run 开机自启                     │              │
│  └──────────────────────────────────────────────────────┘             │
└──────────────────────────────────────────────────────────────────────┘
```

**依赖方向（硬约束）**：`ui → core → platform`，`app` 可以引用全部。
禁止 `core` 引用 `ui`；禁止 `platform` 引用 `ui`；禁止在 `Store` 里出现窗口代码。
（`platform/HotkeyText` 被 `ui/SettingsDialog` 直接使用是允许的——platform 是最底层。）

---

## 3. 线程与消息模型

- **单 UI 线程**：Qt 主线程负责所有窗口、绘制、热键回调、命令通道。
- **异步来源**必须离开 UI 线程，结果用信号回主线程：
  - `Ocr`：`QProcess` 跑 PowerShell 子进程（WinRT OCR），`finished/failed` 信号；
  - `AiClient`：`QNetworkAccessManager` + 25s 超时定时器，`finished/failed` 信号。
- **热键**：注册在隐藏消息窗口上，`WM_HOTKEY` → `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` → 主线程发出 `triggered(id)`。
- **命令通道**：`QLocalServer("TaskPinQt.Cmd")` 每条连接一个短命令，处理完即断开；
  `dump <path>` 例外——它要求常驻实例把快照写到指定路径并回 `ok`。
- **超时预算**：OCR 20s、AI 25s、命令连接 500ms。UI 线程里不允许出现 >50ms 的阻塞调用。

---

## 4. 数据模型

### 4.1 表结构

```sql
task(
  id TEXT PK, title, note, ocr_text, source, tags, due,
  priority INT (0..3), sort_key INT, state TEXT ('open'|'done'|'dropped'),
  created, updated, completed, origin_date, last_day,
  carry_count INT, attachment, ai_state, ai_summary, ai_attempts, ai_next_try
)

task_day(task_id, day 'yyyy-MM-dd', state ('open'|'done'|'carried'|'dropped'), seen_at,
         PRIMARY KEY(task_id, day))
```

### 4.2 语义

| 概念 | 实现 |
|---|---|
| **今日任务** | `state='open'`，按 `priority, COALESCE(due,'9999'), sort_key` 排序 |
| **顺延** | 跨天时 `last_day` 落后于今天 → `carry_count+1`，并写一条 `task_day(state='carried')` |
| **完成** | `state='done'` + `completed` 时间戳；`task_day` 记 `done` |
| **删除** | **软删除**：`state='dropped'`（数据永不丢，回收站可恢复） |
| **彻底删除** | 仅回收站里显式操作，删 `task` + `task_day` |
| **日历** | 只读 `task_day`：`dayStats(from,to)` 按天聚合 open/done/carried；`dayTasks(day)` 取当日清单 |
| **优先级** | 默认按写入顺序（`sort_key` 自增）；手动改 `priority` 立即影响排序 |
| **OCR 建任务** | `source='ocr'`，`ocr_text` 存原文，`title/due/priority` 由 `TimeParser` 从原文解析 |

### 4.3 事务与通知

`Store` 每个写操作：改表 → 写 `task_day` → `emit changed()`。
UI 只监听 `changed()` 刷新，**不轮询**。所有写操作在 `Store` 内部完成，UI 不拼 SQL。

---

## 5. 关键流程

### 5.1 截图 → 标注 → 输出

```
热键 / CLI / 托盘
   └→ AppHost::startSnip()
        ├─ 延迟（设置 snipDelay）→ QTimer::singleShot
        ├─ hideOverlaysForCapture()      // 卡片 + 贴图先隐藏，避免拍进画面
        ├─ captureVirtualDesktop()       // 每屏 grabWindow + 按 DPR 合成
        ├─ new SnipOverlay(shot)         // 全屏无边框窗口，缓存设备像素底图 m_base
        │    ├─ 拖拽 → m_sel；hover → WindowPicker → 单击吸附整窗
        │    ├─ 工具 R/O/A/L/P/H/T/N/M/B/E → Annotation
        │    └─ 绘制：冻结图 → 选区外遮罩 → 标注 → 工具栏 → 放大镜
        └─ Enter / 工具栏
             ├─ compose()   // 按 DPR 裁剪 + scale(dpr) 画标注 → QImage
             │    └─ Pixelate 从 m_base 取 rect*dpr 区域，缩放降采样再放大
             ├─ captured(img, region) → AppHost::recordCapture（历史 + 自动保存 + 记住选区）
             └─ copied / saved / pinned / recognizeRequested / printed
```

要点：
- 冻结画面是**设备像素**位图，`devicePixelRatio` 设为 1.25，绘制时 Qt 自动缩放到逻辑坐标。
- 裁剪必须乘 DPR：`src = sel * dpr`，否则输出会小一圈（实测 371×131 物理 ↔ 297×105 逻辑）。
- 标注坐标全部是**逻辑坐标**，`compose()` 里 `scale(dpr)` 后再画。
- 选区持久化到 `settings.lastRegion`（`"x,y,w,h"`，逻辑坐标），`--last` 用它做预选。

### 5.2 贴图（Pin）

```
captured/pinFromClipboard
   └→ new PinWindow(image)
        ├─ 窗口：Qt::Tool | Frameless | StaysOnTop | WA_TranslucentBackground
        ├─ 绘制：双层阴影 + 图像（按 zoom 缩放）+ 1px 边框
        ├─ 交互：拖动 / 滚轮缩放 / Ctrl+滚轮透明度 / 双击关闭 / 方向键微调
        ├─ 右键菜单：识别、复制、保存、替换、透明度、缩放档位、旋转翻转、点击穿透、分组、关闭
        └─ 静态注册表：groups / currentGroup / toggleAll / closeAll / clearClickThrough
```

点击穿透 = `WS_EX_TRANSPARENT`（`platform/WindowFx`）。穿透后右键失效，所以托盘保留
「取消全部贴图点击穿透」。

### 5.3 OCR → 任务

```
Ctrl+R / 贴图右键「识别为任务」
   └→ AppHost::recognizeImage(img)
        └→ Ocr::recognize(img)
             ├─ 写 ocr-helper.ps1（纯 ASCII）+ ocr/input.png（路径转反斜杠）
             ├─ QProcess: powershell -File helper -Image ... -Out result.json
             └─ finished(text) / failed(reason)
                  └→ AppHost::onOcrFinished
                       ├─ TimeParser::parse(text) → 本地标题 + 到期 + 优先级
                       ├─ AI 开启 → AiClient::summarize(text) → 用模型标题（失败回落本地）
                       └─ Store::addTask(title, "ocr", ..., due, priority, ocrText)
```

### 5.4 白板 / 透明白板

```
--board / --board2 / 托盘
   └→ AppHost::captureWhiteboard(transparent)
        ├─ 冻结画面（W1 作为底图；W2 不使用底图）
        ├─ WhiteboardWindow：Pen 标注 + 单行工具栏（色板/线宽/撤销/清空/复制/保存/打印/关闭）
        ├─ W2 关键点：背景填 alpha=1（不是 0），否则分层窗口的透明像素收不到鼠标
        └─ compose()：W1 = 底图 + 笔迹；W2 = 只有笔迹（透明 PNG）
```

### 5.5 截屏历史

`recordCapture()` 每次成功输出都写 `history/snip/snip-yyyyMMdd-HHmmss-zzz.png`，
超过 `settings.historyLimit`（默认 50）删最旧；`HistoryDialog` 只读该目录做缩略图。

### 5.6 任务生命周期

```
新建（Ctrl+Alt+Space / 卡片 + / CLI / OCR）
  → Store.addTask()  (state=open, origin_date=last_day=today, task_day=open)
跨天启动
  → Store.rollover()  (last_day<today 的 open 任务 carry_count+1, task_day=carried)
完成 → setDone(true)  → state=done, task_day=done
删除 → drop()         → state=dropped, task_day=dropped
恢复 → restore()      → state=open
```

---

## 6. 窗口与渲染

| 窗口 | 技术要点 |
|---|---|
| **卡片** | `Qt::Tool︱Frameless︱StaysOnTop` + `WA_TranslucentBackground` + `WA_ShowWithoutActivating`；`paintEvent` 自绘圆角、渐变、行、计数；拖动后把坐标写回 `settings.cardX/Y` |
| **截图浮层** | 全屏 `Frameless︱StaysOnTop`，`paintEvent` 先 `drawPixmap` 冻结图，再画遮罩/标注/工具栏；工具栏图标用 QPainter 矢量绘制 |
| **贴图** | `Qt::Tool` + `WA_TranslucentBackground`；`windowOpacity` 做不透明度；穿透用 `WS_EX_TRANSPARENT` |
| **透明白板** | 同上，但**背景必须铺 alpha=1**，否则整块区域点不到 |
| **日历 / 历史 / 设置** | 原生标题栏窗口 + `WindowStaysOnTopHint`；卡片是 `WS_EX_NOACTIVATE`，打开它们后要 `WindowFx::forceForeground()` 抢一次前台 |
| **托盘** | `QSystemTrayIcon` + `QMenu`（Qt 原生菜单，避免 C# 版「能画不能点」的坑） |

**踩过的坑（务必保留）**：
1. 卡片/贴图若用 `WS_EX_NOACTIVATE`，托管菜单能显示但收不到输入 → 必须用原生菜单或在可激活宿主窗口上弹。
2. 分层窗口 alpha=0 的像素不接收鼠标 → 需要点击的地方铺 alpha=1 底色。
3. 重复右键会嵌套模态菜单循环 → 必须加重入锁，否则栈溢出（C# 版实测 `0xc00000fd`）。
4. 托盘/菜单/对话框尽量用系统原生实现，别在透明窗口上自绘交互控件。
5. 截图前要隐藏卡片和贴图，并把它们记下来，截图结束后恢复。

---

## 7. 跨进程接口

| 实现 | 协议 | 命令 |
|---|---|---|
| Qt（当前） | `QLocalServer "TaskPinQt.Cmd"` | `snip / full / last / pin / pins / color / board / board-transparent / quick / toggle / recycle / history / calendar / settings / quit / dump <path>` |
| C#（已归档） | 命名管道 `TaskPin.Cmd` | `snip / full / last / paste / paste-text / paste-file / toggle-images / pick-color / history / calendar / show / quit` |

Qt 版入口：`TaskPinQt.exe --snip`（参数去掉前导 `--` 后原样发给常驻实例；没有实例时由新实例执行）。
该通道同时是**自动化测试入口**——比模拟全局热键可靠得多。

**注意**：`--dump` 必须先尝试连常驻实例；如果直接 `start()` 一个新实例，`QLocalServer::removeServer()`
会把常驻实例的管道删掉，之后所有命令都会失败（已在 0.2 修复）。

---

## 8. 配置

`settings.json`（Qt 版 v2；v1 的 `snipHotkey/quickHotkey/toggleHotkey` 会在读取时迁移到 `hotkeys`）：

```json
{
  "version": 2,
  "cardPosition": "TopRight",       // TopLeft / BottomRight / BottomLeft
  "cardWidth": 340, "visibleRows": 6,
  "opacity": 0.94, "alwaysOnTop": true, "showCarryMark": true,
  "cardBg": "#12171f", "cardFg": "#e8ecf2",
  "cardX": -1, "cardY": -1,         // 拖动后的位置；-1 = 自动摆放

  "snipDelay": 0, "annotationColor": "#e53b3b", "annotationWidth": 3,
  "autoSave": false, "autoSaveDir": "", "namePattern": "TaskPin-{yyyy}{MM}{dd}-{HH}{mm}{ss}",
  "historyLimit": 50, "lastRegion": "104,104,297,105",

  "pinOpacity": 1.0, "pinGroups": ["默认"], "pinGroup": "默认",

  "aiEnabled": false, "aiBaseUrl": "https://api.deepseek.com/v1",
  "aiApiKey": "", "aiModel": "deepseek-chat",

  "autostart": false,

  "hotkeys": {
    "snip": "Ctrl+Alt+A", "full": "Ctrl+Alt+F", "last": "Ctrl+Alt+R",
    "pin": "Ctrl+Alt+V", "pins": "Ctrl+Alt+H", "quick": "Ctrl+Alt+Space",
    "toggle": "Ctrl+Alt+D", "history": "Ctrl+Alt+G"
  }
}
```

读取策略（已实现）：JSON 解析失败 → 备份 `settings.json.broken` → 用默认值启动，**绝不静默覆盖**。
写入策略：`QSaveFile` 原子替换，写一半崩溃不会留下半截文件。
热键被占用：注册失败 → 尝试 `fallbackChord()` 备用键 → 日志写 `[degraded]`，dump 里是实际生效的 chord。

---

## 9. 构建与发布

### Qt 主线（唯一构建入口）

```
Qt        6.4.3 mingw_64   D:\AutoEx\Qt\Qt_6.4.3\6.4.3\mingw_64
编译器     MinGW 11.2.0    D:\AutoEx\Qt\Qt_6.4.3\Tools\mingw1120_64
CMake      3.24.2          D:\AutoEx\Qt\Qt_6.4.3\Tools\CMake_64
Ninja      1.10.2          D:\AutoEx\Qt\Qt_6.4.3\Tools\Ninja
模块       Qt6 Core/Gui/Widgets/Sql/Network/PrintSupport
```

```powershell
D:\TaskPin\repo\TaskPinQt\build.cmd   # CMake + Ninja → TaskPinQt\build\TaskPinQt.exe
```

`CMakeLists.txt` 里已挂 `windeployqt` 后置步骤，产物目录自带 Qt 运行库，可直接压缩分发。
`build/` 已被 git 忽略；移动源码目录后要先删掉旧的 `build\`（CMake 缓存里有绝对路径）。

> C# 版的 `build.cmd` / `install.cmd` 已随源码删除；需要时从
> `D:\TaskPin\backup\repo-csharp-history.bundle` 里检出。

---

## 10. 测试策略

| 层次 | 手段 | 现状 |
|---|---|---|
| 单元 | `--selftest`：Store CRUD/聚合、DPR 换算、配置默认与 `.broken`、文件名、中文时间解析、热键文本、标注（马赛克取样/命中/移动）、历史 | ✅ 53 项 |
| 交互 | `scripts/test-snip.ps1`：模拟鼠标键盘跑真 UI，断言剪贴板尺寸 / 任务数 / 卡片几何，截图存档 | ✅ 26 项 |
| 端到端 | CLI 触发 → 剪贴板/文件校验；OCR → 任务 → 卡片 | ✅ |
| 回归 | 上面两个脚本每次改动后都跑 | ✅ 已落盘 |

**测试坐标必须考虑 DPI**：截图进程需 `SetProcessDpiAwarenessContext(-4)`，否则拿到的窗口坐标是 0.8 倍虚拟值（已踩坑两次）。
**合成按键会被输入法拦截**：需要输入中文时用剪贴板粘贴，不要用 `SendKeys` 逐字打。

---

## 11. 迁移状态矩阵

> C# 列是**历史对照**：源码已删除（bundle 在 `backup\`），保留这张表是为了勾掉尚未搬过来的能力。

| 模块 | C#（已归档） | Qt（当前） |
|---|---|---|
| 单实例 / 托盘 / 热键 | ✅ | ✅（8 动作 + 降级 + 录制） |
| 任务库 / 顺延 / 优先级 / 软删除 / 回收站 | ✅ | ✅ |
| 桌面卡片 | ✅ | ✅（含双击日期、位置记忆） |
| 截图浮层 + 标注 | ✅ 11 工具 | ✅ 12 工具（含马赛克/模糊/橡皮 + 窗口吸附 + 微调） |
| 截图输出（复制/保存/贴图/识别/打印/自动保存） | ✅ | ✅ |
| 命令行/IPC | ✅ 12 动词 | ✅ 15 动词 |
| 贴图窗口 | ✅ | ✅（缩放/透明/穿透/分组/翻转/文字贴图） |
| 截屏历史 / 取色器 | ✅ | ✅ |
| 日历 / 白板 / 透明白板 | ✅ | ✅ |
| OCR / AI | ✅ | ✅（OCR 走系统 WinRT，AI 走 OpenAI 兼容） |
| 二维码 / 条码 | ✅ | ⏳（缺 zxing-cpp） |
| 设置窗口 / 热键录制 | ✅ | ✅ |
| 剪切板监听自动建任务 | ✅ | ⏳（可选功能，未做） |

---

## 12. 技术债与风险

| 项 | 影响 | 计划 |
|---|---|---|
| 全局热键在提权前台窗口下不触发 | 用户可能以为坏了 | 改 `WH_KEYBOARD_LL` 低级钩子，或提示以管理员运行 |
| 旧 C# 进程可能残留并占用热键 | 新版抢不到键 | 启动时检测同名进程并提示（目前靠备用键兜底） |
| 各屏 DPR 不同时按主屏缩放 | 副屏截图偏大/偏小 | `captureVirtualDesktop` 改为逐屏裁剪 |
| 扫码未做 | 功能缺口 | vendored zxing-cpp；当前工具链无法本地编译，故不做半成品 |
| 单文件偏大（`SnipOverlay` ~900 行） | 维护成本 | 已拆 `Annotation`；下一步拆 `SnipToolbar` |
| OCR 依赖语言包 | 无中文包即失败 | 失败信息已带原因，设置页可提示装包 |
| 打印未真机验收 | 边距可能不同 | 有打印机时补一次 |
| C# 历史只存在于 bundle | 查阅要 `git clone` | 需要时恢复；确认无用可删 `backup\` |
