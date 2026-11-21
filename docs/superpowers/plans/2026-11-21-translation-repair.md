# 截图翻译修复 Implementation Plan

**Goal:** 共用现有 AI，统一设置，默认原图覆盖译文，并完成真实接口测试与发布。

**Architecture:** Config 解析共享 Settings，TranslationSettingsPage 为设置内组件；TranslationCanvas 独立渲染带几何的块结果。AppHost 管理唯一设置窗口与翻译窗口，Client 保持异步且不记录内容。

**Tech Stack:** C++17 / Qt 6.4.3 / Windows.Media.Ocr / DeepSeek Chat Completions。

**Spec:** ../specs/2026-11-21-translation-repair.md

## 约束

不修改F1和任务数据；不输出密钥；真实接口只使用虚构文本；设置页无第二套 Key；保留原图与失败块；所有发布文件校验；D:\TaskPin 只保留 app/repo/release。

## 执行顺序

- [ ] Config / TranslationWindow：先添加 `sharedAiConfigurationIgnoresLegacyTranslationKey` 回归，断言旧翻译endpoint与共享AI变化时实际连接使用Settings；使用 `Config::load()` 解析共享endpoint/model，`Config::apiKey()` 取现有Key。
- [ ] 新增 TranslationSettingsPage.cpp/.h，SettingsDialog 构造新增tab及 `showTranslationPage()`；AppHost 所有入口只 `showSettings()` 并选页。测试 `repeatedSettingsEntryUsesOneNonmodalWindow`，用QTimer watchdog和QDialog::isModal验证无嵌套exec；保存/取消、连接草稿和授权变化分别测试。
- [ ] TranslationCanvas.cpp/.h：`setContent(image,blocks,results)`、`renderedImage()`、`setOriginal(bool)`、`setZoom(double)`；先用合成图及Block.rect测试原图像素保持、成功块覆盖、失败/代码区域保持、长译文完整注释和原图切换。
- [ ] TranslationWindow 以堆叠视图默认显示canvas，对照面板可切换；信号settingsRequested交给Host；增添保存译图/复制/原译切换，所有Client信号更新canvas，不重建OCR。测试取消/旧结果/视图切换。
- [ ] 开发探针扩展：只从生产配置内存读取共享Key，复制到隔离Settings而不落盘，用真实TranslationWindow及生成图走OCR→云端→原图覆盖；记录HTTP状态/耗时/生成样本与渲染结果，不记录Key。QtTest fixture server继续验证错误/超时/取消。
- [ ] 全回归、独立审查、0.4.1文档/打包/本机数据对照、PR CI与主分支、tag发行、下载校验和/独立自检、安装真实发行版、清理临时回滚。
