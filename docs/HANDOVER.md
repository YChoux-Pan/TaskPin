# TaskPin 项目交接文档（Handover）

> **给接手的人 / 新 agent**：先读完第 0 章和第 6 章就能开始干活，其余章节按需查阅。
> 本文是需求与工程的**唯一事实源**；架构细节见 `docs/ARCHITECTURE.md`，版本记录见 `CHANGELOG.md`。
> 最后更新：2026-11-08 · 单一 Qt 实现线 0.2（C# 版已归档删除，仅差扫码）

---

## 0. 一页速览

### 这是什么

TaskPin 是一个 Windows 桌面常驻工具，把两件事合成一个软件：

1. **截图 / 贴图工具**（Snipaste 类）：热键截图 → 标注 → 复制 / 保存 / 贴到屏幕上；
2. **桌面任务卡片**：右上角一张半透明卡片显示今天的任务，支持顺延、优先级、OCR 把截图变成任务、日历回看。

### 当前状态（一句话）

**仓库里现在只有一条实现线：C++/Qt。** 早期 C# 版（0.3.4）已从其仓库和本机删除，只保留一份 git bundle 在 `D:\TaskPin\backup\`（见 §0 版本控制）。Qt 0.2 已补齐「截图（窗口吸附 / 马赛克 / 模糊 / 橡皮 / 微调）+ 输出（复制 / 保存 / 快速保存 / 自动保存 / 贴图 / OCR / 打印）+ 贴图窗口 + 截屏历史 + 日历 + 设置窗口（热键录制）+ 中文时间解析 + 白板 + 透明白板 + 取色器 + **卡片全局置顶 ↔ 桌面层切换** + CLI」，**只剩二维码 / 条码（R2）未实现**。

### 三条命令

```powershell
# 构建（Qt 主线；0 error 0 warning）
cmd /c D:\TaskPin\repo\TaskPinQt\build.cmd

# 运行
D:\TaskPin\repo\TaskPinQt\build\TaskPinQt.exe

# 触发截图（自动化测试入口，比模拟全局热键可靠）
D:\TaskPin\repo\TaskPinQt\build\TaskPinQt.exe --snip
```

### 两条验证命令

```powershell
# 无界面自检：53 项断言，退出码 0=全过
Start-Process -Wait -PassThru D:\TaskPin\repo\TaskPinQt\build\TaskPinQt.exe `
  -ArgumentList '--selftest','D:\TaskPin\repo\docs\artifacts\selftest'

# 真机交互回归：38 项断言（模拟鼠标键盘，截图存 docs/）
powershell -NoProfile -ExecutionPolicy Bypass -File D:\TaskPin\repo\TaskPinQt\scripts\test-snip.ps1
```

### 关键路径

| 用途 | 路径 |
|---|---|
| 源码 | `D:\TaskPin\repo\TaskPinQt\src\` |
| 构建产物 | `D:\TaskPin\repo\TaskPinQt\build\TaskPinQt.exe` |
| 测试脚本 | `D:\TaskPin\repo\TaskPinQt\scripts\`（`test-snip.ps1` / `show-image.ps1` / `fixtures\`） |
| 文档 | `D:\TaskPin\repo\docs\`（`HANDOVER.md` / `ARCHITECTURE.md` / `screenshots\` / `reports\` / `legacy\`） |
| 回归原始输出 | `D:\TaskPin\repo\docs\artifacts\`（gitignore，可重跑生成） |
| 数据目录 | `%LOCALAPPDATA%\TaskPin\TaskPinQt\`（`taskpin.db` / `settings.json` / `taskpinqt.log` / `history\snip\` / `ocr\`） |
| 运行日志 | `%LOCALAPPDATA%\TaskPin\TaskPinQt\taskpinqt.log` |
| C# 旧版归档（git bundle） | `D:\TaskPin\backup\repo-csharp-history.bundle` |
| C# 旧数据归档 | `D:\TaskPin\backup\csharp-data\` |

### 版本控制

| 位置 | 说明 |
|---|---|
| `D:\TaskPin\repo` | **唯一仓库**：Qt 源码 + 文档 + 存证截图；`origin` → `https://github.com/YChoux-Pan/TaskPin.git` |
| `D:\TaskPin\backup` | 不在 git 里：C# 版历史 bundle + 旧数据归档（确认不再需要可整个删除） |

