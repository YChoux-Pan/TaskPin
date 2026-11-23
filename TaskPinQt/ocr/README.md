# Local PaddleOCR runtime

Build on Windows x64 with a build-time Python 3.11 or newer. End users need
neither Python nor a network connection. The bootstrap interpreter only uses
its standard library; no global pip packages are installed or changed.

```powershell
./TaskPinQt/scripts/prepare-ocr-runtime.ps1 -OutputDir C:/build/taskpin/ocr -BootstrapPython C:/Python312/python.exe
```

`-CacheDir C:/build/ocr-download-cache` reuses SHA-256 verified downloads.
`-Offline` forbids downloads and requires a complete cache. Running again
verifies the existing runtime's file inventory and offline model self-check.
Changed or damaged managed output is rebuilt from the pinned artifacts.
An existing nonempty directory without the builder's receipt is never replaced.
The output path must be absolute; cache and output must be separate directories.
The builder needs Windows' built-in `expand.exe` to read Microsoft's runtime CAB.
It does not execute or install the downloaded VC Redistributable.

Copy the **entire output directory** into the application's `ocr/` directory:

```text
ocr/python.exe
ocr/python312.zip, python312.dll, python312._pth, app-local MSVC DLLs, ...
ocr/helper.py
ocr/runtime-lock.json
ocr/taskpin-ocr-runtime.json
ocr/Lib/site-packages/...
ocr/models/ch_PP-OCRv5_det_mobile.onnx
ocr/models/ch_PP-OCRv5_rec_mobile.onnx
ocr/models/ch_PP-OCRv4_det_mobile.onnx
ocr/models/ch_PP-OCRv4_rec_mobile.onnx
ocr/LICENSE.txt
ocr/licenses/...
```

The one-shot protocol launches `python.exe -I -B helper.py`, writes one UTF-8
JSON request to stdin, and closes stdin. The helper writes exactly one UTF-8
JSON object plus a newline to stdout and exits. It accepts
`{ "image": "<base64 PNG>" }`, optionally with a local `profilePath` or a bundled `profileId` (mutually exclusive).
Bundled IDs are `builtin-ppocrv5-mobile` (default) and `builtin-ppocrv4-mobile`
(lightweight). The latter returns `PaddleOCR PP-OCRv4` metadata and uses the
independently hash-pinned v4 weights; it never falls back to v5.
Successful output includes these fields:

```json
{"ok":true,"engine":"PaddleOCR PP-OCRv5","language":"multi","modelVersion":"PP-OCRv5_mobile/RapidOCR-v3.10.0","lines":[{"text":"Example","x":10,"y":20,"width":100,"height":24,"confidence":0.97}]}
```

The sample confidence above illustrates the schema. Actual values come directly
from the recognition model. Rectangles bound the detected quadrilateral in the
original image's physical pixels. The Chinese PP-OCRv5 recognition model covers
Chinese and English text; `multi` describes this shared model and is not a
promise to recognize every language. The angle classifier is disabled, so
upright text is the intended input. RapidOCR may split a visual row into multiple
detected text regions. It uses its normal detection and recognition thresholds.

Limits are 16,000,000 image pixels, 16,384 pixels per dimension, 64 MiB PNG bytes,
90 MiB input JSON, 512 regions, 32,000 Unicode characters, and 1 MiB output JSON.
Excess output fails instead of silently truncating text. Empty images return
`lines: []`. Recognition uses two intra-operation CPU threads and one
inter-operation thread, with OpenCV/BLAS/OpenMP also capped at two threads.
The application uses `--worker`: one JSON request/reply per line, with a
`requestId` echoed in each reply. A shared Qt queue serves notes and translation
with one worker and at most one cached model. Cancelling a queued owner removes
its request; cancelling/closing the active owner kills that worker and restarts
for the next owner. Responses are accepted only for the current request. The
application allows up to eight waiting requests and releases an idle worker after
60 seconds. Each active request times out after 90 seconds. The one-shot protocol
and `--self-check` remain available for deployment checks.

