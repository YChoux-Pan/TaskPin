"""One image in, one JSON object out. All inference stays in memory and offline."""
from __future__ import annotations

import base64
import contextlib
import hashlib
import io
import json
import math
import os
from pathlib import Path
import socket
import struct
import sys

MAX_PIXELS = 16_000_000
MAX_PNG_BYTES = 64 * 1024 * 1024
MAX_INPUT_BYTES = 90 * 1024 * 1024
MAX_OUTPUT_BYTES = 1024 * 1024
MAX_LINES = 512
MAX_CHARACTERS = 32_000
MODEL_VERSION = "PP-OCRv5_mobile/RapidOCR-v3.10.0"
EXPECTED_MODELS = {
    "det": ("ch_PP-OCRv5_det_mobile.onnx", "4d97c44a20d30a81aad087d6a396b08f786c4635742afc391f6621f5c6ae78ae"),
    "rec": ("ch_PP-OCRv5_rec_mobile.onnx", "5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5"),
}


class OcrFailure(Exception):
    def __init__(self, code: str):
        self.code = code


@contextlib.contextmanager
def quiet_dependencies():
    """Suppress Python and native diagnostics, which can contain image contents."""
    sys.stdout.flush()
    sys.stderr.flush()
    saved = (os.dup(1), os.dup(2))
    with open(os.devnull, "w", encoding="utf-8") as null:
        try:
            os.dup2(null.fileno(), 1)
            os.dup2(null.fileno(), 2)
            with contextlib.redirect_stdout(null), contextlib.redirect_stderr(null):
                yield
        finally:
            os.dup2(saved[0], 1)
            os.dup2(saved[1], 2)
            os.close(saved[0])
            os.close(saved[1])


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_models(root: Path) -> dict[str, Path]:
    try:
        manifest = json.loads((root / "runtime-lock.json").read_text(encoding="utf-8"))
        if manifest["modelVersion"] != MODEL_VERSION:
            raise OcrFailure("runtime_incompatible")
        declared = {model["task"]: (model["filename"], model["sha256"]) for model in manifest["models"]}
        if declared != EXPECTED_MODELS:
            raise OcrFailure("runtime_incompatible")
    except OcrFailure:
        raise
    except (OSError, ValueError, KeyError, TypeError):
        raise OcrFailure("runtime_incomplete") from None
    paths = {}
    for task, (name, expected) in EXPECTED_MODELS.items():
        path = root / "models" / name
        if not path.is_file():
            raise OcrFailure("model_missing")
        if sha256(path) != expected:
            raise OcrFailure("model_hash_mismatch")
        paths[task] = path
    return paths