> 2026-11-08：C# 版（源码 / 构建产物 / 安装包 / 安装脚本 / 本机数据）已按要求删除；
> 其 27 个提交的历史与旧数据先压成 bundle 归档在 `backup\`，需要考古时 `git clone` 该 bundle 即可。

### 绝对不要做的事

1. **不要试图从 C# 版抄实现**：C# 源码已删除，`docs\legacy\` 只剩功能矩阵等文档；实现以本仓库 Qt 代码为准。
2. **不要用 PowerShell 内联字符串写 C++ 代码**：历史上有 6 次以上因引号转义导致编译失败。一律「写文件 → 用编辑工具改 → 编译」。
3. **不要在没有 DPI 感知的情况下截图取值**：否则坐标是 0.8 倍虚拟值，会得出错误结论。测试脚本里 `[U]::Aware()` 就是这个作用。
4. **不要在 `WS_EX_NOACTIVATE` 窗口上弹托管菜单**：会「能显示但点不动」，必须用原生菜单。
5. **不要把 Qt 版的功能说成"完成"而没有截图或日志证据**。
6. **不要在 GUI 线程里同步等 OCR / AI**：OCR 走 `QProcess`（PowerShell 子进程），AI 走 `QNetworkAccessManager`，结果用信号回主线程。
7. **不要把 `--dump` 做成"再启动一个实例"**：第二个实例会抢走 `TaskPinQt.Cmd` 命名管道，导致后续 `--snip` 全部失效。现在 `--dump` 是向常驻实例发 `dump <path>` 命令。

---

## 1. 产品定义

### 1.1 定位

> 一个常驻托盘的小工具：**看到什么就截下来，截下来就能变成任务，任务永远在桌面右上角。**

### 1.2 两类核心场景

- **场景 A（截图流）**：写文档时要引用某个界面 → `Ctrl+Alt+A` → 框选 → 画个箭头 → `Enter` 复制 → 粘贴进文档。
- **场景 B（任务流）**：别人在微信里发来一段安排 → 截图 → `Ctrl+R` 识别 → 自动变成一条任务 → 明天没做完自动顺延，卡片上标 `×2`。

### 1.3 设计原则

| 原则 | 具体含义 |
|---|---|
| **零打扰** | 卡片不抢焦点、不挡鼠标；不弹通知轰炸（提示统一走托盘气泡） |
| **常驻低占用** | 空闲私有内存 < 30 MB，CPU 0% |
| **离线优先** | 截图、标注、OCR 全部本地完成；AI 只是可选增强 |
| **数据不丢** | 删除是软删除；配置解析失败要备份；数据库用 WAL |
| **键盘优先** | 全流程都有热键，鼠标只是补充 |

---

## 2. 功能全量规格

> 每条包含：**行为 / 交互 / 边界 / 验收标准**。`[Qt]` 表示已在 Qt 版实现，`[ ]` 表示待做。

### 2.1 截图

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| C1 | 区域截图 `[Qt]` | 热键后冻结全屏画面，拖拽框选；未框选时全屏加暗；验收：能框出任意矩形且尺寸标签实时更新 |
| C2 | 全屏截图 `[Qt]` | 一键截全屏并复制（`--full` / 托盘）；验收：剪贴板得到 2560×1600 图像 |
| C3 | 上一次区域 `[Qt]` | 复用上次框选矩形直接截（`--last` / 托盘）；验收：第二次热键后自动带出上次选框 |
| C4 | 延迟截图 `[Qt]` | N 秒后开截（设置页 `截图延迟`，默认 0）；验收：设置 3 秒后，3 秒内不出现遮罩 |
| C5 | 冻结画面 `[Qt]` | 截的是「按下热键那一刻」的画面，不是实时；验收：截图里没有截图工具本身 |
| C6 | 多屏与 DPI `[Qt]` | 跨屏合成；125% 缩放下坐标与输出像素正确；验收：371×131 物理选区 → 逻辑 297×105 → 输出 371×131 |
| C7 | 窗口检测 / 吸附 `[Qt]` | 鼠标移动时高亮鼠标下的窗口（含标题），单击直接选中该窗口；验收：hover 测试窗口 → 单击即选中整窗 720×220 |
| C8 | 放大镜 `[Qt]` | 8× 放大光标处像素，显示坐标与 `#RRGGBB` |
| C9 | 选区微调 `[Qt]` | 方向键移动 1px，`Shift` 外扩，`Alt` 收窄；八个控制点可拖拽缩放 |
| C10 | 取消 `[Qt]` | `Esc` 或右键退出，不产生任何输出 |

### 2.2 标注

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| A1 | 矩形 / 椭圆 `[Qt]` | 拖拽绘制，颜色与线宽可调 |
| A2 | 箭头 `[Qt]` | 起点到终点 + 箭头头部，线宽联动 |
| A3 | 直线 `[Qt]` | 拖拽直线 |
| A4 | 画笔 `[Qt]` | 自由曲线 |
| A5 | 荧光笔 `[Qt]` | 半透明粗线，叠加不遮字 |
| A6 | 文字 `[Qt]` | 点击落点 → 内联输入框 → 回车提交；支持中文（IME）；`Esc` 取消本次输入；失焦也提交 |
| A7 | 序号 `[Qt]` | 点击放置 ①②③，自动递增 |
| A8 | 马赛克 / 模糊 `[Qt]` | 拖拽区域像素化；`M` 马赛克、`B` 高斯模糊；滚轮调块大小（4–80px） |
| A9 | 橡皮 `[Qt]` | 擦除已有标注（按命中测试，拖过即删） |
| A10 | 撤销 / 重做 `[Qt]` | `Ctrl+Z` / `Ctrl+Y`，步数不限 |
| A11 | 选中再编辑 `[Qt]` | 选择工具下点选已画标注 → 拖动移动、点色板改色、`Delete` 删除 |
| A12 | 调色板 `[Qt]` | 7 色 + 线宽 ±（滚轮也可调） |
| A13 | 工具栏 `[Qt]` | 两行图标（12 工具 + 色板 + 8 动作），当前工具高亮 |

**编码约定（Qt 版）**：`Annotation` 结构体 + `paint(QPainter&, const QImage *base, qreal dpr)`；坐标一律逻辑坐标；`compose()` 里先 `scale(dpr)` 再画；马赛克/模糊从 `base`（冻结图，设备像素）按 `rect*dpr` 取样。

