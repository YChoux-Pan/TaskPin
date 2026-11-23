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
import re
import socket
import struct
import sys
import time

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


def selected_profile(root: Path, path: str | None) -> dict:
    if path is None:
        models = verify_models(root)
        profile = {"id": "builtin-ppocrv5-mobile", "modelVersion": MODEL_VERSION, "language": "multi",
                   "cpu": {"intraOpThreads": 2, "interOpThreads": 1}}
        for task, (_, digest) in EXPECTED_MODELS.items():
            profile[task] = {"path": models[task], "sha256": digest, "ocrVersion": "PP-OCRv5", "modelType": "mobile"}
    else:
        try:
            location = Path(path)
            if not location.is_file():
                raise OcrFailure("profile_missing")
            if location.stat().st_size > 64 * 1024:
                raise OcrFailure("profile_invalid")
            profile = json.loads(location.read_text(encoding="utf-8-sig"))
            if not isinstance(profile, dict) or set(profile) - {"schemaVersion", "id", "modelVersion", "language", "det", "rec", "cpu"}:
                raise OcrFailure("profile_invalid")
            if isinstance(profile.get("schemaVersion"), bool) or profile.get("schemaVersion") != 1:
                raise OcrFailure("profile_invalid")
            if not isinstance(profile.get("id"), str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", profile["id"]):
                raise OcrFailure("profile_invalid")
            if not isinstance(profile.get("modelVersion"), str) or not profile["modelVersion"].strip() or len(profile["modelVersion"]) > 128:
                raise OcrFailure("profile_invalid")
            if profile.get("language") not in ("multi", "zh", "en", "zh+en"):
                raise OcrFailure("profile_unsupported")
            cpu = profile.setdefault("cpu", {"intraOpThreads": 2, "interOpThreads": 1})
            if not isinstance(cpu, dict) or set(cpu) - {"intraOpThreads", "interOpThreads"}:
                raise OcrFailure("profile_invalid")
            for name, default, maximum in (("intraOpThreads", 2, 8), ("interOpThreads", 1, 4)):
                value = cpu.setdefault(name, default)
                if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value != math.floor(value) or not 1 <= value <= maximum:
                    raise OcrFailure("profile_unsupported")
                cpu[name] = int(value)
            for task in ("det", "rec"):
                model = profile.get(task)
                if not isinstance(model, dict) or set(model) != {"path", "sha256", "ocrVersion", "modelType"}:
                    raise OcrFailure("profile_invalid")
                if model["ocrVersion"] not in ("PP-OCRv4", "PP-OCRv5") or model["modelType"] not in ("mobile", "server"):
                    raise OcrFailure("profile_unsupported")
                if not isinstance(model["path"], str) or not model["path"] or not isinstance(model["sha256"], str) or not re.fullmatch(r"[a-fA-F0-9]{64}", model["sha256"]):
                    raise OcrFailure("profile_invalid")
            # Validate all options before touching model bytes. There is no implicit
            # default or remote model resolution for a custom profile.
            for task in ("det", "rec"):
                model = profile[task]
                weight = Path(model["path"])
                if not weight.is_absolute():
                    weight = location.parent / weight
                if weight.suffix.lower() != ".onnx":
                    raise OcrFailure("profile_invalid")
                if not weight.is_file():
                    raise OcrFailure("model_missing")
                if sha256(weight) != model["sha256"].lower():
                    raise OcrFailure("model_hash_mismatch")
                model["path"] = weight.resolve()
                model["sha256"] = model["sha256"].lower()
        except OcrFailure:
            raise
        except (OSError, ValueError, TypeError, KeyError):
            raise OcrFailure("profile_invalid") from None
    identity = {task: {key: value for key, value in profile[task].items() if key != "path"} for task in ("det", "rec")}
    identity["cpu"] = profile["cpu"]
    profile["modelIdentity"] = "sha256:" + hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return profile


def read_request(data: bytes | None = None, worker: bool = False) -> tuple[bytes, str | None]:
    if data is None:
        data = sys.stdin.buffer.read(MAX_INPUT_BYTES + 1)
    if len(data) > MAX_INPUT_BYTES:
        raise OcrFailure("input_too_large")
    try:
        request = json.loads(data)
        if not isinstance(request, dict) or set(request) - ({"image", "profilePath", "requestId"} if worker else {"image", "profilePath"}):
            raise ValueError
        profile_path = request.get("profilePath")
        if profile_path is not None and (not isinstance(profile_path, str) or not profile_path or len(profile_path) > 32768):
            raise ValueError
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
    return png, profile_path


def deny_network(*_args, **_kwargs):
    raise OcrFailure("network_disabled")


def infer(root: Path, profile: dict, png: bytes | None, cache: dict | None = None) -> dict:
    # Set before importing any native library. The process never changes user settings.
    threads = profile["cpu"]["intraOpThreads"]
    os.environ["OMP_NUM_THREADS"] = str(threads)
    os.environ["OPENBLAS_NUM_THREADS"] = str(threads)
    os.environ["MKL_NUM_THREADS"] = str(threads)
    # Explicit paths already prevent model downloads. Also fail closed if a future
    # dependency attempts an HTTP connection, font download, or other socket use.
    socket.socket.connect = deny_network
    socket.socket.connect_ex = deny_network
    socket.create_connection = deny_network
    socket.getaddrinfo = deny_network
    stages = {}
    started = time.perf_counter()
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
        stages["import"] = (time.perf_counter() - started) * 1000
        started = time.perf_counter()
        cv2.setNumThreads(threads)
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
        stages["prepareImage"] = (time.perf_counter() - started) * 1000
        params = {
            "Global.log_level": "critical",
            "Global.model_root_dir": str(root / "models"),
            "Global.use_cls": False,
            "Global.return_word_box": False,
            "Global.return_single_char_box": False,
            "Det.model_path": str(profile["det"]["path"]),
            "Rec.model_path": str(profile["rec"]["path"]),
            "Det.ocr_version": OCRVersion(profile["det"]["ocrVersion"]),
            "Rec.ocr_version": OCRVersion(profile["rec"]["ocrVersion"]),
            "Det.model_type": ModelType(profile["det"]["modelType"]),
            "Rec.model_type": ModelType(profile["rec"]["modelType"]),
            "EngineConfig.onnxruntime.intra_op_num_threads": threads,
            "EngineConfig.onnxruntime.inter_op_num_threads": profile["cpu"]["interOpThreads"],
            "EngineConfig.onnxruntime.use_cuda": False,
            "EngineConfig.onnxruntime.use_dml": False,
            "EngineConfig.onnxruntime.use_cann": False,
        }
        reused = cache is not None and cache.get("identity") == profile["modelIdentity"]
        if reused:
            engine = cache["engine"]
            stages["load"] = 0
        else:
            if cache is not None:
                cache.clear()  # Release the previous model before loading another.
            started = time.perf_counter()
            try:
                engine = RapidOCR(params=params)
                # Models are lazy in RapidOCR 3.10; load both even for a blank image.
                engine._load_det_model()
                engine._load_rec_model()
            except OcrFailure:
                raise
            except Exception:
                raise OcrFailure("model_incompatible") from None
            stages["load"] = (time.perf_counter() - started) * 1000
            if cache is not None:
                cache.update(identity=profile["modelIdentity"], engine=engine)
        started = time.perf_counter()
        try:
            result = engine(image)
        except OcrFailure:
            raise
        except Exception:
            raise OcrFailure("inference_failed") from None
        stages["inference"] = (time.perf_counter() - started) * 1000
        started = time.perf_counter()
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
        stages["formatResult"] = (time.perf_counter() - started) * 1000
    families = "+".join(dict.fromkeys(profile[task]["ocrVersion"] for task in ("det", "rec")))
    return {"ok": True, "engine": "PaddleOCR " + families, "language": profile["language"],
            "modelVersion": profile["modelVersion"], "profileId": profile["id"],
            "modelIdentity": profile["modelIdentity"], "stageMs": stages, "workerReused": reused, "lines": lines}


def process_request(root: Path, data: bytes | None = None, cache: dict | None = None, self_check: bool = False) -> tuple[bytes, int]:
    identifier = None
    try:
        started = time.perf_counter()
        if cache is not None:
            try:
                identifier = json.loads(data)["requestId"]
                if not isinstance(identifier, str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,64}", identifier):
                    identifier = None
                    raise ValueError
            except (ValueError, TypeError, KeyError):
                raise OcrFailure("invalid_request") from None
        png, profile_path = (None, None) if self_check else read_request(data, cache is not None)
        read_ms = (time.perf_counter() - started) * 1000
        verify_started = time.perf_counter()
        profile = selected_profile(root, profile_path)
        verify_ms = (time.perf_counter() - verify_started) * 1000
        reply = infer(root, profile, png, cache)
        reply["stageMs"].update(readRequest=read_ms, verifyModels=verify_ms, total=(time.perf_counter() - started) * 1000)
        reply["stageMs"]["overhead"] = max(0, reply["stageMs"]["total"] - sum(value for key, value in reply["stageMs"].items() if key != "total"))
        if self_check:
            reply["runtimeReady"] = True
        if identifier is not None:
            reply["requestId"] = identifier
        output = json.dumps(reply, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
        if len(output) > MAX_OUTPUT_BYTES:
            raise OcrFailure("output_too_large")
        code = 0
    except OcrFailure as error:
        reply = {"ok": False, "errorCode": error.code}
        if identifier is not None:
            reply["requestId"] = identifier
        output = json.dumps(reply, separators=(",", ":")).encode("ascii")
        code = 1
    except Exception:
        reply = {"ok": False, "errorCode": "internal_error"}
        if identifier is not None:
            reply["requestId"] = identifier
        output = json.dumps(reply, separators=(",", ":")).encode("ascii")
        code = 1
    return output, code


def main() -> int:
    root = Path(__file__).resolve().parent
    arguments = sys.argv[1:]
    if arguments == ["--worker"]:
        cache = {}
        while True:
            data = sys.stdin.buffer.readline(MAX_INPUT_BYTES + 1)
            if not data:
                return 0
            output, _ = process_request(root, data, cache)
            # No exception messages, image bytes, or filesystem paths are logged.
            os.write(1, output + b"\n")
            if len(data) > MAX_INPUT_BYTES:
                return 1
            del data, output  # Retain model state only, never request/transcript history.
    if arguments not in ([], ["--self-check"]):
        os.write(1, b'{"ok":false,"errorCode":"invalid_arguments"}\n')
        return 1
    output, code = process_request(root, self_check=bool(arguments))
    os.write(1, output + b"\n")
    return code


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    raise SystemExit(main())
