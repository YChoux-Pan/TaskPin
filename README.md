# TaskPin

**留住画面，记下灵感。** 面向 Windows 的截图、桌面贴图与每日任务卡片。

[English](README.en.md) · [下载发行版](https://github.com/YChoux-Pan/TaskPin/releases) · [更新日志](CHANGELOG.md) · [构建](docs/BUILDING.md) · [测试](docs/TESTING.md)

![TaskPin 0.3.0 截图编辑器，使用示例内容](docs/screenshots/taskpin-0.3.0-editor.png)

*0.3.0 开发版本的真实桌面截图，内容为专用演示夹具；拍摄时程序版本字符串尚未从 0.2.3 更新。它展示编辑器布局，不代表全部功能已经完成真机验收。旧 `test-*.png` 为 0.2.x 历史截图。*

## 画面与任务，放在手边

- **截图与标注**：捕获鼠标所在显示器，框选或选取窗口；矩形、椭圆、箭头、直线、画笔、荧光笔、文字、序号、马赛克、模糊和橡皮。工具栏按选区位置与可用空间排列，参数随工具切换；支持撤销、重做和标注选择编辑。
- **输出与贴图**：复制、保存、快速保存、桌面贴图，以及「识别为任务」。贴图支持缩放、透明度、旋转、翻转、穿透与分组；右键菜单按用途组织。直接从截图贴图时保留选区原始位置和逻辑尺寸。
- **每日任务卡片**：速记、中文时间解析、优先级、任务备注、完成、顺延、回收站与日历回看；卡片可以全局置顶或置于桌面层。
- **辅助工具**：截屏历史、白板、透明白板、取色器与可配置全局热键。打印和可选 AI 接口已有入口，环境验证范围见[测试说明](docs/TESTING.md)。

项目使用 C++17 / Qt 6，目标系统为 Windows 10/11 x64。当前界面主要为中文。仓库的维护实现是 `TaskPinQt/`；早期 C# 实现仅属于历史，不作为当前功能承诺。

## 下载与开始

1. 在 [Releases](https://github.com/YChoux-Pan/TaskPin/releases) 下载 `TaskPin-0.3.0-windows-x64.zip` 与 `SHA256SUMS.txt`（该版本发布后提供）。
2. 解压到有写权限的文件夹，保留所有 DLL 与插件子目录，运行 `TaskPinQt.exe`。无需安装 Qt 或开发工具。
3. 在系统托盘中打开设置，检查快捷键。启动后可通过托盘退出；关闭单个窗口不等于退出常驻程序。

校验下载文件：`Get-FileHash -Algorithm SHA256 -LiteralPath .\TaskPin-0.3.0-windows-x64.zip`，与校验文件中的同名条目逐字比较。便携版指程序无需安装，**用户数据仍默认写入本机用户目录**。升级前通过托盘退出旧版本并备份数据，再替换程序目录。

## 常用快捷键

| 全局操作 | 默认快捷键 |
|---|---|
| 区域截图 / 当前显示器全屏 / 上次区域 | `F1` / `Ctrl+Alt+F` / `Ctrl+Alt+R` |
| 贴住剪贴板图片 / 显隐全部贴图 | `F3` / `Shift+F3` |
| 速记 / 显隐卡片 / 截屏历史 | `Ctrl+Alt+Space` / `Ctrl+Alt+D` / `Ctrl+Alt+G` |

编辑器内：`V` 选择、`R` 矩形、`O` 椭圆、`A` 箭头、`T` 文字、`M` 马赛克、`B` 模糊；`Ctrl+Z` / `Ctrl+Y` 撤销 / 重做，`Enter` 复制，`Ctrl+S` 保存，`Ctrl+Shift+S` 快速保存，`Ctrl+P` 贴图，`Ctrl+R` 识别为任务，`Esc` 取消。完整工具提示直接显示在按钮上方。全局快捷键可在设置中更改，冲突时以托盘显示的已注册快捷键为准。

保存或快速保存成功后，截图的完整文件路径会自动复制到剪贴板（Windows 原生路径格式，可直接粘贴到文件路径输入框）。`Ctrl+Shift+S` 使用设置中的自动保存目录，文件重名时自动添加序号；保存失败或取消不会改动剪贴板。复制图片仍使用 `Enter`。

截图、贴图及全部贴图显隐的默认快捷键与 Snipaste 对齐。升级时，仅迁移旧版的这三个默认值，保留自定义按键；之后重新设置的按键不会再次被迁移。如果 Snipaste 同时运行并占用这些按键，TaskPin 会使用备用键，请查看托盘中的实际按键。

## 数据与隐私

默认数据目录为 `%LOCALAPPDATA%\TaskPin\TaskPinQt\`：`taskpin.db`（SQLite 与 WAL）、`settings.json`、`taskpinqt.log`、`history\snip\` 和 `ocr\`。可用环境变量 `TASKPIN_DATA` 指定独立目录；退出应用后再复制整个目录进行备份。截图历史和 OCR 临时文件可能包含敏感内容，请自行管理保存范围。

截图、任务与 Windows OCR 在本地处理；OCR 需要系统 OCR 组件与合适的语言包。「识别为任务」把识别结果接入已有任务流程。**AI 默认关闭**；主动配置并启用 OpenAI 兼容服务后，会把最多 4000 字符的识别文本发送到所填服务地址。API Key 存储于本机 `settings.json`，目前不做系统凭据加密；不要分享配置、数据库或日志，AI 服务按其自身政策处理数据。失败时使用本地识别文本回落，不要求 AI 才能使用截图与任务功能。

## 开发与参与

```powershell
$env:QT_KIT = 'C:/Qt/6.4.3/mingw_64'
# 匹配的 MinGW、CMake、Ninja 加入 PATH，或通过参数指定，详见构建文档
cmd /c TaskPinQt\build.cmd
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/test.ps1
```

[构建与打包](docs/BUILDING.md) · [测试与证据边界](docs/TESTING.md) · [架构](docs/ARCHITECTURE.md) · [贡献指南](CONTRIBUTING.md) · [发布流程](docs/RELEASING.md)

## 已知限制与后续方向

- 截图范围为鼠标所在的一块显示器，暂不支持跨显示器拼接捕获。
- 自动化测试覆盖多种 DPR、负坐标和窄视口夹具；不等同于所有真实混合 DPI 显示器验证。
- 打印机、真实 AI 服务、不同 Windows OCR 语言环境仍需独立验证。0.3.0 完整编辑 / 笔记桌面验收进度见测试文档。
- 二维码 / 条码、剪贴板监听自动建任务尚未实现。后续优先补充真实环境覆盖、无障碍与交互打磨；这些方向不构成发布日期承诺。

## 许可

TaskPin 自有代码采用 [Unlicense](LICENSE)。第三方图标和动态运行库各自保留许可：Lucide ISC（含 Feather MIT 声明）、Material Design Icons 图标 Apache-2.0、Qt LGPL/GPL 及 MinGW 运行库许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [licenses/](licenses)。