### 2.3 输出

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| O1 | 复制 `[Qt]` | `Enter` → 剪贴板得到合成图（含标注） |
| O2 | 另存为 `[Qt]` | `Ctrl+S` → 保存对话框 → PNG |
| O3 | 快速保存 `[Qt]` | `Ctrl+Shift+S` 直接存到自动保存目录，文件名按命名规则 |
| O4 | 自动保存 `[Qt]` | 开关 + 目录 + 命名规则（`{yyyy}{MM}{dd}-{HH}{mm}{ss}`），设置页可改 |
| O5 | 贴到桌面 `[Qt]` | `Ctrl+P` → 生成贴图窗口（见 2.4） |
| O6 | 识别文字 `[Qt]` | `Ctrl+R` → 系统 OCR → 建任务（标题 / 截止时间 / 优先级自动解析） |
| O7 | 打印 `[Qt]` | 打印对话框，等比居中（`QtPrintSupport`） |

每次成功输出都会写一份到 `history/snip`（H1）。

### 2.4 贴图（Pin）

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| P1 | 贴图窗口 `[Qt]` | 无边框、置顶、可拖动，双击关闭 |
| P2 | 缩放 `[Qt]` | 滚轮缩放，25%–400% |
| P3 | 不透明度 `[Qt]` | `Ctrl+滚轮` 10%–100%，右键菜单可选 100/90/…/30% |
| P4 | 点击穿透 `[Qt]` | 右键菜单切换；穿透后托盘「取消全部贴图点击穿透」可恢复 |
| P5 | 分组 `[Qt]` | 贴图归属分组，可「切换到该分组」并闪烁定位 |
| P6 | 全部显示 / 隐藏 `[Qt]` | 热键 / 托盘 / `--pins` 一键切换 |
| P7 | 旋转 / 翻转 `[Qt]` | 90° 顺逆时针、水平翻转 |
| P8 | 1px 微调 `[Qt]` | 方向键；`Shift` 10px |
| P9 | 剪贴板替换 `[Qt]` | 右键「以剪贴板内容替换」 |
| P10 | 文字贴图 `[Qt]` | 剪贴板没有图片时，把文字渲染成深色卡片再贴出 |

### 2.5 截屏历史

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| H1 | 自动留底 `[Qt]` | 每次截图 / 白板输出都存一份到 `history/snip` |
| H2 | 滚动保留 `[Qt]` | 超出条数删除最旧（默认 50，设置页可改） |
| H3 | 历史浏览器 `[Qt]` | 缩略图列表；可「贴到桌面 / 复制 / 删除 / 打印 / 打开文件夹」 |

### 2.6 任务卡片

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| T1 | 桌面卡片 `[Qt]` | 右上角、半透明、圆角、置顶但不抢焦点 |
| T2 | 今日列表 `[Qt]` | 默认 6 行，滚轮翻页，显示完成圈、标题、顺延次数 |
| T3 | 新建任务 `[Qt]` | `Ctrl+Alt+Space` 唤出单行输入框，回车入库（支持时间解析） |
| T4 | 完成 / 取消完成 `[Qt]` | 点圆圈切换；右键菜单也可 |
| T5 | 优先级 `[Qt]` | P0–P3，影响排序 |
| T6 | 顺延 `[Qt]` | 跨天未完成自动 `carry_count+1`，卡片显示 `×N` |
| T7 | 删除 / 恢复 `[Qt]` | 删除是软删除；回收站可恢复或彻底删除 |
| T8 | 双击日期开日历 `[Qt]` | 双击卡片右上角日期打开日历 |
| T9 | 外观自定义 `[Qt]` | 背景色 / 文字色 / 不透明度 / 位置 / 行数；拖动位置会被记住 |
| T10 | 中文时间解析 `[Qt]` | 「明天 15:00 前」「周五下午两点」→ 到期时间；`#标签`、`！！` 优先级同样生效 |
| T11 | 卡片层级切换 `[Qt]` | 全局置顶 ↔ 桌面层（壁纸上方、普通窗口会盖住它）。托盘勾选项、卡片右键「全局置顶显示」、设置页复选框、`--card-top / --card-desktop / --card-layer` 四处同步；选择会写进 `settings.json` 并在下次启动恢复 |

### 2.7 日历

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| K1 | 月历 `[Qt]` | 周一为首列，今天高亮，选中日蓝框 |
| K2 | 每日计数 `[Qt]` | 每天显示「N 待办 / N 完成 / N 顺延」 |
| K3 | 当日清单 `[Qt]` | 右侧列该日任务及状态（✓ 完成 / → 顺延），底部总数 |
| K4 | 换月 `[Qt]` | 左右箭头 / `←→`；「今天」按钮回到本月 |

### 2.8 白板

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| W1 | 白板 `[Qt]` | 冻结画面手写，色板 / 线宽 / 撤销 / 清空 / 复制 / 保存 / 打印 |
| W2 | 透明白板 `[Qt]` | 笔迹直接浮在桌面上（分层窗口，背景 alpha=1 而不是 0，否则收不到鼠标） |

