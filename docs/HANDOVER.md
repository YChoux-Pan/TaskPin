# TaskPin 0.3.0 handover

Updated 2026-11-19. This document describes the current Qt implementation; use code, current test reports and observed desktop evidence together. Old reports, screenshots and `docs/legacy/` are historical context rather than a current acceptance checklist.

## Start here

- User guide: [Chinese README](../README.md) / [English README](../README.en.md).
- Build/test/package commands and tool discovery: [BUILDING](BUILDING.md).
- What passed, what is a fixture and what still needs a desktop: [TESTING](TESTING.md).
- Layers, coordinate flow and persistent model: [ARCHITECTURE](ARCHITECTURE.md).
- Tag publication and downloaded-asset verification: [RELEASING](RELEASING.md).

The active source is `TaskPinQt/` (C++17/Qt >= 6.4). 0.3.0 follows the Qt 0.2.x line; earlier unprefixed C# version numbers refer to a separate historical implementation. No old C# source, private data backup or developer toolchain needs to be restored to build this repository.

## What changed in 0.3.0

The editor has a viewport-aware toolbar, contextual tool parameters, consistent labels/tooltips, annotation undo/redo and shared render/export coverage. The OCR output remains `识别为任务`. Pin menus are grouped without changing workflows. A user-reported direct-pin displacement was traced to cursor-based placement near the selection bottom-right: screenshot pins now preserve the capture top-left and logical display size, with DPR and shadow padding accounted for.

The task schema, notes, card/calendar workflows, settings and shortcuts remain compatible. Generic Release build, isolated automated tests, fresh portable deployment, committed source packaging, SHA-256 manifests, Windows CI, tag release workflow, issue forms and third-party notices replace machine-specific build assumptions.

## Evidence and remaining work

Fresh Task 3 local automation: CTest 2/2, screenshot suite 41/0, integration suite 26/0 and application selftest 53/0. Exact script invocations and deployment manifest are recorded with the task handover report; public reproducible commands are in BUILDING. Future work must report its own current results.

The physical desktop record covers primary-screen DPR 1.25 screenshot selection → direct pin with matching logical content geometry, wheel zoom and grouped menu. Fixture-only cropped editor/menu images are committed. The screenshot was taken from the 0.3.0 development UI before the binary version bump; it is not evidence of every editor action. Full native annotation editing, copy/save and notes acceptance remains pending user timing. No printer, real AI provider, fresh OS or every OCR language/physical monitor was accepted by these tests. Do not publish a claim that these passed without a subsequent record.

## Safety and maintenance boundaries

- Use a new absolute `TASKPIN_DATA` for tests; do not send `--quit` or desktop inputs to the normal user instance. New scripts never invoke the legacy desktop-input script.
- Coordinate native input with the user. Preserve the user's running process, data, autostart value, clipboard and window state; use synthetic fixture content.
- All Win32 API includes stay in `src/platform/`. Keep C++17/Qt 6.4 compatibility and do not casually migrate the SQLite schema.
- Screenshot scope is the monitor under the cursor, not cross-monitor capture. Automated DPR fixtures do not establish all physical-monitor behavior.
- Source/release archives must contain no databases, credentials, personal screenshots, logs or developer toolchains. Keep third-party runtime/icon notices with the portable files.
- Do not share settings or logs without review: API keys are plaintext and logs can contain OCR/AI results. AI remains optional.

## Priorities after the release gates

Complete the pending normal-mode desktop checks, broaden physical mixed-DPI and OCR-language coverage, and address accessibility/interaction reports. QR/barcode support and automatic clipboard-to-task monitoring are not implemented. They require a separate scoped design rather than an assertion that an old implementation's features still exist.

Only the tag release workflow publishes the standard assets. The maintainer/controller waits for that workflow before supplemental uploads or final release text, then independently downloads and checks the distributed bytes. Do not run two publishers concurrently.
