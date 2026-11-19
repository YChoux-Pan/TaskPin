TaskPin 0.3.0 - Windows 10/11 x64

Extract the whole TaskPin-0.3.0 folder and run TaskPinQt.exe.
Keep the DLLs and plugin folders beside the executable. No Qt SDK is required.
Use the system tray menu for Settings and Exit.

Default shortcuts:
Ctrl+Alt+A region screenshot; Ctrl+Alt+F current monitor capture
Ctrl+Alt+V pin clipboard image; Ctrl+Alt+Space quick note
Ctrl+Alt+D show/hide task card; Ctrl+Alt+G screenshot history
Editor: Enter copy; Ctrl+S save; Ctrl+P pin; Ctrl+R recognize as a task

Portable application files do not imply portable user data.
Data defaults to %LOCALAPPDATA%\TaskPin\TaskPinQt\.
Exit before upgrading or backing up that whole data directory.
TASKPIN_DATA can select a separate directory.

AI is disabled by default. Enabling a configured service sends OCR text to it.
The API key is stored in settings.json without OS credential encryption.
Do not share databases, settings, logs, screenshot history or OCR files.
Windows OCR requires suitable installed OCR components and language packs.
Capture is limited to the monitor under the cursor.

TaskPin code: Unlicense (LICENSE). Third-party terms are separate:
THIRD_PARTY_NOTICES.md and licenses/ contain Qt, icon and MinGW notices.
Qt is dynamically linked; users may replace compatible Qt libraries.
Qt source and build information: https://download.qt.io/archive/qt/6.4/6.4.3/
Project, instructions, source and limitations:
https://github.com/YChoux-Pan/TaskPin