### 2.9 识别与智能

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| R1 | 离线 OCR `[Qt]` | 系统 OCR（`Windows.Media.Ocr`），中文可用；截图 → 文字 → 任务标题 + 截止时间 |
| R2 | 二维码 / 条码 `[ ]` | 离线解码（QR/DataMatrix/Aztec/PDF417/Code128/EAN）；**未实现**，理由见 §9 |
| R3 | AI 归纳 `[Qt]` | 可选，OpenAI 兼容接口；把 OCR 长文总结成一句话任务标题（失败自动回落本地标题） |

### 2.10 系统集成

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| S1 | 全局热键 `[Qt]` | 8 个动作都可配置；被占用时降级到备用键并记日志；设置页可直接录制 |
| S2 | 托盘 `[Qt]` | 截图 / 全屏 / 上次区域 / 新建 / 贴图 / 显示贴图 / 取色 / 白板 / 历史 / 日历 / 回收站 / 设置 / 卡片 / **卡片全局置顶（勾选）** / 退出 |
| S3 | 单实例 `[Qt]` | 二次启动只唤出已有实例 |
| S4 | 命令行 `[Qt]` | 15 个动词，见 §6.1 |
| S5 | 开机自启 `[Qt]` | 当前用户 `HKCU\...\Run` 运行项（见 §9 与 C# 计划的差异） |
| S6 | 设置窗口 `[Qt]` | 卡片 / 热键 / 截图与贴图 / AI / 其它 五页，保存后即时生效 |
| S7 | 日志 `[Qt]` | 启动、热键、命令、截图尺寸、OCR、错误都写文件 |

### 2.11 取色器（C# 版有，本文档 §2 早期漏记）

| 编号 | 功能 | 行为与验收 |
|---|---|---|
| C11 | 取色器 `[Qt]` | 全屏冻结 + 10× 放大镜 + `#RRGGBB` / RGB 读数；单击复制色值，`Esc` 取消 |

---

## 3. C++/Qt 迁移思路

### 3.1 概念映射（照这张表翻译，不要逐行照抄）

| C# / WinForms | C++ / Qt | 备注 |
|---|---|---|
| `Form` + `SetStyle(UserPaint)` | `QWidget` + `paintEvent` | 全部自绘 |
| `Graphics` / `GDI+` | `QPainter` | `AntiAliasing` 对应 `SmoothingMode` |
| `Bitmap` | `QPixmap` / `QImage` | 前者用于显示，后者用于像素操作 |
| `NotifyIcon` | `QSystemTrayIcon` | |
| `ContextMenuStrip` | `QMenu`（或原生 `TrackPopupMenuEx`） | 在不可激活窗口上必须用原生 |
| `RegisterHotKey` + `WndProc` | `RegisterHotKey` + 隐藏消息窗口 + `QMetaObject::invokeMethod` | 不要指望 `QAbstractNativeEventFilter` 能收到线程消息 |
| `UpdateLayeredWindow` | `WA_TranslucentBackground` + `WS_EX_TRANSPARENT`（`platform/WindowFx`） | 贴图窗口够用 |
| `Microsoft.Data.Sqlite` | `QtSql` + `QSQLITE` | schema 保持一致 |
| 命名管道 `NamedPipeServerStream` | `QLocalServer` / `QLocalSocket` | |
| `Windows.Media.Ocr` (WinRT) | **PowerShell 子进程调同名 API** | 见 §3.2 决策 4 |
| `ZXing.Net` | zxing-cpp（源码编译） | 离线；**未做** |
| `PrintDocument` | `QPrinter` + `QPrintDialog` | 需要 `Qt6::PrintSupport` |
| `System.Windows.Forms.Timer`（闪烁） | `QTimer` + `setWindowOpacity` | |

### 3.2 四个关键决策与理由

1. **用 Widgets 不用 QML**
   理由：界面全是自绘（卡片、浮层、工具栏），QML 的优势（声明式布局、动画）用不上；
   而 Widgets 更容易精确控制窗口标志（`Qt::Tool`、`WindowStaysOnTopHint`、`WA_TranslucentBackground`），
   也便于直接调 Win32（`winId()`）。

2. **UI 层零阻塞**
   截图、裁剪、标注合成都在同一帧里做完（位图操作，毫秒级）；OCR/AI 必须异步，
   结果用信号回主线程。禁止在 `paintEvent` 里做文件 IO。
   为避开"每帧 `toImage()`"，`SnipOverlay` 在构造时缓存一份设备像素底图（`m_base`）。

3. **Win32 边界集中在 `platform/`**
   所有 `#include <windows.h>` 只出现在 `platform/` 下；`ui/` 与 `core/` 不碰原生 API。

4. **OCR 用子进程而不是 C++/WinRT**
   WinRT 需要 C++/WinRT 生成的头文件与 `/await`，MinGW 工具链（本项目的 11.2.0）不支持。
   因此 OCR 走「把脚本写到数据目录 → `powershell -File` 调 `Windows.Media.Ocr` → 结果写 UTF-8 JSON → Qt 读回」。
   代价是一次约 1 秒的进程启动，完全在 UI 线程之外；换来的是不引入第二套编译器。
   **两个必须记住的坑**：WinRT 只认反斜杠路径；PowerShell 5.1 按 ANSI 读脚本（脚本文件必须纯 ASCII）。

### 3.3 已踩过的坑（每条都对应一次真实故障）

