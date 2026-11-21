"""Build a self-contained Windows x64 OCR runtime from hash-pinned artifacts.

Only Python's standard library and Windows expand.exe are used at build time.
No pip install, system installation, or execution of downloaded installers occurs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid
import zipfile

SOURCE = Path(__file__).resolve().parent
MARKER = "taskpin-ocr-runtime.json"


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def fetch(record: dict, cache: Path, offline: bool) -> Path:
    target = cache / record["filename"]
    if target.is_file() and digest(target) == record["sha256"]:
        return target
    if offline:
        raise RuntimeError("Missing or corrupt cached artifact: " + record["filename"])
    temporary = cache / (record["filename"] + "." + uuid.uuid4().hex + ".part")
    print("Downloading " + record["filename"], flush=True)
    try:
        for attempt in range(3):
            try:
                request = urllib.request.Request(record["url"], headers={"User-Agent": "TaskPin-OCR-Runtime-Builder/1"})
                with urllib.request.urlopen(request, timeout=60) as reply, temporary.open("wb") as stream:
                    shutil.copyfileobj(reply, stream, 1024 * 1024)
                break
            except (OSError, TimeoutError):
                if attempt == 2:
                    raise
                time.sleep(attempt + 1)
        if digest(temporary) != record["sha256"]:
            raise RuntimeError("SHA-256 mismatch: " + record["filename"])
        temporary.replace(target)
        return target
    finally:
        temporary.unlink(missing_ok=True)


def unzip(archive: Path, destination: Path, wheel: bool = False) -> None:
    with zipfile.ZipFile(archive) as zipped:
        for member in zipped.infolist():
            relative = PurePosixPath(member.filename)
            if relative.is_absolute() or ".." in relative.parts or ":" in member.filename or "\\" in member.filename:
                raise RuntimeError("Unsafe archive path")
            # RapidOCR's wheel contains v6/angle models. This runtime explicitly
            # distributes only its two pinned PaddleOCR v5 models below.
            if wheel and relative.parts[:2] == ("rapidocr", "models"):
                continue
            parts = relative.parts
            if wheel and len(parts) >= 2 and parts[0].endswith(".data"):
                if parts[1] not in ("purelib", "platlib"):
                    continue  # Console launchers and headers are not needed.
                relative = PurePosixPath(*parts[2:])
            target = destination.joinpath(*relative.parts)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with zipped.open(member) as source, target.open("wb") as stream:
                shutil.copyfileobj(source, stream)


def expand(cabinet: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    command = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "expand.exe"
    result = subprocess.run([str(command), "-F:*", str(cabinet), str(destination)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode:
        raise RuntimeError("Windows CAB extraction failed")


def copy_vc_runtime(artifact: Path, record: dict, staging: Path, scratch: Path) -> None:
    """Extract unmodified desktop DLLs and EULA from Microsoft's signed redist.

    The pinned WiX bundle contains two CAB containers. expand.exe extracts
    them without executing either the EXE or its MSI installer packages.
    """
    payload = artifact.read_bytes()
    extracted = []
    for index, offset in enumerate(record["cabOffsets"]):
        if payload[offset:offset + 4] != b"MSCF":
            raise RuntimeError("Unexpected VC runtime container")
        size = struct.unpack_from("<I", payload, offset + 8)[0]
        if size < 36 or offset + size > len(payload):
            raise RuntimeError("Invalid VC runtime container size")
        cabinet = scratch / ("vc-container-" + str(index) + ".cab")
        cabinet.write_bytes(payload[offset:offset + size])
        folder = scratch / ("vc-container-" + str(index))
        expand(cabinet, folder)
        extracted.append(folder)
    minimum = scratch / "vc-minimum"
    expand(extracted[1] / record["minimumCabPayload"], minimum)
    for dll in record["dlls"]:
        source = minimum / dll["source"]
        if digest(source) != dll["sha256"]:
            raise RuntimeError("VC runtime DLL hash mismatch")
        shutil.copyfile(source, staging / dll["filename"])
    licenses = staging / "licenses" / "Microsoft-VC-Runtime"
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(extracted[0] / record["licensePayload"], licenses / "license.rtf")
    shutil.copyfile(extracted[0] / record["licenseZhPayload"], licenses / "license-zh-CN.rtf")


def collect_licenses(staging: Path, lock: dict) -> None:
    packages = staging / "Lib" / "site-packages"
    central = staging / "licenses" / "Python-Packages"
    names = ("license", "licence", "notice", "copying", "copyright")
    collected = []
    for path in sorted(packages.rglob("*")):
        if path.is_file() and any(term in path.name.lower() for term in names):
            relative = path.relative_to(packages)
            target = central / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            collected.append(relative.as_posix())
    repo_licenses = SOURCE.parents[1] / "licenses"
    for name in ("PADDLEOCR_RUNTIME_NOTICE.md", "PaddleOCR-Apache-2.0.txt"):
        shutil.copyfile(repo_licenses / name, staging / "licenses" / name)
    inventory = {"packages": [{"name": item["name"], "version": item["version"],
                               "license": item["license"]} for item in lock["wheels"]],
                 "files": collected,
                 "pythonLicense": "../LICENSE.txt",
                 "vcRuntimeLicense": "Microsoft-VC-Runtime/license.rtf",
                 "modelLicense": "PaddleOCR-Apache-2.0.txt"}
    (staging / "licenses" / "INDEX.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")


def self_check(root: Path) -> None:
    environment = os.environ.copy()
    # The embedded interpreter must succeed with no user Python or DLLs on PATH.
    environment["PATH"] = str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32")
    environment.pop("PYTHONPATH", None)
    environment.pop("PYTHONHOME", None)
    result = subprocess.run([str(root / "python.exe"), "-I", "-B", str(root / "helper.py"), "--self-check"],
                            capture_output=True, timeout=90, env=environment, cwd=root,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        reply = json.loads(result.stdout)
        valid = result.returncode == 0 and reply.get("runtimeReady") is True and not result.stderr
    except ValueError:
        valid = False
    if not valid:
        raise RuntimeError("Prepared runtime failed its offline self-check")


def verify_existing(output: Path, source_hash: str) -> bool:
    marker = output / MARKER
    if not marker.is_file():
        return False
    try:
        receipt = json.loads(marker.read_text(encoding="utf-8"))
        if receipt["sourceHash"] != source_hash:
            return False
        for relative, expected in receipt["files"].items():
            path = output.joinpath(*PurePosixPath(relative).parts)
            if not path.is_file() or digest(path) != expected:
                return False
        self_check(output)
        return True
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired):
        return False


def build(args: argparse.Namespace) -> None:
    if os.name != "nt" or struct.calcsize("P") != 8:
        raise RuntimeError("A Windows x64 bootstrap Python is required")
    if not args.output_dir.is_absolute():
        raise RuntimeError("--output-dir must be an absolute path")
    output = args.output_dir.resolve()
    if output == output.parent or output == SOURCE or SOURCE.is_relative_to(output):
        raise RuntimeError("Unsafe output directory")
    cache = args.cache_dir.resolve() if args.cache_dir else output.parent / "ocr-download-cache"
    if cache == output or cache.is_relative_to(output):
        raise RuntimeError("Cache must be outside the output directory")
    lock_path = SOURCE / "runtime-lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    source_digest = hashlib.sha256()
    for path in (lock_path, SOURCE / "helper.py", SOURCE / "build_runtime.py",
                 SOURCE.parents[1] / "licenses" / "PADDLEOCR_RUNTIME_NOTICE.md",
                 SOURCE.parents[1] / "licenses" / "PaddleOCR-Apache-2.0.txt"):
        source_digest.update(path.read_bytes())
    for notice in lock.get("licenseSupplements", []):
        path = SOURCE / notice["filename"]
        if digest(path) != notice["sha256"]:
            raise RuntimeError("Supplemental license hash mismatch")
        source_digest.update(path.read_bytes())
    source_hash = source_digest.hexdigest()
    if output.is_dir() and verify_existing(output, source_hash):
        print("Verified existing OCR runtime: " + str(output), flush=True)
        return
    if output.exists() and (not output.is_dir() or (any(output.iterdir()) and not (output / MARKER).is_file())):
        raise RuntimeError("Refusing to replace a directory not created by this builder")
    cache.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    all_artifacts = [lock["python"], lock["vcRuntime"], *lock["wheels"], *lock["models"]]
    cached = {item["filename"]: fetch(item, cache, args.offline) for item in all_artifacts}
    with tempfile.TemporaryDirectory(prefix="taskpin-ocr-build-", dir=output.parent) as working:
        scratch = Path(working)
        staging = scratch / "runtime"
        staging.mkdir()
        unzip(cached[lock["python"]["filename"]], staging)
        site_packages = staging / "Lib" / "site-packages"
        site_packages.mkdir(parents=True)
        for wheel in lock["wheels"]:
            unzip(cached[wheel["filename"]], site_packages, wheel=True)
        for notice in lock.get("licenseSupplements", []):
            target = site_packages / notice["destination"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(SOURCE / notice["filename"], target)
        # This dedicated path file is isolated from user site-packages and PYTHONPATH.
        (staging / "python312._pth").write_text("python312.zip\n.\nLib/site-packages\nimport site\n", encoding="ascii")
        copy_vc_runtime(cached[lock["vcRuntime"]["filename"]], lock["vcRuntime"], staging, scratch)
        (staging / "models").mkdir()
        for model in lock["models"]:
            shutil.copyfile(cached[model["filename"]], staging / "models" / model["filename"])
        shutil.copyfile(lock_path, staging / "runtime-lock.json")
        shutil.copyfile(SOURCE / "helper.py", staging / "helper.py")
        collect_licenses(staging, lock)
        self_check(staging)
        receipt = {"schemaVersion": 1, "sourceHash": source_hash,
                   "modelVersion": lock["modelVersion"], "files": {}}
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                receipt["files"][path.relative_to(staging).as_posix()] = digest(path)
        (staging / MARKER).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        # Only a managed previous output is removed; failed builds leave it intact.
        previous = scratch / "previous-runtime"
        if output.exists():
            output.rename(previous)
        try:
            staging.rename(output)
        except BaseException:
            if previous.exists():
                previous.rename(output)
            raise
    size = sum(path.stat().st_size for path in output.rglob("*") if path.is_file())
    print("Prepared offline OCR runtime: " + str(output), flush=True)
    print("Runtime size: " + str(round(size / 1024 / 1024, 1)) + " MiB", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--cache-dir", type=Path)
    parser.add_argument("--offline", action="store_true", help="Use only hash-verified cached downloads")
    try:
        build(parser.parse_args())
    except Exception as error:
        print("OCR runtime preparation failed: " + str(error), file=sys.stderr)
        raise SystemExit(1)
