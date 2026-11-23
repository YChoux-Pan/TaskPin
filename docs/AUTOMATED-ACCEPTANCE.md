# Playwright Test + GitHub Actions 验收

TaskPin 是 Qt Widgets 原生程序。Playwright 的 Chromium/DOM 驱动不能直接操作这些窗口；本项目使用 **Playwright Test 的测试调度器** 启动真实的 Qt 测试和应用探针，断言 JSON、像素及窗口几何，并把原生截图附到 HTML 报告。没有为了测试重做网页界面，也不需要移动桌面鼠标、发送系统点击或安装浏览器。

## 本地运行

先按 [构建说明](BUILDING.md) 构建完整 Release 和 OCR 运行时，准备中文测试字体，然后：

```powershell
cmake --build TaskPinQt/build --parallel 2 --target TaskPinTranslationProbe
npx --yes pnpm@11.25.0 install --frozen-lockfile --ignore-scripts
$env:TASKPIN_BUILD_DIR = (Resolve-Path TaskPinQt/build).Path
$env:TASKPIN_TEST_FONT = (Resolve-Path docs/artifacts/fonts/NotoSansCJKsc-Regular.otf).Path
node node_modules/@playwright/test/cli.js test --config tests/playwright/playwright.config.ts
```

Node 24、Playwright Test 1.65.1 和 pnpm 11.25.0；依赖完整性由 pnpm-lock.yaml 固定。Qt、模型和字体仍按各自锁定版本部署。

测试进程强制 `QT_QPA_PLATFORM=offscreen`、`TASKPIN_AUTOMATION=1` 和独立短临时数据目录，绕开 Windows 长路径，结束后清理本次临时目录。不会向日常实例发命令或读取真实 API Key。Qt 内部按键/滚轮事件直接调用应用处理逻辑，不向操作系统注入桌面输入。

## 验收范围

| 项目 | 实际执行 |
|---|---|
| 完整翻译流程 | 生成段落 → F1 编辑器翻译动作 → 真正 PaddleOCR → Mock 翻译 → 桌面贴图 |
| 缩放 | 反向动画、光标锚点、负原点、高 DPI、平滑像素和透明留白 |
| 对照窗口 | 半透明/深浅贴图上仍不透明，内容占比和双栏宽度，原生 PNG |
| 快捷键 | F3/自定义键固定标注选区、空选区不贴剪贴板、普通贴图、实际备用键、文字焦点 |
| 设置 | 翻译按键保存/取消/重载、全局及编辑器冲突、OCR 模型选择/错误/双文件回滚 |
| 模型复用 | 真模型加载、排队、取消、关闭所有者、重启与迟到隔离 |
| 自检 | 独立应用数据库/平台契约 |

Windows GitHub Actions 保留完整 QtTest 和真实 OCR 行为测试，随后执行 Playwright；两个门禁均成功才允许打包发行。CI 不调用付费翻译服务。实际 AI 耗时仅在本机使用生成内容、显式云端探针测量。

产物：`docs/artifacts/playwright/html/`、`results.json`、`junit.xml` 及原生 PNG/JSON/进程输出。GitHub 附件为 `playwright-native-acceptance`。本轮实测与发布验证见 [0.4.3 报告](reports/2026-11-21-ocr-performance-0.4.3.md)。

这些测试验证原生应用的逻辑和渲染，不声称覆盖所有 Windows 桌面合成器、显卡、物理显示器与第三方软件抢键状态。安装后的诊断会核对实际注册结果；遇到其他程序占键时，托盘和编辑器显示实际备用键。