| 现象 | 根因 | 解法 |
|---|---|---|
| 截图后卡片被拍进去 | 截图前没隐藏卡片 | 先 `hide()` + `processEvents()` 再抓屏；贴图也一起隐藏 |
| 输出图比选区小一圈 | 裁剪没乘 `devicePixelRatio` | `src = sel * dpr` |
| 菜单能显示但点不动 | 托管菜单挂在 `WS_EX_NOACTIVATE` 窗口上 | 换原生 `TrackPopupMenuEx` 或可激活宿主窗口 |
| 连续右键卡死/闪退 | 嵌套模态菜单循环导致栈溢出 | 菜单重入锁（`m_menuOpen`） |
| 分层窗口角上点不到 | alpha=0 像素不接收鼠标 | 铺 alpha=1 底色（透明白板同样处理） |
| 热键注册成功但不触发 | 前台是提权窗口时低权限进程收不到 | 提示以管理员运行（`WH_KEYBOARD_LL` 仍未做） |
| 自动化测试点不中控件 | 测试进程未声明 DPI 感知，拿到 0.8 倍坐标 | `SetProcessDpiAwarenessContext(-4)` |
| `--dump` 之后所有 `--snip` 失灵 | 第二个实例 `removeServer` 抢走了命名管道 | `--dump` 改为向常驻实例发命令 |
| 输入文字后 `Esc` 关不掉截图 | `QLineEdit` 有焦点时按键被它吃掉 | `eventFilter` 里处理 `Esc`/`Enter`，失焦即提交 |
| OCR 报 `Wait ... one or more errors` | WinRT `GetFileFromPathAsync` 不认正斜杠 | 全部路径 `QDir::toNativeSeparators` |
| `SendKeys` 打字被中文输入法吃掉 | 系统 IME 拦截合成按键 | 自动化测试改用剪贴板粘贴文本 |

---

## 4. 工程结构与进度

### 4.1 目录（Qt 主线）

```
D:\TaskPin\repo\TaskPinQt\
├─ CMakeLists.txt          # Qt6 Core/Gui/Widgets/Sql/Network/PrintSupport + windeployqt
├─ build.cmd               # 一键构建（固定 Qt/MinGW/CMake/Ninja 路径）
├─ scripts\
│  ├─ test-snip.ps1        # 真机交互回归（38 项断言，截图到 docs/）
│  └─ show-image.ps1       # 回归用的固定测试窗口（已知位置 / 已知文字）
└─ src\
   ├─ main.cpp             # 单实例、CLI 转发、日志、区域设置、--dump/--selftest
   ├─ app\AppHost.*        # 托盘、热键、命令通道、窗口编排、OCR/AI 编排
   ├─ core\
   │  ├─ Store.*           # SQLite（CRUD / 顺延 / 软删除 / 按天聚合）
   │  ├─ Task.h            # 任务实体
   │  ├─ Settings.*        # JSON 配置（v2：热键表 + 迁移 + .broken 备份 + 原子写）
   │  ├─ SelfTest.*        # 自检（53 项）
   │  ├─ TimeParser.*      # 中文时间解析（T10）
   │  ├─ FileNaming.*      # 文件名规则（O4）
   │  ├─ History.*         # 截屏历史（H1/H2）
   │  ├─ Ocr.*             # Windows.Media.Ocr 子进程封装（R1）
   │  └─ AiClient.*        # OpenAI 兼容归纳（R3）
   ├─ platform\
   │  ├─ Hotkeys.*         # 全局热键（RegisterHotKey + 隐藏消息窗口）
   │  ├─ HotkeyText.*      # 组合键文本 ↔ 修饰键/虚拟键
   │  ├─ HotkeyEdit.*      # 热键录制控件
   │  ├─ ScreenCapture.*   # 多屏冻结画面
   │  ├─ WindowPicker.*    # 光标下窗口命中（窗口吸附）
   │  ├─ WindowFx.*        # 点击穿透 / 不抢焦点 / 置顶 / 抢前台
   │  └─ Autostart.*       # HKCU Run 开机自启
   └─ ui\
      ├─ Annotation.*      # 标注结构体 + 绘制 + 命中测试（含马赛克/模糊）
      ├─ SnipOverlay.*     # 截图浮层（选区 / 工具 / 工具栏 / 文字）
      ├─ CardWindow.*      # 桌面卡片
      ├─ QuickAddDialog.*  # 速记输入
      ├─ PinWindow.*       # 贴图窗口 + 分组
      ├─ HistoryDialog.*   # 截屏历史浏览器
      ├─ CalendarWindow.*  # 月历
      ├─ SettingsDialog.*  # 五页设置
      ├─ ColorPickerOverlay.* # 取色器
      └─ WhiteboardWindow.*# 白板 / 透明白板
```

### 4.2 进度矩阵

