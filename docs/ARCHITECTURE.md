# Architecture — TaskPin 0.3.0

The maintained application is C++17 / Qt Widgets for Windows 10/11 x64. CMake requires Qt >= 6.4. The reference release kit is Qt 6.4.3 / MinGW 11.2 x64. The repository contains one active implementation under `TaskPinQt/`; older C# documents in `docs/legacy/` are historical references, not current contracts.

## Layers and responsibilities

| Directory | Responsibility |
|---|---|
| `src/app/` | `AppHost`: tray, global hotkeys, command server and orchestration of UI, OCR and AI |
| `src/ui/` | Screenshot overlay/toolbar, annotation rendering, image pins, task card, quick-add, history, calendar, settings, whiteboards, color picker |
| `src/core/` | Task/store/settings, parsing, filename/history, OCR, optional AI, instance names and selftest |
| `src/platform/` | Windows hotkeys, capture, window picking/effects and autostart; all Win32 API includes stay here |

UI uses core/platform services; core and platform must not depend on UI. App orchestration may use all layers. Storage has no window code. Qt modules are Core, Gui, Widgets, Sql, Network, PrintSupport and Svg; test targets additionally link Test. Icons are embedded SVG paths rendered locally by `QSvgRenderer`; no icon network request occurs.

## Startup, process isolation and commands

`main.cpp` creates QApplication and logging, handles `--selftest`, then routes command requests to the matching instance before creating `AppHost`. With the default data directory, local servers retain the names `TaskPinQt.Cmd` and `TaskPinQt.SingleInstance`. A custom `TASKPIN_DATA` adds a SHA-256 suffix derived from its resolved directory identity, including existing aliases/junctions, so test/secondary data directories do not command the user's normal instance. `--dump` asks the owner to write a snapshot rather than stealing its command server.

Supported command entry points include `--snip`, `--full`, `--last`, `--pin`, `--pins`, `--color`, `--board`, `--board-transparent`, `--quick`, `--toggle`, `--card-top`, `--card-desktop`, `--card-layer`, `--recycle`, `--history`, `--calendar`, `--settings`, `--quit`, `--dump [path]` and `--selftest [dir]`. Preserve their meaning when changing UI actions.

UI and native event callbacks run on the Qt main thread. OCR runs a PowerShell/WinRT helper through `QProcess`; AI uses `QNetworkAccessManager` and a timeout. Results return through signals. Do not block interactive rendering with new synchronous work.

## Capture and editor coordinates

`platform/ScreenCapture` captures the monitor under the cursor, not the entire virtual desktop. Captured pixels carry their screen geometry and DPR. Selection coordinates are logical within the overlay; exported image pixels use DPR. Negative monitor origins must be preserved as global coordinates.

`SnipOverlay` owns selection, active tool, annotation vector and output. `Annotation` paints the shared export/display representation. `SnipToolbar` owns action labels/shortcuts and a layout constrained to the viewport. Parameters are contextual to the active tool. `EditorHistory` snapshots annotation mutations for undo/redo; gestures record one mutation rather than every move event. A text edit commits or cancels through the overlay's existing handlers.

Copy/save/pin/OCR use rendered selection output. The captured signal also carries the global logical rectangle into `AppHost`. Direct screenshot pins use its top-left and logical size: the old path used the current cursor near the selection's bottom-right and shifted the pin. `PinWindow` accounts for DPR and shadow padding separately from visible content geometry. Clipboard-created pins continue to use their own placement workflow. See automated and physical evidence boundaries in [TESTING](TESTING.md).

Pin menus group copy/save/OCR, appearance/zoom, transforms, click-through/groups and close actions without changing their public semantics. Task recognition retains the visible Chinese label `识别为任务` and feeds the existing task flow.

## Persistent model and privacy

`Store` uses SQLite with WAL. Existing tables remain `task` and `task_day`; no 0.3.0 schema migration is introduced. `task` retains title, note, OCR/source/tags/due/priority/order, open/done/dropped state, timestamps, carry-over fields, attachment and AI state fields. `task_day` associates a task with a day's state. Soft-deleted tasks are available through the recycle bin. Preserve settings migration and note/card/calendar behavior.

`Settings::dataDir()` uses `TASKPIN_DATA` when set, otherwise Qt's AppLocalDataLocation (`%LOCALAPPDATA%\TaskPin\TaskPinQt\`). Settings use atomic JSON writes and back up malformed JSON. History and OCR files stay under the selected data directory; saved user exports use their configured destination. Logging may contain OCR/AI content and service URLs, so it is not a safe public fixture.

AI is optional and off by default. If enabled, the configured OpenAI-compatible service receives recognized text (up to 4000 characters) for a title summary; failures fall back to recognized text. Credentials currently reside in plaintext settings. Windows OCR depends on installed OS components/languages. Never package application data with source or binaries.

## Build and verification

`TaskPinQt/build.cmd` delegates to `scripts/build.ps1`. CMake retains `TaskPinScreenshotTests` and `TaskPinIntegrationTests`, runs them offscreen via CTest and writes QtTest text/JUnit reports. `scripts/test.ps1` uses separate data and also waits for `--selftest`. `scripts/package.ps1` deploys Release runtime into a fresh staging directory and archives committed source. Development-tool absolute paths are supplied by parameters/environment, not stored in scripts.

Automated DPI fixtures and physical desktop checks answer different questions. `TASKPIN_AUTOMATION=1` changes top-level window flags for testing; see [TESTING](TESTING.md). Build, release and provenance instructions are in [BUILDING](BUILDING.md), [RELEASING](RELEASING.md) and [HANDOVER](HANDOVER.md).
