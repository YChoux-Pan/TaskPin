# TaskPin

**Keep the picture. Capture the thought.** Screenshots, desktop image pins and daily task cards for Windows.

[简体中文](README.md) · [Downloads](https://github.com/YChoux-Pan/TaskPin/releases) · [Changelog](CHANGELOG.md) · [Building](docs/BUILDING.md) · [Testing](docs/TESTING.md)

![TaskPin 0.3.0 screenshot editor with demonstration content](docs/screenshots/taskpin-0.3.0-editor.png)

*Real desktop capture of the 0.3.0 development UI using a dedicated demonstration fixture. The executable still reported 0.2.3 before the version bump. This illustrates the editor layout; it does not certify every desktop workflow. Older `test-*.png` images document 0.2.x.*

## Capture, annotate, keep nearby

- **Screenshot editor:** capture the monitor under the cursor, select a region or window, and annotate with shapes, arrows, lines, pen, highlighter, text, numbering, mosaic, blur and eraser. The toolbar fits available space, shows relevant parameters and supports undo/redo and annotation editing.
- **Outputs and image pins:** copy, save, quick-save, pin to the desktop or choose “识别为任务” (recognize as a task). Pins support zoom, opacity, rotation, flipping, click-through and groups, with a grouped context menu. Direct screenshot pins preserve the original selection position and logical size.
- **Daily task cards:** quick notes, Chinese time parsing, priorities, task notes, completion, carry-over, recycle bin and calendar history. Keep the card above windows or in the desktop layer.
- **Utilities:** screenshot history, whiteboard, transparent whiteboard, color picker and configurable global hotkeys. Printing and optional AI have existing entry points; see [testing limits](docs/TESTING.md) for environment coverage.

Built with C++17 and Qt 6 for Windows 10/11 x64. The current interface is primarily Chinese. `TaskPinQt/` is the maintained implementation; the earlier C# implementation is historical and does not define the current feature set.

## Download and start

1. Get `TaskPin-0.3.0-windows-x64.zip` and `SHA256SUMS.txt` from [Releases](https://github.com/YChoux-Pan/TaskPin/releases), once that version is published.
2. Extract the complete folder to a writable location, keep its DLLs and plugin folders, and run `TaskPinQt.exe`. No Qt SDK or development tools are needed.
3. Open Settings from the system tray and check the registered shortcuts. Use the tray to exit the background application.

Verify the download with `Get-FileHash -Algorithm SHA256 -LiteralPath .\TaskPin-0.3.0-windows-x64.zip` and compare it with the matching checksum entry. Portable means no installation is required; **user data is stored in your local user profile by default**. Before upgrading, exit the old version from the tray and back up the data directory, then replace the application folder.

## Keyboard shortcuts

| Global action | Default shortcut |
|---|---|
| Region / current monitor / last region | `F1` / `Ctrl+Alt+F` / `Ctrl+Alt+R` |
| Pin clipboard image / show or hide pins | `F3` / `Shift+F3` |
| Quick note / show or hide card / screenshot history | `Ctrl+Alt+Space` / `Ctrl+Alt+D` / `Ctrl+Alt+G` |

Inside the editor: `V` select, `R` rectangle, `O` ellipse, `A` arrow, `T` text, `M` mosaic, `B` blur; `Ctrl+Z` / `Ctrl+Y` undo/redo, `Enter` copy, `Ctrl+S` save, `Ctrl+Shift+S` quick-save, `Ctrl+P` pin, `Ctrl+R` recognize as a task and `Esc` cancel. Hover a button for its label and shortcut. Global hotkeys can be changed in Settings; the tray shows the shortcuts actually registered after conflict handling.

## Data, privacy and optional AI

Successful save and quick-save copy the screenshot's absolute native Windows file path to the clipboard. `Ctrl+Shift+S` uses the configured auto-save directory and adds a suffix to avoid overwriting existing files. Failed or cancelled saves leave the clipboard unchanged; `Enter` still copies the image.

The three screenshot/pin shortcuts match Snipaste defaults. Upgrades migrate only their old default values while preserving custom bindings; subsequent user choices remain unchanged. If Snipaste occupies the keys, TaskPin uses fallback bindings shown in its tray menu.

The default data directory is `%LOCALAPPDATA%\TaskPin\TaskPinQt\`, containing `taskpin.db` (SQLite and WAL), `settings.json`, `taskpinqt.log`, `history\snip\` and `ocr\`. Set `TASKPIN_DATA` to use a separate directory. Exit the application before copying the whole directory for a backup. Screenshot history and OCR temporary files may contain private information.

Screenshots, tasks and Windows OCR are processed locally. OCR requires Windows OCR components and a suitable language pack. “识别为任务” connects recognized text to the existing task workflow. **AI is disabled by default.** If you configure and enable an OpenAI-compatible endpoint, up to 4000 characters of recognized text are sent to that endpoint. The API key is stored locally in `settings.json` without OS credential encryption. Do not share settings, databases or logs. Your chosen provider's data policy applies. Failed AI requests fall back to the recognized text; screenshot and task features do not require AI.

## Build and contribute

```powershell
$env:QT_KIT = 'C:/Qt/6.4.3/mingw_64'
# Add a matching MinGW, CMake and Ninja to PATH, or pass explicit parameters.
cmd /c TaskPinQt\build.cmd
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/test.ps1
```

[Build and package](docs/BUILDING.md) · [Testing and evidence](docs/TESTING.md) · [Architecture](docs/ARCHITECTURE.md) · [Contributing](CONTRIBUTING.md) · [Release process](docs/RELEASING.md)

## Limits and direction

- Capture is limited to the monitor under the cursor; spanning multiple monitors is unsupported.
- Automated fixtures exercise DPR values, negative origins and narrow viewports. They do not establish behavior on every physical mixed-DPI setup.
- Printers, real AI endpoints and different Windows OCR languages need separate verification. The remaining 0.3.0 editor/note desktop acceptance is tracked in the testing document.
- QR/barcode recognition and automatic clipboard-to-task monitoring are not implemented. Further work prioritizes physical environment coverage, accessibility and interaction polish, without a promised release date.

## License

TaskPin's own code is [Unlicense](LICENSE). Third-party components retain their licenses: Lucide ISC (including Feather MIT notices), Material Design Icons graphics under Apache-2.0, Qt LGPL/GPL and MinGW runtime licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [licenses/](licenses).