| 模块 | 状态 | 证据 |
|---|---|---|
| 单实例 / 托盘 / 热键（8 动作 + 降级） | ✅ | 日志 `hotkeys: 8/8 registered`；`dump.json` hotkeys |
| 任务库 / 顺延 / 优先级 / 软删除 | ✅ | `--selftest` store 段 11 项 |
| 桌面卡片（含拖动记忆 / 双击日期） | ✅ | `qt/docs/test-ocr-task.png` |
| 速记输入 + 时间解析 | ✅ | `--selftest` time parser 段 8 项 |
| 回收站 | ✅ | 托盘/卡片右键；`RecycleDialog` |
| 截图浮层 / 选区 / 12 工具 / 放大镜 | ✅ | `qt/docs/test-snip-tools.png` |
| 窗口检测 / 吸附 | ✅ | 回归「click snaps the whole window 720x220」 |
| 马赛克 / 模糊 / 橡皮 | ✅ | 回归 + `test-snip-tools.png` |
| 复制输出（含 DPR） | ✅ | 回归 371×131 / 720×220 / 2560×1600 |
| 保存 / 快速保存 / 自动保存 / 打印 | ✅ | 代码 `O2–O4/O7`；回归覆盖快速保存外的路径 |
| 贴图窗口（缩放/透明/穿透/分组/翻转） | ✅ | `qt/docs/test-pin.png`；回归 pins 计数 |
| 截屏历史 | ✅ | `qt/docs/test-history.png`；回归 `history.count` |
| 日历 | ✅ | `qt/docs/test-calendar.png` |
| 设置窗口 + 热键录制 | ✅ | `qt/docs/test-settings.png`；回归 settings 往返 |
| OCR → 建任务 | ✅ | 回归「OCR creates exactly one task」+ 日志 `ocr task created ... due=...T15:00:00` |
| AI 归纳 | ✅ | 代码 `core/AiClient`（需自备 Key，未做联网验收） |
| 白板 / 透明白板 | ✅ | `qt/docs/test-board.png` / `test-board-transparent.png` |
| 取色器 | ✅ | 回归「colour picker copies a hex value -> #FFFFFF」 |
| 二维码 / 条码 | ⏳ | 见 §9 |

---

## 5. 数据与配置

### 5.1 数据库

```sql
task(id PK, title, note, ocr_text, source, tags, due, priority, sort_key,
     state, created, updated, completed, origin_date, last_day,
     carry_count, attachment, ai_state, ai_summary, ai_attempts, ai_next_try)
task_day(task_id, day, state, seen_at, PRIMARY KEY(task_id, day))
```

- `state`: `open` / `done` / `dropped`
- `task_day.state`: `open` / `done` / `carried` / `dropped`
- 索引：`idx_day(day)`、`idx_state(state, sort_key)`
- 时间统一 ISO8601 字符串；`day` 用 `yyyy-MM-dd`

### 5.2 配置字段

见 `docs/ARCHITECTURE.md` §8。缺失字段用默认值，**解析失败必须备份 `.broken`**（已实现并自检）。

---

## 6. 诊断与测试接口（新 agent 必读）

### 6.1 命令行

| 命令 | 作用 |
|---|---|
| `TaskPinQt.exe --snip` | 触发区域截图 |
| `--full` | 全屏截图并复制 |
| `--last` | 用上次选区再截一次（带预选框，可调） |
| `--pin` | 把剪贴板内容贴到桌面（图片优先，否则文字） |
| `--pins` | 显示 / 隐藏全部贴图 |
| `--quick` | 打开速记输入 |
| `--toggle` | 显示/隐藏卡片 |
| `--card-top` / `--card-desktop` | 卡片切到全局置顶 / 桌面层（壁纸上方，会被普通窗口盖住） |
| `--card-layer` | 在两种层级之间切换（等价于点托盘勾选项） |
| `--recycle` | 打开回收站 |
| `--history` | 打开截屏历史 |
| `--calendar` | 打开日历 |
| `--settings` | 打开设置 |
| `--color` | 取色器 |
| `--board` / `--board-transparent` | 白板 / 透明白板 |
| `--quit` | 退出 |
| `--dump [path]` | **导出状态快照 JSON**（默认 `<dataDir>\dump.json`）· 向常驻实例请求，不新起实例 |
| `--selftest [dir]` | **自检**：写 `selftest.txt`，退出码 0=全过 · 53 项 |

### 6.2 dump 快照包含

```json
{
  "app": "TaskPinQt",
  "version": "0.2.1",
  "time": "2026-11-08T17:20:00",
  "dataDir": "...", "dbPath": "...", "logPath": "...", "historyDir": "...",
  "settings": { "...全部配置..." },
  "tasks": { "open": 3, "carried": 1, "done": 12, "dropped": 2 },
  "hotkeys": [ {"action": "snip", "chord": "Ctrl+Alt+A", "registered": true} ],
  "card": { "x": 2110, "y": 30, "w": 340, "h": 177, "visible": true,
            "desktopLayer": false, "topMost": true, "zOrder": "topmost" },
  "pins": { "count": 2, "group": "默认", "groups": ["默认"] },
  "history": { "count": 26, "limit": 50 },
  "ocr": { "available": true, "engine": "Windows.Media.Ocr" },
  "screens": [ {"name": "\\\\.\\DISPLAY1", "geometry": "0,0,2048x1280", "dpr": 1.25} ]
}
```

用途：出问题时让用户跑一次 `--dump`，把 JSON 发回来即可复现环境。

### 6.3 日志规范

```
2026-11-08 17:30:07 [INFO] ocr start 720x220
2026-11-08 17:30:08 [INFO] ocr ok lines=1 lang=zh-Hans-CN
2026-11-08 17:30:08 [INFO] ocr task created: 测试 id=... due=2026-11-09T15:00:00
```

- 位置：`<dataDir>/taskpinqt.log`（启动时追加）
- 级别：`INFO` / `WARN` / `CRIT`
- 必须记录的节点：启动、热键注册结果（含降级）、每条命令、截图尺寸、OCR/AI 结果、异常