Successful replies report `profileId`, `modelIdentity` (SHA256 of model hashes,
family/type and CPU options), `workerReused`, and `stageMs`. Timings use a monotonic
clock and include `readRequest`, `verifyModels`, dependency `import`, `prepareImage`,
model `load`, `inference`, `formatResult`, `overhead`, and helper `total`. A warm
request reports `load: 0`. Helper `total` excludes interpreter startup,
worker pipe waiting/reading and JSON output
serialization. Qt adds `queue`, `process`, `requestTotal` and `bridgeOverhead`
(`process` minus helper `total`, bounded at zero). Cold-start
and inference times depend on the machine, input and CPU contention.

All input/recognition data stays in memory. The helper never saves
images or text, emits no dependency logs, verifies both model hashes before
loading, disables RapidOCR's downloader, and blocks Python socket connections
and name lookup. No API keys, cloud service, automatic model download, or font
download is used. `-I -B` isolates user Python configuration and bytecode writes.

Failure is a nonzero exit with `{"ok":false,"errorCode":"..."}`. Stable codes:
`invalid_request`, `invalid_image`, `input_too_large`, `image_too_large`,
`model_missing`, `model_hash_mismatch`, `runtime_incomplete`,
`runtime_incompatible`, `runtime_unavailable`, `network_disabled`,
`inference_failed`, `invalid_model_output`, `output_too_large`,
`profile_missing`, `profile_invalid`, `profile_unsupported`, `model_incompatible`,
`invalid_arguments`, and `internal_error`. No user contents or exception strings
are written to stderr.

## Local model profiles

Settings → Screenshot translation selects bundled PP-OCRv5 mobile (20.5 MiB),
bundled lightweight PP-OCRv4 mobile (14.9 MiB), or a custom local JSON file.
The lightweight weights occupy about 27% less disk space; this is not a
guarantee of faster inference or equal recognition quality on every image.
Both bundled profiles are included in the portable package and require no
download or configuration file. Existing selections are preserved. Both notes OCR and screenshot translation use this
selection. `ocr.json` in the isolated TaskPin data directory stores only
`schemaVersion: 1`, `profile` (`builtin-ppocrv5-mobile`, `builtin-ppocrv4-mobile`, or `custom`) and
`customProfilePath`. Editing settings is a draft until Save; Cancel preserves
the previous selection. A malformed saved selection fails visibly.

A custom profile uses this schema (replace paths and hashes with actual files):

```json
{
  "schemaVersion": 1,
  "id": "my-local-v5",
  "modelVersion": "PP-OCRv5 local export 2026-11",
  "language": "multi",
  "det": {
    "path": "models/det.onnx",
    "sha256": "<64 hexadecimal characters>",
    "ocrVersion": "PP-OCRv5",
    "modelType": "mobile"
  },
  "rec": {
    "path": "models/rec.onnx",
    "sha256": "<64 hexadecimal characters>",
    "ocrVersion": "PP-OCRv5",
    "modelType": "mobile"
  },
  "cpu": {"intraOpThreads": 2, "interOpThreads": 1}
}
```

Paths are absolute or relative to the profile file. Only ONNX files are accepted.
Each task declares its own family (`PP-OCRv4` or `PP-OCRv5`) and type (`mobile` or
`server`). These are the RapidOCR 3.10 options exposed by this schema; they do not
guarantee compatibility of arbitrary ONNX exports. Recognition models must carry
the character dictionary expected by RapidOCR in ONNX metadata. Incompatible
weights or missing dictionaries fail; no downloads or fallback are attempted.
`language` describes output metadata (`multi`, `zh`, `en`, `zh+en`), rather than
changing model weights. `id` is 1–64 ASCII letters/digits/dot/underscore/hyphen,
starting with a letter or digit; `modelVersion` is a nonempty label up to 128
characters. Optional CPU values are integer intra-operation threads 1–8 and
inter-operation threads 1–4 (defaults 2 and 1). Unknown fields/options are rejected.
The settings UI checks profile syntax and starts a cancellable helper process to
verify SHA256 and actually load the selected ONNX models before saving. The GUI
does not hash large weights. The inference helper repeats verification before every
request, including warm reuse. A profile hash is an integrity check supplied by
the model owner, not an independent trust/signature claim. New model families
require a tested schema/runtime update; changing a label cannot add engine support.

