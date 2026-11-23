# PaddleOCR v6 tiny and model folder discovery

User intent: add the measured PP-OCRv6 tiny model and allow dropping compatible model files into a folder, restarting, and selecting them without compiling TaskPin again. Standing user authorization covers autonomous implementation, tests, GitHub main/release, and local update. No new design approval is required by the user.

## Product behavior

TaskPin 0.4.5 includes pinned PP-OCRv6 tiny detection/recognition weights alongside v4/v5. Existing selections remain unchanged. The v6 preset uses the measured max-side 960 / 4 intra-op threads / 1 inter-op thread; tests must verify real Chinese and English behavior as well as the local supplied screenshot. The image itself and its transcript stay out of public source and CI fixtures.

The stable model directory is `<application>/ocr-models` (`D:/TaskPin/app/ocr-models` on this machine), separate from the managed `ocr/` runtime. Settings provides Open model folder and Refresh. Opening settings automatically runs discovery; restarting the application and opening settings discovers the same files. No hash/model import/load happens on the GUI thread.

Each immediate subfolder is one model package containing one detection and one recognition `.onnx`. Canonical names such as `PP-OCRv6_det_small.onnx` + `PP-OCRv6_rec_small.onnx`, or `ch_PP-OCRv5_det_mobile.onnx` + `ch_PP-OCRv5_rec_mobile.onnx`, determine family, role, type and language. The root itself may contain one pair. Missing/ambiguous/unsupported files are shown as unavailable with reasons, never guessed into a different model. Generic filenames can use an optional `model.json` descriptor; ordinary recognized filenames need no descriptor, hash entry, code change, or rebuild. Supported adapters: v4/v5 mobile/server, v6 tiny/small/medium; arbitrary future architectures need an engine update. Native Paddle `.pdiparams`/`.json` exports require compatible ONNX conversion.

Packages have stable IDs derived from relative package location (`local-` + 64 lowercase SHA256 characters), independent of file contents. Selection stores that ID, not an executable path. Renamed/removed selected packages fail visibly; no fallback. Replaced model bytes produce a new model identity and worker reload. A malformed optional descriptor does not fall back to auto detection. No user package Python/script is executed; inference never downloads models or dictionaries.

## Interfaces

`helper.py --catalog --model-dir ABS_DIRECTORY` returns one bounded JSON object:

```json
{"ok":true,"models":[{"id":"builtin-ppocrv6-tiny","name":"PP-OCRv6 tiny","description":"...","enabled":true,"source":"builtin","files":["absolute weight path"]},{"id":"local-<64 hex>","name":"folder name · PP-OCRv6 small","description":"...","enabled":true,"source":"folder","files":["absolute det path","absolute rec path"]}]}
```

Unavailable entries have `enabled:false` and an explanatory `error`/`errorCode`. Catalog scans only bounded directory metadata/config/filenames, no weights/hash/ONNX load. At most 128 packages; excessive or malformed directory conditions return explicit bounded errors. Paths/display strings are treated as text. GUI holds its own draft while a refresh runs and ignores stale callbacks after cancellation/destruction.

Normal helper requests keep the current built-in/custom protocol. A folder request uses:

```json
{"image":"base64 PNG","profileId":"local-<64 hex>","modelDirectory":"absolute model root","requestId":"1"}
```

Folder resolution/actual SHA256/loading is performed by the Python worker on each request. IDs are validated; custom profilePath is mutually exclusive with profileId/modelDirectory. Successful output reports actual family, profileId, modelIdentity, stageMs and workerReused. A catalog entry is a candidate, not a promise of successful ONNX loading. Settings Save performs cancellable background actual loading before committing a folder/custom choice, checks the same draft and file stats, and preserves both settings files on failure/cancel.

Qt `OcrProfiles::modelDirectory()` uses `TASKPIN_OCR_MODELS` for isolated tests, otherwise the application sibling directory. Static built-in options may be shown immediately as a fallback while the asynchronous catalog is loading. `OcrProfiles` permits the new built-in and well-formed local IDs without large file I/O. GUI draft identity includes local entry file paths and current size/mtime metadata; custom validation remains compatible.

## Distribution and upgrade

Runtime builder copies Python catalog code and all six pinned weights, verifies actual loading for all three built-in profiles, and records their hashes in the runtime receipt. Portable packaging includes only an `ocr-models/README.txt` guide, never development/user packages. Local updater explicitly preserves the existing model directory and verifies every preexisting package file hash. User DB/settings/Key/history/autostart remain unchanged. Keep D:/TaskPin root limited to app/repo/release.

## Validation

Red/green tests cover v6 actual text/metadata, filename-only package discovery, fresh settings/process discovery, save/cancel/reload, incomplete/ambiguous/malformed packages, unknown family, replacement, deleted selected folder, worker switching, invalid IDs/options and no automatic fallback/download. Real generated Chinese/English/dark fixtures run for all three built-ins and at least one discovered package; adding a real v6 small pair to an isolated package folder must produce a selectable option without compilation. Native Qt tests and Playwright process bridge run in GitHub Actions and release gates with no desktop mouse/system input simulation. Package/source/runtime hashes and the actual published ZIP are verified before local installation.