### 6.4 测试方法

**自动化（推荐）**：`scripts\test-snip.ps1` 已经封装了「DPI 感知 + 模拟鼠标拖拽 + 按键 + 截图 + 断言 + 清理」，
其中 `show-image.ps1` 提供一个位置和内容都固定的测试窗口（720×220，含中文与时间文本）。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File D:\TaskPin\repo\TaskPinQt\scripts\test-snip.ps1
# 结果写 docs\test-snip-report.txt；截图写 docs\test-*.png
```

**注意（回归的副作用）**：`test-snip.ps1` 每跑一次会产生一条 OCR 任务（`source=ocr`，标题是夹具图里的两个字）。
它**不会**再点任务行（旧版会误把用户的任务标成完成）。清理残留：

```powershell
python -c "import sqlite3,os;p=os.path.expandvars(r'%LOCALAPPDATA%\TaskPin\TaskPinQt\taskpin.db');c=sqlite3.connect(p);t='\u6d4b\u8bd5';c.execute('delete from task_day where task_id in (select id from task where source=? and title=?)',('ocr',t));c.execute('delete from task where source=? and title=?',('ocr',t));c.commit()"
```

**手工**：`--snip` → 拖拽 → 按键切工具（`R/O/A/L/P/H/T/N/M/B/E`）→ `Enter` 复制 → 校验剪贴板尺寸。

坐标换算：物理坐标 = 逻辑坐标 × DPR（本机 1.25）。工具栏位置随选区变化，**不要手算**。

---

## 7. 新 agent 上路清单（第一小时）

1. 读本文 §0 / §2 / §6。
2. 跑一次 `build.cmd`，确认 `build\TaskPinQt.exe` 存在且 0 warning。
3. 跑 `--selftest`（应 53/53）与 `scripts\test-snip.ps1`（应 38/38）。
4. 启动应用，确认右上角出现卡片；`--dump` 生成 JSON。
5. 读 `docs/ARCHITECTURE.md`，确认模块边界。
6. 从 §4.2 的 ⏳（目前只有扫码）或 §9 的技术债里挑一项开工，按 §8 交付。

---

## 8. 交付流程（每个模块）

1. **设计**：先想清楚数据从哪来、到哪去，写进本文 §2 的表格；
2. **实现**：按 `platform → core → ui` 顺序写，别跨层;
3. **构建**：`build.cmd` 必须 0 error；有 warning 也要看；
4. **交互测试**：跑 `scripts\test-snip.ps1`（或按同样方式手工），**截图存档**到 `qt/docs/`；
5. **自检**：把可断言的部分加进 `--selftest` 或 `test-snip.ps1`；
6. **文档**：更新本文进度矩阵 + `docs/ARCHITECTURE.md` + `CHANGELOG.md`；
7. **提交**：Qt 主线在 `D:\TaskPin\repo` 仓库、文档在 `D:\TaskPin\repo` 仓库；
   提交信息 `feat(qt): <模块>: <一句话>`，不要攒大批量提交。

**「完成」的定义**：能点、有截图、有日志、文档已更新。四个缺一不可。

---

## 9. 已知问题 / 技术债

| 问题 | 影响 | 计划 |
|---|---|---|
| 全局热键在提权前台窗口下不触发 | 用户误以为坏了 | 改 `WH_KEYBOARD_LL` 低级钩子 |
| 旧 C# 进程可能残留并占用热键 | 新版抢不到键 | 启动时检测并提示（当前靠降级键兜底） |
| 多屏且各屏 DPR 不同 | 输出按主屏 DPR 缩放，副屏会偏 | `Capture` 改为按屏 DPR 逐块裁剪 |
| 扫码（R2）未实现 | 功能缺口 | 离线需要 zxing-cpp（需源码编译 vendored 依赖）；当前没有可用工具链，故不做半成品 |
| 开机自启用 `HKCU\...\Run`，不是计划任务 | 没有 20 秒延迟；不适用于"所有用户" | 需要延迟时改 `schtasks /sc onlogon /delay 0000:20`（需权限） |
| AI 只做标题归纳，不做多模态 | 不能直接读图 | 预留 `AiClient`，可扩展 vision 请求 |
| `SnipOverlay` 仍有 ~900 行 | 维护成本 | `Annotation` 已拆出，工具栏可继续拆 `SnipToolbar.*` |
| OCR 依赖 Windows 语言包 | 无中文包时识别失败 | 日志已给出原因；可在设置页提示安装语言包 |
| 打印未做真机验收 | 可能有边距/缩放差异 | 有打印机时补一次 `O7` 验收 |
| 桌面层模式下卡片会被最大化/全屏窗口盖住 | 属设计取舍（不置顶的代价） | 需要时切回全局置顶：托盘勾选项 / 卡片右键 / `--card-top` |
| 别的应用从"最小化恢复"后出现黑块 / 残影（`Widgets`、`RadeonSoftware`、Chromium 都中过） | 屏幕看着像"重影"，容易被当成 TaskPin 的 bug | **不是 TaskPin 的窗口**：这是 GPU 合成器丢了绘制表面。做法：强制全桌面重绘（`RedrawWindow`）即可恢复，严重时按 `Win+Ctrl+Shift+B` 重置显示驱动。回归脚本已改成不再最小化其它应用的窗口 |

### 验证记录（2026-11-09，Qt 0.2 + 卡片层级切换）

```
> TaskPinQt.exe --selftest D:\TaskPin\repo\docs\artifacts\selftest
== summary: 53 passed, 0 failed ==        (exit code 0)

