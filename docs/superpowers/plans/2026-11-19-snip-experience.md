# TaskPin 0.3.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox syntax for tracking.

**Goal:** Ship a verified Windows x64 release with a compact screenshot editor, reliable reversible edits, preserved notes, and reproducible open-source documentation.

**Architecture:** Retain Qt Widgets and the existing app/core/platform boundaries. Extract toolbar action/layout metadata and annotation undo transactions from SnipOverlay; keep native calls in platform. Use deterministic QtTest coverage plus real desktop smoke testing and the existing selftest.

**Tech Stack:** C++17, Qt 6.4.3, QtTest, MinGW 11.2, CMake, Ninja, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-19-snip-experience-design.md` (approved by the user on 2026-11-19).

## Global Constraints

- Windows 10/11 x64; keep C++17 and Qt >= 6.4 compatibility.
- Keep the existing task database schema, note/card/calendar workflows, user settings and shortcuts compatible.
- Screenshot scope is the monitor under the cursor; do not claim cross-monitor capture.
- All Win32 API includes remain under `src/platform/`.
- Output action OCR means recognition into the existing task flow, with the visible label `识别为任务`.
- Source and releases must exclude personal databases, credentials, logs and developer toolchains.
- User has authorized source upload, changelog and GitHub release; do not ask again for those routine authorized actions.
- Work in the fresh clone on `feat/snip-experience-0.3.0`; do not modify the running installation at `D:/TaskPin/repo`.
- Baseline: source commit e34ef36, Release build succeeded; freshly built selftest 53 passed / 0 failed.

## Task 1: Screenshot regression suite and editor redesign

**Files:** modify `TaskPinQt/CMakeLists.txt`, `TaskPinQt/src/ui/SnipOverlay.h/.cpp`, `TaskPinQt/src/ui/Annotation.h/.cpp` where required; create focused toolbar/editor-history units under `TaskPinQt/src/ui/`; create `TaskPinQt/tests/tst_screenshot.cpp`. IconSet may be extended with compatible licensed icons.

**Interfaces:** preserve all existing SnipOverlay signals, `selection()`, `toolbarRect()`, `hoverHint()` and `setPreselect()`. Add production diagnostics for toolbar actions (id, rect, enabled, label) if useful to AppHost dump/accessibility. Unit boundaries should expose normal editor/history operations, not test-only switches.

- [ ] Add a QtTest executable `TaskPinScreenshotTests` linked to the real editor modules and Qt6::Test. Register it with CTest and deploy or locate the QtTest runtime. Keep existing app target and selftest working.
- [ ] First reproduce missing behavior with real events and signal spies. Expected values must be literal fixtures. Example behavior:

```cpp
QSignalSpy output(overlay, &SnipOverlay::copied);
QTest::mouseDClick(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
QCOMPARE(output.count(), 1); // one completion, one captured signal
```

Use synthetic frozen images with a known colored quadrant, e.g. physical pixel (250,125) is red in a 1000x750 source at DPR 1.25. An interior drag must move the existing selection without replacing it. Add failures for undo after delete/move/recolor; redo invalidation; unfinished text submitted on output; toolbar bounds on a 360x240 screen; toolbar disabled undo before edits; no tool action before a valid selection; toolbar absent while selecting. Capture the failing output before implementing.
- [ ] Extract action metadata and bounded responsive toolbar layout. Use grouped tools/actions and a compact light bar. Provide contextual color/width/font/mosaic properties, custom color, stateful disabled actions, an emphasized copy action, and a More menu containing quick-save and print. Properties and menu labels must match actual keyboard behavior. All actions stay accessible on narrow screens. Toolbar must avoid the selection when room exists and remain within the viewport. Render the frozen pixmap into the logical viewport explicitly so high-DPI display matches composition.
- [ ] Implement moving an empty selection interior, distinct annotation movement, resize handles, coherent selection cursors, double-click copy, non-intrusive magnifier behavior, and full undo transactions for add/move/delete/recolor/erase. A drag is one undo entry. New mutation invalidates redo. Keep input within valid capture bounds; do not create accidental outside annotations.

```cpp
// Required observable behavior, regardless of chosen internal history representation:
drawRectangle(); deleteSelected(); undo(); // rectangle restored
moveSelected(); undo();                   // previous geometry restored
redo();                                   // moved geometry restored
changeSelectedColor(); undo();            // previous color restored
```

- [ ] Commit or cancel inline Chinese text predictably before switching tool/output; preserve Enter/Esc semantics while input has focus. A selected text annotation must have bounds aligned with its actual rendered baseline. Save failures keep the editor open with a visible reason. Successful copy/pin/OCR/save records exactly one capture.
- [ ] Run the full suite against DPR fixtures 1.0, 1.25, 1.5, 2.0, screen-edge selections and nonzero screen origins; verify both physical output sizes and known image pixels. Verify existing selftest still passes. Commit changes and report red/green commands, final counts and any real limitations.

## Task 2: Isolated app sessions, pin menu and integration regression

**Files:** `TaskPinQt/src/main.cpp`, `TaskPinQt/src/app/AppHost.h/.cpp`, `TaskPinQt/src/ui/PinWindow.h/.cpp`; create `TaskPinQt/src/core/InstanceNames.h/.cpp` and `TaskPinQt/tests/tst_integration.cpp`; update CMake for tests. Extend screenshot diagnostics without changing Task 1 action ids.

**Interfaces:** default normal-session command socket names remain compatible. A custom TASKPIN_DATA directory produces a stable, path-derived unique namespace used by every CLI probe, command server and single-instance server. `TASKPIN_AUTOMATION=1` provides isolated development execution without registering hotkeys or changing logon/autostart settings. Document these diagnostic settings in testing docs, not in normal product flows.

- [ ] Add failing tests for equal canonical data paths yielding equal namespaces, different data paths yielding different namespaces, default namespace compatibility, and pin menu state/zoom/reset behavior using real Qt widgets.

```cpp
QCOMPARE(instanceNamesFor(QString()).command, QStringLiteral("TaskPinQt.Cmd"));
QVERIFY(instanceNamesFor(dataA).command != instanceNamesFor(dataB).command);
```

- [ ] Implement the namespace helper and apply it consistently to all commands and servers. Ensure automation mode avoids autostart registry writes and global hotkey registration while preserving command-triggered capture, notes and settings for its isolated data directory.
- [ ] Redesign pin menu groups with checkable opacity and zoom presets, explicit original-size/reset actions and readable shortcuts. Preserve rotation, replacement, groups, OCR-to-task, click-through recovery and close behavior. Verify small monitor placement clamps safely even for a large image.
- [ ] Add useful dump metadata for screenshot action ids, enabled state and geometry, pins and instance identity, omitting secret values from any newly generated public evidence. Keep existing dump fields compatible.
- [ ] Run new integration tests, screenshot tests, existing selftest, and a two-process namespace test that verifies an automation process responds in its own directory while the existing user's process remains running. Commit and report evidence.

## Task 3: Portable build, package and open-source project documentation

**Files:** `TaskPinQt/build.cmd`, new build/test/package scripts under `TaskPinQt/scripts/`, `.github/workflows/`, `.github/ISSUE_TEMPLATE/`, `README.md`, `README.en.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `THIRD_PARTY_NOTICES.md`, `docs/BUILDING.md`, `docs/TESTING.md`, `docs/RELEASING.md`, `docs/HANDOVER.md`, `docs/ARCHITECTURE.md`, `.gitignore`, CMake version.

**Interfaces:** retain `build.cmd` entry point while supporting QT_ROOT/QT_KIT/Qt6_DIR/CMAKE_PREFIX_PATH and compiler/CMake/Ninja discovery without requiring the author's absolute paths. Release version is 0.3.0. Package name `TaskPin-0.3.0-windows-x64.zip`, source bundle `TaskPin-0.3.0-source.zip`, checksums `SHA256SUMS.txt`.

- [ ] Make generic scripts discover or accept a Qt kit and configure/build Release. Propagate failures. Provide a dedicated testing script with separate TASKPIN_DATA, offscreen QtTest execution and selftest reports. Do not call the legacy PowerShell desktop input script automatically or touch an existing normal-session process.
- [ ] Package only executable, required deployed runtime/plugins, licensing and end-user README. Exclude CMake files, import libraries, build caches and personal data. Package from a fresh staging directory. Compute SHA-256 from final archives.

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath
```

- [ ] Add Windows CI with a pinned Qt 6.4.3 MinGW-compatible toolchain, qtsvg, Release build, QtTest + selftest and uploaded reports. Add release-by-tag workflow that uploads a portable zip and checksum only after tests succeed. Choose supported, verified GitHub action versions; avoid inventing action commit hashes.
- [ ] Write a polished Chinese-first README and English companion: honest feature scope, real screenshots, download/start, shortcuts, data/privacy/optional AI, build/test links, roadmap, known limits and Unlicense. Update stale hardcoded claims in HANDOVER and ARCHITECTURE. State source provenance without reintroducing old C# implementation or personal paths. Include correct Lucide ISC/MDI Apache-2.0 and Qt runtime licensing notices; use upstream license text.
- [ ] Add issue forms and contributor/release instructions. Clearly distinguish actual desktop evidence, automated DPI fixtures, and untested printer/AI/physical-monitor environments. Run all scripts and document exact output; commit.

## Task 4: Desktop acceptance, independent review and release

**Files:** real evidence under `docs/screenshots/`, versioned test report under `docs/reports/`; final artifacts copied to the parent task's `outputs/TaskPin/`.

- [ ] Launch the built app in an isolated automation namespace/data directory using shell process launch. Use the supported computer-use APIs for actual UI actions. Verify selection, toolbar, rectangle/arrow/text/mosaic, undo/redo, copy, pin/zoom/menu/reset, save and note entry with disposable fixtures; archive screenshots and logs. Keep actual user content out of evidence.
- [ ] Run full Release build, CTest and selftest after final changes. Test the extracted portable app independently of the build directory and verify its selftest.
- [ ] Request independent whole-branch spec/code review. Fix material findings and rerun covering checks. Review source diff, packaging manifest, notes compatibility and claimed test evidence.
- [ ] Commit final test report, screenshots and changelog. Push the authorized source updates, merge through a PR if appropriate, tag v0.3.0, publish the release and upload the verified portable package plus checksums. Prefer GitHub MCP for supported repository operations; use authenticated gh for release asset creation/upload because the connector lacks those capabilities.
- [ ] Read back the remote commit/tag, release asset names/sizes and CI conclusions. Link the public release and local deliverables, report exact test results and practical limits. Never state a pending workflow passed.
