# 网易有道翻译：截图翻译静态证据与 TaskPin 设计参考

调查时间：2026-11-21（Asia/Shanghai）。目标为已安装的 `YoudaoDict.exe`，文件版本 **11.3.26.0**，SHA-256 `d6b63a62c202aecb9438f583493ecc148edef07ec43b0dca94fc17d8441b451c`。

**可采用的结论：有道安装包确实包含 Windows 原生程序、CEF 浏览器组件、独立截图程序、本地 OCR 库与识别模型，以及“对照／原译切换／保存译图／钉图”的 UI 定义。现有证据不能确定当前截图翻译实际使用哪条 OCR 路径、哪种翻译模型，或如何清除原文字及回填背景。TaskPin 可独立实现相同交互目标，不必依赖有道组件或接口。**

## 范围与工具

按 `reverse-engineer-anything` 技能执行。当前会话未提供 REA MCP 工具；已有 `D:\REA\bin\rea.cmd` 6.8.0 可用。只读 `doctor --client codex --json` 显示 Codex 未注册 REA；未修改配置，直接使用现有 CLI 的便携 PE import/export 解析。未启动、附加或修改有道进程，未读取账号数据、密钥、用户缓存或私有 API，未抓用户流量。网页证据仅来自匿名获取的官方公开产品 HTML 与该页引用的公共静态 JS。

没有进行反编译、调用图追踪、运行时验证或图像质量实验。下文“UI 定义”不等于当前运行时显示与功能可用性已验证；导入／导出表也不证明相关函数实际被调用。

## 证据表