> powershell -File scripts\test-snip.ps1
PASS  region snip output is physical pixels (371x131)      -> 371x131
PASS  logical selection x dpr == physical output          -> logical 297x105 -> physical 371x131
PASS  click snaps the whole window (720x220)              -> 720x220
PASS  last region re-uses the snapped rectangle (720x220) -> 720x220
PASS  OCR hotkey creates exactly one task                 -> open 5 -> 6
PASS  OCR text reaches TimeParser (due 15:00 parsed)      -> log: ocr task created
PASS  pin window opened                                   -> pins=1 group=默认
PASS  colour picker copies a hex value                    -> #FFFFFF
PASS  card starts topmost                                 -> zOrder=topmost
PASS  card context menu opens                             -> new app windows: 2
PASS  card takes a real click while topmost (+ opens the input) -> new app windows: 1
PASS  card switches to the desktop layer                  -> zOrder=desktop
PASS  card window is findable by title                    -> hwnd=263632
PASS  card still takes clicks on the desktop layer (+ opens the input) -> new app windows: 1
PASS  date double-click opens the calendar from the desktop layer -> hwnd=1573034
PASS  calendar board click selects a day                  -> log: calendar select
PASS  calendar month arrow switches month                 -> log: calendar month
PASS  card returns to topmost                             -> zOrder=topmost
PASS  card show/hide toggle keeps working                 -> visible True -> False -> True
PASS  whiteboard copies the full frozen frame             -> 2560x1600
PASS  transparent whiteboard copies a full-size image      -> 2560x1600
PASS  full screen capture copies the whole desktop        -> 2560x1600
PASS  settings reload resizes the card (300 x 159)        -> card 300x159
PASS  autostart writes the logon entry                    -> autostart=True
PASS  saved z-order mode is restored on start             -> zOrder=desktop
PASS  delayed snip honours the configured delay           -> log: startSnip: delayed 1.0s
== summary: 38 passed, 0 failed ==        (exit code 0)

> TaskPinQt.exe --dump D:\TaskPin\repo\docs\dump.json
dump via running instance: ok -> D:\TaskPin\repo\docs\dump.json   (v0.2.1)
```

> 注意：`TaskPinQt.exe` 是 GUI 子系统程序，PowerShell 里直接用 `&` 不会等它结束，
> 要拿退出码请用 `Start-Process -Wait -PassThru`。
> 回归脚本**不会去动其它应用的窗口**：桌面层的点击/双击用 `PostMessage` 直接发给卡片 HWND，真实鼠标点击与右键菜单在置顶模式验证。

### 证据截图（`D:\TaskPin\repo\docs\screenshots\`）

> 仓库里存的是缩到 1280px 的存证版；重跑 `test-snip.ps1` 会往 `docs\artifacts\`（gitignore）
> 写 2560×1600 的原始截图。

| 文件 | 内容 |
|---|---|
| `test-snip-tools.png` | 截图编辑器：矩形 / 椭圆 / 箭头 / 马赛克 / 模糊 / 中文文字 / 序号 + 工具栏 |
| `test-ocr-task.png` | 卡片里出现 OCR 建出的任务（「测试」，截止明天 15:00） |
| `test-pin.png` | 贴图窗口浮在桌面上 |
| `test-history.png` | 截屏历史缩略图列表 |
| `test-calendar.png` | 月历 + 每日计数 + 当日清单 |
| `test-calendar-click.png` | 从卡片双击日期打开日历后，点击看板选中 11/10 |
| `test-settings.png` | 设置窗口五页中的「卡片」页 |
| `test-card-desktop.png` | 卡片贴在桌面层（壁纸上方），普通窗口已让开 |
| `test-card-menu.png` | 桌面层模式下卡片右键菜单（含「全局置顶显示」勾选项） |
| `test-card-top.png` | 切回全局置顶后的卡片 |
| `test-board.png` | 白板（冻结画面 + 手写） |
| `test-board-transparent.png` | 透明白板（笔迹浮在桌面上） |

### 报告（`D:\TaskPin\repo\docs\reports\`）

| 文件 | 内容 |
|---|---|
| `selftest.txt` | `--selftest` 原始输出（53 项） |
| `test-snip-report.txt` | 交互回归原始输出（38 项） |

---

## 10. 文档地图

| 文档 | 内容 |
|---|---|
| `docs/HANDOVER.md`（本文） | 需求全量 + 迁移思路 + 进度 + 诊断接口 |
| `docs/ARCHITECTURE.md` | 分层、数据模型、流程时序、渲染要点 |
| `docs/legacy/qt-migration.md` | 早期迁移笔记（概念映射、环境版本） |
| `docs/legacy/ACCEPTANCE.md` | C# 版验收测试记录（历史参考） |
| `docs/legacy/FEATURE-MATRIX.md` | C# 版功能矩阵（61 项，历史参考；扫码仍按它对照） |
| `docs/legacy/PROGRESS.md` | 逐轮开发日志（历史） |
| `docs/legacy/RELEASING.md` | C# 版发版笔记（历史） |
| `CHANGELOG.md` | 版本记录 |
