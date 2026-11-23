"""Behavioral checks with synthetic in-memory images; no user data or cloud API."""
from __future__ import annotations

import argparse
import base64
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import threading
import queue
import tempfile
import time

from PIL import Image, ImageDraw, ImageFont


def call(runtime: Path, request: bytes, helper: Path | None = None) -> tuple[dict, float]:
    environment = os.environ.copy()
    environment["PATH"] = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32")
    environment.pop("PYTHONPATH", None)
    environment.pop("PYTHONHOME", None)
    started = time.perf_counter()
    process = subprocess.run([str(runtime / "python.exe"), "-I", "-B", str(helper or runtime / "helper.py")],
                             input=request, capture_output=True, env=environment,
                             cwd=runtime, timeout=90,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    elapsed = time.perf_counter() - started
    assert not process.stderr, "Helper emitted diagnostic contents"
    assert len(process.stdout) <= 1024 * 1024, "Oversized output"
    reply = json.loads(process.stdout.decode("utf-8"))
    assert (process.returncode == 0) == reply["ok"], "Exit code disagrees with JSON"
    return reply, elapsed


def request(image: Image.Image, profile: Path | None = None) -> bytes:
    stream = io.BytesIO()
    image.save(stream, format="PNG")
    payload = {"image": base64.b64encode(stream.getvalue()).decode("ascii")}
    if profile is not None:
        payload["profilePath"] = str(profile)
    return json.dumps(payload).encode("utf-8")


def check_worker(runtime: Path, image: Image.Image, profile_path: Path, profile: dict) -> list[dict]:
    worker = subprocess.Popen([str(runtime / "python.exe"), "-I", "-B", str(runtime / "helper.py"), "--worker"],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              cwd=runtime, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    output = queue.Queue()
    def receive():
        for line in worker.stdout:
            output.put(line)
        output.put(None)
    threading.Thread(target=receive, daemon=True).start()
    def exchange(identifier: str, path: Path | None = None) -> dict:
        payload = json.loads(request(image, path));payload["requestId"] = identifier
        try:
            worker.stdin.write(json.dumps(payload).encode() + b"\n");worker.stdin.flush()
        except OSError:
            raise AssertionError("Helper did not accept persistent worker requests") from None
        line = output.get(timeout=90)
        assert line is not None, "Worker exited before replying"
        reply = json.loads(line)
        assert reply.get("requestId") == identifier, "Worker did not match the request"
        return reply
    try:
        cold = exchange("cold");warm = exchange("warm")
        assert cold["ok"] and warm["ok"] and cold["lines"] == warm["lines"]
        assert cold["stageMs"]["load"] > 0 and warm["stageMs"]["load"] == 0
        assert warm["workerReused"] is True and cold["workerReused"] is False
        switched = json.loads(json.dumps(profile));switched["cpu"]["intraOpThreads"] = 1
        profile_path.write_text(json.dumps(switched), encoding="utf-8")
        custom = exchange("switch", profile_path)
        assert custom["ok"] and custom["profileId"] == "custom-v5-fixture"
        assert custom["stageMs"]["load"] > 0 and not custom["workerReused"]
        missing = exchange("missing", profile_path.parent / "absent.json")
        assert missing == {"ok": False, "errorCode": "profile_missing", "requestId": "missing"}
        after_failure = exchange("after-failure", profile_path)
        assert after_failure["ok"] and after_failure["workerReused"]
        return [{"fixture": "worker-" + name, "matched": True, "stageMs": reply["stageMs"]}
                for name, reply in (("cold", cold), ("warm", warm), ("switch", custom))]
    finally:
        try:
            worker.stdin.close()
        except OSError:
            pass
        worker.wait(timeout=20)
        assert not worker.stderr.read(), "Worker emitted diagnostics"
        worker.stdout.close();worker.stderr.close()


def fixture(lines: list[str], dark: bool = False, font_path: Path | None = None) -> Image.Image:
    image = Image.new("RGB", (1100, 80 + 65 * len(lines)), "#171b22" if dark else "white")
    if font_path is None:
        fonts = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "Fonts"
        chinese = any(ord(character) > 127 for line in lines for character in line)
        font_path = fonts / ("msyh.ttc" if chinese else "arial.ttf")
    if not font_path.is_file():
        raise RuntimeError("Fixture font unavailable; pass --font with a Chinese/Latin font such as Noto Sans CJK")
    font = ImageFont.truetype(str(font_path), 30)
    draw = ImageDraw.Draw(image)
    for index, line in enumerate(lines):
        draw.text((40, 35 + index * 65), line, font=font, fill="#e8eaf0" if dark else "#171b22")
    return image


def check_regions(reply: dict, image: Image.Image) -> None:
    assert reply["ok"] and reply["engine"] == "PaddleOCR PP-OCRv5"
    assert reply["modelVersion"] == "PP-OCRv5_mobile/RapidOCR-v3.10.0"
    assert reply["language"] == "multi"
    assert reply.get("profileId") == "builtin-ppocrv5-mobile", "Helper did not report selected profile"
    assert reply.get("modelIdentity", "").startswith("sha256:")
    stages = reply.get("stageMs", {})
    for stage in ("import", "load", "inference", "verifyModels", "prepareImage", "formatResult", "total"):
        assert isinstance(stages[stage], (float, int)) and stages[stage] >= 0
    assert stages["total"] >= sum(stages[stage] for stage in ("import", "load", "inference", "verifyModels", "prepareImage", "formatResult"))
    assert 1 <= len(reply["lines"]) <= 512
    for line in reply["lines"]:
        assert isinstance(line["confidence"], (float, int)) and 0 < line["confidence"] <= 1
        assert all(isinstance(line[key], int) for key in ("x", "y", "width", "height"))
        assert 0 <= line["x"] < image.width and 0 <= line["y"] < image.height
        assert 0 < line["width"] <= image.width - line["x"]
        assert 0 < line["height"] <= image.height - line["y"]
        # All generated text starts well away from (0,0); this catches scaled or
        # normalized coordinate responses masquerading as physical pixels.
        assert line["x"] >= 20 and line["y"] >= 20


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--font", type=Path, help="Optional CJK/Latin TTF/OTF/TTC fixture font (e.g. Noto Sans CJK)")
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    results = []
    for name, lines, expected, dark in [
        ("english", ["Hello TaskPin 2026", "Keep the original text."], ["HelloTaskPin2026", "originaltext"], False),
        ("chinese", ["请确认翻译结果", "保留原始文字"], ["确认翻译结果", "保留原始文字"], False),
        ("dark-technical", ["const maxRetries = 3;", "retry failed requests", "离线文字识别"],
         ["maxRetries=3", "retryfailedrequests", "离线文字识别"], True),
    ]:
        image = fixture(lines, dark, args.font)
        reply, seconds = call(runtime, request(image))
        check_regions(reply, image)
        normalized = "".join(item["text"] for item in reply["lines"]).replace(" ", "")
        assert all(phrase in normalized for phrase in expected), "Expected synthetic phrase was not recognized: " + name
        assert any(item["confidence"] < 1 for item in reply["lines"]), "Confidences unexpectedly all perfect"
        results.append({"fixture": name, "regions": len(reply["lines"]), "matched": True,
                        "minimumConfidence": min(item["confidence"] for item in reply["lines"]),
                        "processSeconds": round(seconds, 3), "stageMs": reply["stageMs"]})

    # A user-selected local profile must actually drive the engine and metadata.
    # Copy only JSON: weights stay in their existing, verified runtime location.
    with tempfile.TemporaryDirectory(prefix="taskpin-ocr-profiles-") as temporary:
        profile_path = Path(temporary) / "custom.json"
        lock = json.loads((runtime / "runtime-lock.json").read_text(encoding="utf-8"))
        profile = {"schemaVersion": 1, "id": "custom-v5-fixture", "modelVersion": "fixture-v5-local",
                   "language": "multi", "cpu": {"intraOpThreads": 2, "interOpThreads": 1}}
        for model in lock["models"]:
            profile[model["task"]] = {"path": str(runtime / "models" / model["filename"]),
                                     "sha256": model["sha256"], "ocrVersion": "PP-OCRv5", "modelType": "mobile"}
        # Windows editors/PowerShell can write a UTF-8 BOM; it must select the
        # same local profile accepted by Qt instead of failing only at runtime.
        profile_path.write_text(json.dumps(profile), encoding="utf-8-sig")
        custom_image = fixture(["Hello TaskPin 2026"], font_path=args.font)
        reply, seconds = call(runtime, request(custom_image, profile_path))
        assert reply["ok"] and reply["profileId"] == "custom-v5-fixture"
        assert reply["modelVersion"] == "fixture-v5-local"
        assert "HelloTaskPin2026" in "".join(line["text"] for line in reply["lines"]).replace(" ", "")
        assert reply["stageMs"]["inference"] > 0
        results.append({"fixture": "custom-v5-local", "matched": True, "processSeconds": round(seconds, 3), "stageMs": reply["stageMs"]})
        results.extend(check_worker(runtime, custom_image, profile_path, profile))
        for mutate, expected in [
            (lambda p: p["det"].update(sha256="0" * 64), "model_hash_mismatch"),
            (lambda p: p["rec"].update(path=str(Path(temporary) / "missing.onnx")), "model_missing"),
            (lambda p: p["rec"].update(ocrVersion="PP-OCRv99"), "profile_unsupported"),
            (lambda p: p["cpu"].update(intraOpThreads=64), "profile_unsupported"),
            (lambda p: p.update(downloadUrl="https://example.invalid/model.onnx"), "profile_invalid"),
        ]:
            invalid = json.loads(json.dumps(profile));mutate(invalid)
            profile_path.write_text(json.dumps(invalid), encoding="utf-8")
            reply, _ = call(runtime, request(custom_image, profile_path))
            assert reply == {"ok": False, "errorCode": expected}, (expected, reply)
        reply, _ = call(runtime, request(custom_image, Path(temporary) / "missing.json"))
        assert reply == {"ok": False, "errorCode": "profile_missing"}
        # Correct hashes do not make arbitrary bytes a compatible OCR model.
        import hashlib
        broken_model = Path(temporary) / "broken.onnx"
        broken_model.write_bytes(b"generated-not-an-onnx-model")
        incompatible = json.loads(json.dumps(profile))
        incompatible["det"].update(path=str(broken_model), sha256=hashlib.sha256(broken_model.read_bytes()).hexdigest())
        profile_path.write_text(json.dumps(incompatible), encoding="utf-8")
        reply, _ = call(runtime, request(custom_image, profile_path))
        assert reply == {"ok": False, "errorCode": "model_incompatible"}

    # Boundary image is valid and blank. It must complete with an empty result.
    blank = Image.new("RGB", (4000, 4000), "white")
    reply, seconds = call(runtime, request(blank))
    assert reply["ok"] and reply["lines"] == []
    results.append({"fixture": "16-million-pixel-blank", "matched": True, "processSeconds": round(seconds, 3)})
    del blank

    malformed = [
        (b"not JSON: synthetic-sensitive-content", "invalid_request"),
        (b'{"image":"@not-base64"}', "invalid_request"),
        (json.dumps({"image": base64.b64encode(b"not a PNG").decode()}).encode(), "invalid_image"),
    ]
    large_header = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 4001, 4000) + b"\x08\x02\x00\x00\x00" + b"\0" * 4
    malformed.append((json.dumps({"image": base64.b64encode(large_header).decode()}).encode(), "image_too_large"))
    for data, code in malformed:
        reply, _ = call(runtime, data)
        assert reply == {"ok": False, "errorCode": code}
        assert "synthetic-sensitive-content" not in json.dumps(reply)

    # Only two small models and source/config are copied for the rejection test.
    with tempfile.TemporaryDirectory(prefix="taskpin-ocr-tamper-") as temporary:
        altered = Path(temporary)
        shutil.copyfile(runtime / "helper.py", altered / "helper.py")
        shutil.copyfile(runtime / "runtime-lock.json", altered / "runtime-lock.json")
        (altered / "models").mkdir()
        for model in (runtime / "models").glob("*.onnx"):
            shutil.copyfile(model, altered / "models" / model.name)
        detector = altered / "models" / "ch_PP-OCRv5_det_mobile.onnx"
        with detector.open("ab") as stream:
            stream.write(b"synthetic-model-tamper")
        reply, _ = call(runtime, request(Image.new("RGB", (100, 100), "white")), altered / "helper.py")
        assert reply == {"ok": False, "errorCode": "model_hash_mismatch"}
        detector.unlink()
        reply, _ = call(runtime, request(Image.new("RGB", (100, 100), "white")), altered / "helper.py")
        assert reply == {"ok": False, "errorCode": "model_missing"}

    assert not list(runtime.rglob("__pycache__")), "Runtime wrote Python bytecode"
    print(json.dumps({"ok": True, "fixtures": results, "invalidInputCases": len(malformed),
                      "tamperAndMissingModelRejected": True,
                      "networkInference": False, "imageOrTranscriptFilesWritten": False}, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
