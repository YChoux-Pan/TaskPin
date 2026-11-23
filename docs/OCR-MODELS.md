# 添加本地 PaddleOCR 模型

TaskPin 0.4.5 随包提供 PP-OCRv6 tiny、PP-OCRv5 mobile 和 PP-OCRv4 mobile。打开 **设置 → 截图翻译 → 本地 OCR** 选择模型；便签识别与截图翻译共用这一选择。升级保留原先的模型选择。

## 直接放入文件

点击 **打开模型目录**，在打开的 `ocr-models` 中新建一个文件夹，把匹配的检测和识别 ONNX 文件放进去。重启 TaskPin 后打开设置，或点击 **刷新**，新模型就会成为可选项。普通官方文件名不需要手写配置、不需要重新编译。

本机安装位置示例：

```text
D:\TaskPin\app\ocr-models\
  我的v6-small\
    PP-OCRv6_det_small.onnx
    PP-OCRv6_rec_small.onnx
  我的v5-mobile\
    ch_PP-OCRv5_det_mobile.onnx
    ch_PP-OCRv5_rec_mobile.onnx
```

每个直接子文件夹只放一份 `det` 检测模型和一份 `rec` 识别模型，同系列、同类型。目录根部也可放一组模型。文件夹名称用于显示，不决定模型架构。可以保留上游许可证或文字说明；含多个检测/识别模型或无法辨认的 ONNX 文件时，请拆成不同子文件夹。

支持的文件名与适配器：

| 家族 | 类型 | 文件名示例 |
|---|---|---|
| PP-OCRv4 / v5 | mobile、server | `ch_PP-OCRv5_det_mobile.onnx` / `ch_PP-OCRv5_rec_mobile.onnx` |
| PP-OCRv6 | tiny、small、medium | `PP-OCRv6_det_small.onnx` / `PP-OCRv6_rec_small.onnx` |

保留官方文件名；`ch_`、`en_`、`multi_` 前缀也可识别。自动配对不猜测不同家族/类型的混合方式。所选模型必须是当前 RapidOCR / ONNX Runtime 能加载的兼容导出，识别 ONNX 需包含匹配的字符字典。原生 Paddle `.pdiparams` 或推理 `.json` 需先转为兼容 ONNX。未来模型若改变网络算子、预处理或输出格式，可能需要更新引擎；“直接放入文件”不意味着任意格式或未来家族自动兼容。

PP-OCRv6 自动发现的模型默认使用检测长边 960、4 个推理线程；v4/v5 保留原有检测与双线程设置。v6 tiny 在此前这台电脑的英文截图实验中取得较好速度；识别效果与速度仍取决于图片和设备，复杂内容建议核对原图。

## 自定义文件名和参数

文件名为 `det.onnx` / `rec.onnx`，或需要混合检测/识别模型、调整 CPU 和检测尺寸时，在同一文件夹放一个可选的 `model.json`：

```json
{
  "schemaVersion": 1,
  "name": "我的 v6 small",
  "description": "本地兼容 ONNX 模型",
  "language": "multi",
  "det": {
    "path": "det.onnx",
    "ocrVersion": "PP-OCRv6",
    "modelType": "small",
    "language": "ch"
  },
  "rec": {
    "path": "rec.onnx",
    "ocrVersion": "PP-OCRv6",
    "modelType": "small",
    "language": "ch"
  },
  "cpu": {"intraOpThreads": 4, "interOpThreads": 1},
  "detector": {"limitType": "max", "limitSideLen": 960}
}
```

`path` 只指向同一文件夹中的 ONNX 文件，不使用绝对路径或 `../`。每份模型可选填 `sha256`；填入后严格校验，不填时也会计算实际哈希用于识别缓存变化。`name`、`description`、`modelVersion`、顶层输出语言、CPU 与 detector 可省略。使用描述文件时，无效或不支持的字段会明确失败，不会忽略描述文件后改用自动猜测。

每个模型的 `language` 可为 `ch` / `en` / `multi`（`zh` 为 `ch` 别名），用于推理适配；顶层 `language` 是输出元数据，可为 `multi` / `zh` / `en` / `zh+en`。识别字典仍由实际 ONNX 决定。CPU `intraOpThreads` 支持1–8、`interOpThreads` 支持1–4；检测 `limitType` 为 `min` 或 `max`、`limitSideLen` 为32–8192。降低检测尺寸可能漏字，更多线程也不保证更快。

## 保存、替换和删除

发现模型时仅扫描名称、目录元数据和小型描述文件；不会在界面线程读取大型权重。选择后点击保存，软件在可取消的后台进程实际核对模型并加载。加载成功且当前草稿未变，才写入设置；取消或失败保留旧选择。

同一模型连续识别可复用已加载的 worker；替换模型字节后会识别新的模型身份并重新加载。模型使用相对于根目录的文件夹位置作为稳定 ID，因此仅替换文件不会丢失选择。重命名或删除所选文件夹后会明确提示重新选择，软件不会悄悄换成其他模型。

不完整、重复、描述损坏或不兼容的模型显示为不可用；一个坏文件夹不影响其他正常文件夹。目录最多扫描128组模型，每组最多64个条目；描述文件最多64 KiB。只扫描根目录及直接子文件夹，不递归搜遍硬盘。模型加载和识别不会自动下载模型或字典，也不执行模型包里的 Python/脚本。

## 更新软件时

保留 `app/ocr-models` 文件夹。它与程序管理的 `app/ocr` 运行时分开；便携发行包只含一份目录说明，不包含用户自行添加的模型。更换程序文件时不要删除已有模型文件夹。应用数据、任务和原有模型选择仍保存在本机用户数据目录。

模型权重下载地址应使用 PaddleOCR 或可靠的兼容转换发布来源，并保留其许可证。随 TaskPin 内置的权重来源、SHA256 与许可证见 [运行时清单](../TaskPinQt/ocr/runtime-lock.json) 和 [第三方说明](../licenses/PADDLEOCR_RUNTIME_NOTICE.md)。
