TaskPin local OCR models

Put one model pair in each immediate subfolder of this directory, then restart
TaskPin or use Settings > Screenshot translation > Local OCR > Refresh.

Examples (compatible ONNX files with their original official filenames):

ocr-models/My-v6-small/PP-OCRv6_det_small.onnx
ocr-models/My-v6-small/PP-OCRv6_rec_small.onnx

ocr-models/My-v5-mobile/ch_PP-OCRv5_det_mobile.onnx
ocr-models/My-v5-mobile/ch_PP-OCRv5_rec_mobile.onnx

Supported adapters: PP-OCRv4/v5 mobile or server; PP-OCRv6 tiny, small or medium.
Each package needs both detection (det) and recognition (rec) files. Generic
filenames or mixed pairs need an optional model.json; see docs/OCR-MODELS.md in
the source repository. Recognition ONNX must include its matching character
dictionary metadata. Paddle .pdiparams exports need compatible ONNX conversion.

The app discovers candidates from names without loading large models. Saving a
selection verifies and actually loads it in the background. Incomplete or
unsupported packages show an error; no automatic downloads or model fallback.
Keep upstream licenses with weights you distribute. Future model architectures
can require a runtime adapter update; arbitrary ONNX files are not guaranteed.

Keep this folder when upgrading. It is separate from the managed ocr/ runtime.
