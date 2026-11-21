# 截图翻译实现记录 · 0.4.2

用户最新要求覆盖原先独立入口设计：翻译集成进F1截图工具栏；不再保留独立截图翻译按钮/全局快捷键/专用遮罩。结果直接作为桌面贴图，OCR明确使用PaddleOCR系列模型。

SnipOverlay 的翻译操作输出当前合成选区和全局逻辑矩形；AppHost 创建继承PinWindow的TranslationWindow。沿用贴图移动、缩放、旋转、透明度、边框、穿透、分组与显隐，异步replaceFrame保留视图。文字对照仅由右键主动打开。AppHost拥有全部结果贴图，主要任务串行；取消OCR后的贴图可以原图重试。

本地识别改用PaddleOCR PP-OCRv5 Det/Rec模型，RapidOCR执行封装、ONNX Runtime CPU及独立Python运行时。翻译和便签识别共享该适配器。运行时使用本地模型与固定hash，输入PNG经stdin，输出含坐标/真实分数/引擎元数据，不写截图或识别结果文件，不自动联网下载。

共享AI连接、endpoint授权、取消与迟到隔离、技术token保护、原图渲染和单一设置页继承0.4.1。模型/依赖体积、版本/许可、生成图片实测及测试计数按本轮报告记录，不将旧Windows OCR样本冒充PaddleOCR结果。
