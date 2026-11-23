# OCR model catalog implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans task by task. Standing user authorization selects autonomous execution in this session.

**Goal:** ship PP-OCRv6 tiny plus folder-discovered compatible ONNX packages with no rebuild per added model.

**Architecture:** authoritative Python discovery/validation, asynchronous Qt catalog UI, per-request shared worker model identity, preserved portable model directory.

**Tech stack:** C++17 Qt6.4.3, Python3.12, RapidOCR3.10.0, ONNX Runtime CPU, Playwright Test, Windows GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-23-ocr-model-catalog-design.md`.

## Task 1: runtime catalog and real v6 models

Files: `TaskPinQt/ocr/model_catalog.py`, `helper.py`, `test_helper.py`, `runtime-lock.json`, `build_runtime.py`, `test_model_catalog.py`, `TaskPinQt/scripts/package.ps1`, `licenses/PADDLEOCR_RUNTIME_NOTICE.md`.

- [ ] Add failing behavioral tests creating a temporary package folder with canonical names and real runtime weights; invoke catalog subprocess and expect a usable local entry. Exercise removed/duplicate/invalid packages. Add real v6 text and profile metadata assertions before implementing.
- [ ] Run existing runtime against new tests; preserve expected-failure evidence under DEV/catalog-reports.
- [ ] Implement catalog CLI and file-pair discovery per spec, strict optional descriptor, bounded errors, stable IDs, per-request hash/model validation, actual v6 enum/model types and detector/cpu options. Keep default/custom v4/v5 compatibility.
- [ ] Pin v6 weights from the completed local experiment: det SHA `f42c0fbd294d95eac1a550e131b277dac97462c8025fa4b6c3cec1b7894bd3d5`; rec SHA `e16e242de5937ad92609223f19bc2aff3727ee40b095f996907c24749bad251b`. Builder and package require all three actual-load selfchecks.
- [ ] Run `pin-build/ocr/python.exe -I -B TaskPinQt/ocr/test_helper.py --runtime ABS_RUNTIME` plus catalog tests; verify generated Chinese/English/dark fixtures and dropped real pair. Self-review source, errors and compatibility.

## Task 2: asynchronous Qt catalog/settings

Files: `OcrProfiles.h/.cpp`, new `OcrModelCatalog.h/.cpp` if useful, `TranslationSettingsPage.h/.cpp`, `tests/tst_translation.cpp`, CMake source registration.

- [ ] Add failing tests expecting v6 option, filename-only added folder option after Refresh/reopen, preserved drafts/cancel, failed/removed model disabled or rejected, and responsive asynchronous Save.
- [ ] Implement `OcrProfiles::modelDirectory()` and local-ID validation; consume Task1 catalog JSON via a cancellable bounded QProcess, never GUI weight hashing/loading. Add buttons object names `translationOcrOpenFolder` and `translationOcrRefresh`.
- [ ] Expose loaded local entry file list to Save draft identity. Background validation forwards folder ID/root or custom path, validates expected profile ID, and commits only unchanged draft; preserve existing rollback/cancel behavior.
- [ ] Run native settings tests against shared Release build, capture red and green. Parent owns shared compiler scheduling.

## Task 3: worker routing, integration, documentation and acceptance

Files: `TranslationOcr.cpp`, `AppHost.cpp`, native probes, `tests/playwright/native.spec.ts`, README/CHANGELOG, `docs/OCR-MODELS.md`, portable guide, version.

- [ ] Snapshot local model directory with each queued request; forward it only with local profile IDs. Make app diagnostics report selected catalog/profile accurately.
- [ ] Add Playwright regression for v6 and dropping a real pair into an isolated folder, refresh/save/restart persistence, full translation-pin output; no OS input. Verify unknown/removed model errors do not use another model.
- [ ] Document copy-only canonical filename examples and Open folder/Refresh. Clearly explain ONNX adapter boundaries; no arbitrary future-family guarantee. Record measured local v6 screenshot timings without publishing user image/transcript.
- [ ] Run full QtTest/selftest, actual Python models/catalog suite, Playwright, independent code review; resolve important findings. Native commands use DEV/pin-build, compile `--parallel 2`.

## Task 4: authorized publication and local update

- [ ] Commit/push branch and PR, attach PR; require Actions reports and counts before merging main. Verify merged tree identical to tested branch and push v0.4.5 annotated tag.
- [ ] Wait release gates; download actual published ZIP/source/SHA, validate source tag bytes, all six weights/receipt hashes, Chinese path/System32-only runtime and real helper behavior.
- [ ] Preserve old user model directory across app replacement and verify its inventory plus all user data/configs. Update D:/TaskPin/app and desktop shortcut from published package; check actual running version/hotkeys.
- [ ] Publish test report and verification archive, replace SHA manifest with hashes of four assets; re-download supplements and verify all five GitHub digests. Synchronize D:/TaskPin/release, remove only task temporary rollback directories, archive managed worktree.
