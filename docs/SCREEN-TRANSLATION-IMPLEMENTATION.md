# 截图翻译实现记录

当前版本 0.4.1，维护 C++17 / Qt 6.4.3 / MinGW 11.2.0。原需求 [SCREEN-TRANSLATION-SPEC.md](SCREEN-TRANSLATION-SPEC.md)；用户后续明确要求复用现有 AI 并提供原图译文覆盖，设计见 superpowers/specs/2026-11-21-translation-repair.md。

TranslationConfig 只保存模式、超时和接口授权，连接与 Key 均来自 Settings 的既有 AI 配置。TranslationSettingsPage 是 SettingsDialog 中的页面，主机拥有唯一非模态设置窗口；没有独立密钥库与模态翻译设置。连接测试使用生产 Client、固定虚构短句并明确跳过缓存。

TranslationWindow 默认展示 TranslationCanvas，原始截图与带几何 Block/Result 分开。Canvas 以局部采样背景遮盖对应文字框并排版译文；代码/公式/失败块保留原图，长文图下注释，输出上限 32MP。原图切换、文字对照、PNG 保存、缩放与复制复用同一份结果。

OCR 使用本地 Windows.Media.Ocr，图像经 stdin 内存流，不写识别内容文件；没有迁移 Python或集成有道模型/私有接口。英文技术字符在本机 zh-Hans-CN 语言包下仍有误识别，需核对原图。真实接口固定样本和原图覆盖已实测，细节/限制见专项报告。
