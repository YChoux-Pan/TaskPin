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


def request(image: Image.Image) -> bytes:
    stream = io.BytesIO()
    image.save(stream, format="PNG")
    return json.dumps({"image": base64.b64encode(stream.getvalue()).decode("ascii")}).encode("utf-8")


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
                        "processSeconds": round(seconds, 3)})

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