def read_request() -> bytes:
    data = sys.stdin.buffer.read(MAX_INPUT_BYTES + 1)
    if len(data) > MAX_INPUT_BYTES:
        raise OcrFailure("input_too_large")
    try:
        request = json.loads(data)
        encoded = request["image"]
        if not isinstance(encoded, str) or not encoded:
            raise ValueError
        if len(encoded) > 4 * ((MAX_PNG_BYTES + 2) // 3):
            raise OcrFailure("input_too_large")
        png = base64.b64decode(encoded, validate=True)
    except OcrFailure:
        raise
    except (ValueError, KeyError, TypeError):
        raise OcrFailure("invalid_request") from None
    if len(png) > MAX_PNG_BYTES:
        raise OcrFailure("input_too_large")
    if len(png) < 33 or png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR":
        raise OcrFailure("invalid_image")
    width, height = struct.unpack_from(">II", png, 16)
    if not width or not height:
        raise OcrFailure("invalid_image")
    if width * height > MAX_PIXELS or max(width, height) > 16_384:
        raise OcrFailure("image_too_large")
    return png


def deny_network(*_args, **_kwargs):
    raise OcrFailure("network_disabled")


def infer(root: Path, models: dict[str, Path], png: bytes | None) -> dict:
    # Set before importing any native library. The process never changes user settings.
    os.environ["OMP_NUM_THREADS"] = "2"
    os.environ["OPENBLAS_NUM_THREADS"] = "2"
    os.environ["MKL_NUM_THREADS"] = "2"
    # Explicit paths already prevent model downloads. Also fail closed if a future
    # dependency attempts an HTTP connection, font download, or other socket use.
    socket.socket.connect = deny_network
    socket.socket.connect_ex = deny_network
    socket.create_connection = deny_network
    socket.getaddrinfo = deny_network
    with quiet_dependencies():
        try:
            from importlib.metadata import version
            if version("rapidocr") != "3.10.0" or version("onnxruntime") != "1.32.0":
                raise OcrFailure("runtime_incompatible")
            import cv2
            import numpy as np
            from PIL import Image
            from rapidocr import RapidOCR, ModelType, OCRVersion
            from rapidocr.utils.download_file import DownloadFile
            DownloadFile.run = staticmethod(deny_network)
        except OcrFailure:
            raise
        except Exception:
            raise OcrFailure("runtime_unavailable") from None

        cv2.setNumThreads(2)
        if png is None:
            image = np.full((64, 128, 3), 255, dtype=np.uint8)
        else:
            try:
                with Image.open(io.BytesIO(png)) as original:
                    if original.format != "PNG" or getattr(original, "n_frames", 1) != 1:
                        raise ValueError
                    original.load()
                    if original.mode == "RGBA" or "transparency" in original.info:
                        rgba = original.convert("RGBA")
                        rgb = Image.new("RGB", rgba.size, "white")
                        rgb.paste(rgba, mask=rgba.getchannel("A"))
                    else:
                        rgb = original.convert("RGB")
                    image = cv2.cvtColor(np.asarray(rgb), cv2.COLOR_RGB2BGR)
            except Exception:
                raise OcrFailure("invalid_image") from None

        params = {
            "Global.log_level": "critical",
            "Global.model_root_dir": str(root / "models"),
            "Global.use_cls": False,
            "Global.return_word_box": False,
            "Global.return_single_char_box": False,
            "Det.model_path": str(models["det"]),
            "Rec.model_path": str(models["rec"]),
            "Det.ocr_version": OCRVersion.PPOCRV5,
            "Rec.ocr_version": OCRVersion.PPOCRV5,
            "Det.model_type": ModelType.MOBILE,
            "Rec.model_type": ModelType.MOBILE,
            "EngineConfig.onnxruntime.intra_op_num_threads": 2,
            "EngineConfig.onnxruntime.inter_op_num_threads": 1,
            "EngineConfig.onnxruntime.use_cuda": False,
            "EngineConfig.onnxruntime.use_dml": False,
            "EngineConfig.onnxruntime.use_cann": False,
        }
        try:
            engine = RapidOCR(params=params)
            # Models are lazy in RapidOCR 3.10; load both even for a blank image.
            engine._load_det_model()
            engine._load_rec_model()
            result = engine(image)
        except OcrFailure:
            raise
        except Exception:
            raise OcrFailure("inference_failed") from None

        lines = []
        characters = 0
        if result.txts is not None:
            if result.boxes is None or result.scores is None:
                raise OcrFailure("invalid_model_output")
            if not (len(result.txts) == len(result.boxes) == len(result.scores)):
                raise OcrFailure("invalid_model_output")
            height, width = image.shape[:2]
            for text, box, score in zip(result.txts, result.boxes, result.scores):
                confidence = float(score)
                if not isinstance(text, str) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
                    raise OcrFailure("invalid_model_output")
                if len(lines) >= MAX_LINES or characters + len(text) > MAX_CHARACTERS:
                    raise OcrFailure("output_too_large")
                if not np.isfinite(box).all():
                    raise OcrFailure("invalid_model_output")
                x = max(0, min(width, math.floor(float(np.min(box[:, 0])))))
                y = max(0, min(height, math.floor(float(np.min(box[:, 1])))))
                right = max(x, min(width, math.ceil(float(np.max(box[:, 0])))))
                bottom = max(y, min(height, math.ceil(float(np.max(box[:, 1])))))
                if right == x or bottom == y:
                    continue
                lines.append({"text": text, "x": x, "y": y, "width": right - x,
                              "height": bottom - y, "confidence": confidence})
                characters += len(text)
    return {"ok": True, "engine": "PaddleOCR PP-OCRv5", "language": "multi",
            "modelVersion": MODEL_VERSION, "lines": lines}


def main() -> int:
    try:
        root = Path(__file__).resolve().parent
        self_check = sys.argv[1:] == ["--self-check"]
        if sys.argv[1:] and not self_check:
            raise OcrFailure("invalid_arguments")
        png = None if self_check else read_request()
        models = verify_models(root)
        reply = infer(root, models, png)
        if self_check:
            reply["runtimeReady"] = True
        output = json.dumps(reply, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
        if len(output) > MAX_OUTPUT_BYTES:
            raise OcrFailure("output_too_large")
        code = 0
    except OcrFailure as error:
        output = json.dumps({"ok": False, "errorCode": error.code}, separators=(",", ":")).encode("ascii")
        code = 1
    except Exception:
        output = b'{"ok":false,"errorCode":"internal_error"}'
        code = 1
    # No exception messages, user text, image bytes, or filesystem paths are logged.
    os.write(1, output + b"\n")
    return code


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    raise SystemExit(main())
