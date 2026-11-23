TaskPin 0.4.4 - Windows 10/11 x64

Extract the whole TaskPin-0.4.4 folder and run TaskPinQt.exe.
Keep the DLLs and plugin folders beside the executable. No Qt SDK is required.
Use the system tray menu for Settings and Exit.

Default shortcuts:
F1 region editor: Translate button / Ctrl+T produces a movable translation pin
F1 region screenshot; Ctrl+Alt+F current monitor capture
F3 pin clipboard image; Shift+F3 show/hide all pins; Ctrl+Alt+Space quick note
Ctrl+Alt+D show/hide task card; Ctrl+Alt+G screenshot history
Editor: Enter copy; Ctrl+S save; Ctrl+P pin; Ctrl+R recognize as a task
Ctrl+Shift+S saves to the configured directory with a unique filename.
Successful Save and Quick Save copy the absolute file path to the clipboard.
Failed or cancelled saves keep the clipboard unchanged. Enter still copies pixels.
Old default screenshot/pin keys migrate once; custom keys remain unchanged.
If another app occupies a key, the tray menu shows TaskPin's actual fallback.

Portable application files do not imply portable user data.
Data defaults to %LOCALAPPDATA%\TaskPin\TaskPinQt\.
Exit before upgrading or backing up that whole data directory.
TASKPIN_DATA can select a separate directory.

AI is disabled by default. Enabling a configured service sends OCR text to it.
The API key is stored in settings.json without OS credential encryption.
Do not share databases, settings, logs, screenshot history or OCR files.
Screen translation is configured on the Screen Translation settings tab.
It shares the AI tab endpoint, model and API key; no second key is required.
Cloud translation sends recognized text only. The test button uses a fixed sentence.
Translation output is a frameless desktop pin, not a separate result dialog.
Drag to move, wheel to zoom; right-click for Original and optional Text Comparison.
Long translations use numbered annotations; Save Translated Image exports PNG.
Mock mode is a workflow demonstration, not real translation.
PaddleOCR PP-OCRv5 mobile and lightweight PP-OCRv4 mobile models and CPU runtime are included in the ocr/ folder.
Keep that complete folder. No system Python or Windows OCR language pack is required.
Verify technical identifiers and formulas against the original image.
Capture is limited to the monitor under the cursor.

TaskPin code: Unlicense (LICENSE). Third-party terms are separate:
THIRD_PARTY_NOTICES.md and licenses/ contain Qt, icon and MinGW notices.
Qt is dynamically linked; users may replace compatible Qt libraries.
Qt source and build information: https://download.qt.io/archive/qt/6.4/6.4.3/
Project, instructions, source and limitations:
https://github.com/YChoux-Pan/TaskPin

Choose Settings > Screenshot translation > Local OCR > PP-OCRv4 mobile for the bundled lightweight model (14.9 MiB of weights). Both OCR entry points share this selection. Existing default/custom selections are retained on upgrade.