Custom weights are user supplied and are not bundled, downloaded, or covered by
the bundled model receipt. Keep their upstream attribution and license with them.

Offline model self-check (also run by the builder):

```powershell
C:/build/taskpin/ocr/python.exe -I -B C:/build/taskpin/ocr/helper.py --self-check
```

Behavioral fixture checks (also run by `scripts/test.ps1`):

```powershell
C:/build/taskpin/ocr/python.exe -I -B ./TaskPinQt/ocr/test_helper.py --runtime C:/build/taskpin/ocr
```

The self-check verifies hashes, imports, and loads all four local ONNX models.
`builtinProfilesVerified` names both checked profiles. The builder and packaging
step require both profiles to pass before producing a release.
The behavioral test creates in-memory English/Chinese/dark technical fixtures,
checks both bundled profiles, worker switching and reuse, recognized phrases, real confidence, coordinates, invalid-input handling,
and model tamper rejection. It writes no screenshots or recognition transcripts.
English fixtures default to Windows Arial; Chinese fixtures use Microsoft YaHei.
On a CI host without Chinese fonts, pass `--font C:/fonts/NotoSansCJK-Regular.ttc`
with an available CJK/Latin TTF, OTF, or TTC font. Fonts are used only to create
synthetic test images and are not part of the OCR runtime or model inference.
Repository source contains manifests and scripts only; binary artifacts remain
in the chosen output/cache directories. Dependency and model attributions are
in `licenses/PADDLEOCR_RUNTIME_NOTICE.md` and each generated runtime's licenses.


## Model folders (TaskPin 0.4.5)

Settings → Screenshot translation → Local OCR offers built-in PP-OCRv6 tiny as
well as v4/v5. The v6 preset uses max-side 960, four intra-op threads and one
inter-op thread. Existing selections remain unchanged on upgrade.

The application scans its sibling `ocr-models/` directory, one pair per immediate
subfolder. Canonical filenames such as `PP-OCRv6_det_small.onnx` and
`PP-OCRv6_rec_small.onnx` need no JSON. Restart or press Refresh to discover them.
Settings performs discovery in a separate process and actual model loading only
when saving a folder/custom selection. Detailed examples and optional model.json
format are in [OCR-MODELS.md](../../docs/OCR-MODELS.md).

`helper.py --catalog --model-dir ABS_DIRECTORY` reports bounded metadata-only
candidates with stable `local-<64 hex>` IDs. A folder request carries `profileId`
and `modelDirectory`; these are mutually exclusive with `profilePath`. Each real
request resolves the package, hashes its selected weights, checks declared hashes
when present, and includes options/hash values in `modelIdentity`. Replacing bytes
loads a new engine; a removed selected package fails visibly without fallback.

Custom JSON also accepts optional `detector` (`limitType`: min/max,
`limitSideLen`: integer 32–8192), optional per-weight `language` (ch/en/multi/zh),
and PP-OCRv6 tiny/small/medium. Existing custom hashes remain required. Folder
`model.json` hashes are optional: actual bytes are always hashed before loading,
so replacement changes the cache identity; an optional declared hash is enforced.

All six built-in weights are hash-pinned and loaded by self-check. Supported
families/types are adapter capabilities, not a promise for arbitrary ONNX files
or future architectures. Recognition models must include a compatible embedded
character dictionary. No model-provided scripts run and inference never downloads.