| 问题 | 观察事实与来源 | 可支持的推断 | 未知／限制 |
|---|---|---|---|
| 官方截图翻译入口 | [官方下载页](https://fanyi.youdao.com/download-Windows/)的公开 JS 文案：Windows 下按 `Ctrl + alt + D`，选取屏幕任意区域实现快速翻译。HTML 描述同时列出截屏翻译、图片翻译。保存于 `official-download.html`、`official-download-app.js`。 | 产品把“区域选择 → 翻译结果”作为快捷操作。 | 官方文案不是本版本实际操作录像；网页可能独立更新。 |
| 官方 OCR 能力声明 | 同一公共 JS 中“翻图片”文案为“支持对99种语言的图片进行OCR识别，自动提取图片中的文字并翻译”。 | OCR 与翻译是产品明确宣称的两个环节。 | **99 种是图片翻译的官方声明**，不能据此认定本地截图 OCR 模型支持相同范围。 |
| Windows / CEF 技术栈 | 主程序为 x86 PE；导入 Win32/GDI/GDI+/Direct2D/DirectWrite；延迟导入 `libcef.dll!cef_initialize`、`cef_browser_host_create_browser` 等。安装 `libcef.dll` 元数据为 CEF `127.1.4+ge71a509+chromium-127.0.6533.89`。REA E1；文件清单 S1。 | 原生 Windows 宿主结合 CEF 的混合结构有直接静态证据。 | CEF 承担哪些页面、截图覆盖窗口由哪个模块绘制，未追踪。 |
| Qt / Electron | 对版本目录递归文件名检查，未发现 `Qt*`、`electron*`、`node.dll` 或 `.asar`；S1。 | 当前清单更符合原生宿主 + CEF，**没有足够证据把它认定为 Qt 或 Electron 应用**。 | 文件名缺失不能排除静态链接、重命名或其他目录组件；UI XML 的具体框架未识别。 |
| 本地 OCR 组件 | `YoudaoOcr.exe` 导入 `youdao_ocr_lib.dll` 的 `recog_model_init`、`ocr_recognize_line`、`switch_to_language`、`release_model`。DLL 导出名称含 `YoudaoOCR`、`cv::Mat` 和 `tensorflow::DEVICE_CPU/GPU/SYCL`。版本目录包含 `recognition/model`、`model_jp`、`model_latin` 及对应 label 文件。REA E2/E3；S1。 | 安装包包含本地行文字识别能力；导出名与 OpenCV／TensorFlow 相关编译接口一致。 | 导出名不证明当前截图流程调用它，也不能确定具体网络结构、检测器、版本、GPU 执行、准确率或离线覆盖。`YoudaoOcr.exe` 自报 11.3.24.0，说明文件版本不必与目录版本一致。 |
| 截图与绘制能力 | `YdScreenCapture.exe` 为 x86 PE；导入 `BitBlt`、`CreateDIBSection`、`GetDIBits`、`DrawTextW/ExW`、`UpdateLayeredWindow`、`DWriteCreateFactory`、WinHTTP；REA E4。 | 原生屏幕位图、文字绘制、分层窗口与联网功能都具备静态接口基础。 | 不能将这些通用导入当成实际截图算法、覆盖算法、背景修复算法、进程通信协议或网络目的地的证据。 |
| 原图翻译相关交互 | 安装资源 `skins/screencap_setwnd_layout.xml`：第14行 `ContrastMode`“对照”；16行 `OriTransSwitch`；18行 `SaveScreenTransImage`；22行 `EditText`；24行 `NailImage`；26行 `RestartScreenCap`；57/62行源／目标语言选择。S2。 | 可参考其“结果就地查看、随时看原图、保存、钉住、重截”的交互组合。 | UI 定义不说明译文块坐标、背景去字方式、布局保留程度或当前会员状态下可用性。 |
| 文字提取与结果状态 | `screencap_extract_text_setting_layout.xml` 第19/22/25/28行依次定义“原文／译文／原文+译文／逐句对照”；`strings.xml` 第174行定义框选与 Esc 退出；175/176/179行定义翻译中、未识别、恢复网络提示。S2。 | 翻译视图与可复制文字视图可以共用一次识别结果；流程应有加载、空结果和错误状态。 | 网络提示不能证明全部 OCR 都在云端；本地模型文件也不能证明全部操作都离线。 |

## TaskPin 可独立采用的实现方案

以下是**设计建议，不是有道内部实现的还原**。

1. **保留原图，单独绘制译文层。** 数据保存 `originalImage + OCR blocks + translations`，默认在原截图坐标处显示译文，按住或一键切换原图；导出时再合成位图。避免译文覆盖结果成为下一次 OCR 输入。
2. **OCR 输出必须包含坐标。** 统一为原图像素坐标的行框／段落框、文本、置信度、方向；OCR 提供器可替换，优先复用 TaskPin 已有方案，缺坐标时引入本地坐标型 OCR。仅有整段纯文本不足以可靠覆盖原图。
3. **先合并段落再翻译。** 根据行高、左右边界、间距与列分组，将同段落行合并，给每个块稳定 ID，批量翻译时按 ID 对齐；保留表格单元格、按钮和多栏边界。译文长度通常变化，逐词替换容易破坏排版。
4. **先做适合 UI 截图的背景覆盖。** 在文字框外围取样，使用稳健背景色填充局部文字区域，再绘制对比度足够的译文。复杂照片、渐变和纹理需要单独的文字掩膜与修补策略；可在低置信度区域显示不透明或半透明译文底板。不要把整张图刷白，也不要宣称简单采色等同于内容感知去字。
5. **文字排版以可读为先。** 在段落框内自动换行、逐步缩小字号并设最小字号；不够时有限扩展到可用空白区，仍不足则允许展开文本。保留块级裁剪，避免盖住邻近 UI；处理高 DPI、截图缩放、负屏幕坐标和旋转框的坐标变换。
6. **翻译服务使用用户配置的合法服务。** OCR 与翻译接口独立，支持取消、超时、缓存与失败重试；以明确的服务适配器调用公开接口，不复用有道私有协议、cookie、内置密钥或二进制组件。可同时提供提取文字／双语对照视图。

建议验收覆盖：白底深字、深底浅字、两栏文档、按钮和表格、密集小字、超长译文、复杂背景、125%/150% DPI；检查原图切换无损、译文不串块、文字不溢出、保存图与当前视图一致。原图覆盖的核心难点是**几何对齐、去字背景与译文排版**；仅替换翻译模型不足以解决这些问题。


## 证据标识

本次本机 REA CLI 6.8.0 只读分析保存的标识：E1 `ev_812feecded20b0a0cd8d6c0b5ff196868815effd7b6f8749ac1f8cbe5422072c`；E2 `ev_95cc7ad9790872b14686653b8b77dc1582da4639017fe90e73e2d707e717c1e3`；E3 `ev_75691634bdb8753619635f4d8e56a9a59a61b97141bca889611aeaa089d36af5`；E4 `ev_479d57b9c57262b64051a9a52e5b2ceb56c02bc8365fc67e26b940a144e08fa8`。完整证据留在本机开发目录，不把有道二进制、模型或专有界面资源纳入 TaskPin 源码或发行包。
