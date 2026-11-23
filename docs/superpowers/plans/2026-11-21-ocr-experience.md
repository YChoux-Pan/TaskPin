# OCR performance and pin experience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make screenshot translation measurable, configurable, smoother and automatically verified.
**Architecture:** Keep native Qt widgets and share a validated local PaddleOCR model profile across notes and translation. Playwright Test runs native probe processes and asserts JSON/image results; GitHub Actions is the final gate.
**Tech Stack:** C++17, Qt6.4.3, Python3.12 embedded, RapidOCR/ONNX CPU, Playwright Test, GitHub Actions Windows.
**Spec:** docs/superpowers/specs/2026-11-21-ocr-experience-design.md

## Global Constraints

- No desktop mouse simulation or user data in fixtures/reports.
- Preserve default PP-OCRv5 and existing AI connection; never output API keys.
- Keep cancellable asynchronous native UI; custom profiles are local and validated.
- Cold/repeated OCR and actual provider latency are reported separately; no invented speed claims.

### Task 1: Native zoom and comparison UI

Files: PinWindow.cpp/.h, TranslationWindow.cpp/.h, tests/tst_experience.cpp.
Interfaces: existing PinWindow wheel handler and TranslationWindow::showComparison(), QObject names for probe inspection.
- [ ] Add regression for reversing pending zoom and cursor anchor; `QApplication::sendEvent` with QWheelEvent; assert final scale decreases and mapped anchor drift <=2px.
- [ ] Add dialog regression: showComparison then assert opacity1, compact header and text panes occupy >60% usable height; inspect rendered PNG alpha.
- [ ] Run red, implement displayed-scale reversal/anchor and opaque proportional dialog, run green/full relevant suites.

### Task 2: Model selection and measured OCR speed

Files: ocr/helper.py, ocr/test_helper.py, translation/TranslationOcr.*, new model-profile config, TranslationSettingsPage.*, relevant tst_translation cases.
Interfaces: selected model profile id/path saved atomically in isolated settings; helper request/response include engine/modelVersion/timings; runtime profiles use SHA256 and det/rec family/type. Notes and translation call same adapter.
- [ ] Add behavioral tests for profile round-trip/cancel, custom-path/hash failure and compatible selection, missing runtime; run red.
- [ ] Implement local profile registry/selection and settings UI with default compatibility. Verify actual built-in profile and custom fixture profile, invalid profiles cannot silently fall back.
- [ ] Use measured cold/warm profiles to decide reuse; any persistent worker must handle cancellation, stale responses, busy lifetime and host ownership, with regression cases.
- [ ] Run real engine tests with generated English/Chinese and protocol/model failures.

### Task 3: Benchmarks and Playwright native acceptance

Files: tests/translation_probe.cpp, CMakeLists, new tests/playwright config/specs and package lock, workflows/scripts.
- [ ] Record OCR duration, network/client duration and rendering/total separately through actual Qt signals; generated small and paragraph fixtures, at least3 actual service samples where available.
- [ ] Playwright invokes native fixtures, parses JSON and attaches PNGs. Assert F1 translation, no default comparison, model metadata, zoom anchor/reversal, opaque balanced comparison, failures/cancel and no user-data changes.
- [ ] Add CI Node/bootstrap + npm ci and runner; secrets-free fixture mode. Preserve full Qt and OCR gates and upload HTML/JSON/JUnit/screenshots.
- [ ] Version0.4.3, document observations/model profile format/test boundaries; review all changes.

### Task 3a: User-added shortcut consistency

Files: core/Settings, app/AppHost, SnipOverlay/Toolbar, SettingsDialog, tst_screenshot/integration.
- [ ] Red tests for F3 after F1/custom configured key, actual fallback and annotation text focus, then route to current selection.
- [ ] Add persisted `editorTranslate` default Ctrl+T editor-only setting; validate collisions, preserve cancel/save and update hints.
- [ ] Real host routing tests cover selected/empty editor and normal clipboard behavior; Playwright runs these native assertions in CI.

### Task 4: Release and local update

- [ ] Full local gates, independent code review, commit/push PR and inspect Windows CI reports.
- [ ] Merge authorized main, tag v0.4.3 and await release workflow.
- [ ] Download actual assets, compare GitHub+manifest digests, source/tag bytes and isolated Chinese path/selftest/OCR.
- [ ] Install verified app, verify19 task records and settings/key/history/autostart, shortcut, then clean temporary rollback and archive worktree.
