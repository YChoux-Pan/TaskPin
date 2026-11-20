# TaskPin 0.3.0 background verification follow-up

Date: 2026-11-20. Source checked: `97f3dee1c12af5b142bc228b61a5beea0dacf075`; implementation remains `f3747c3f3a34679cd0b91138e8b5e8a006b677ab`. This follow-up changes evidence and handover documents only. No release has been published.

## Fresh automated results

Windows x64, Qt 6.4.3, MinGW GCC 11.2.0, C++17, Release. CMake configuration/build completed with no compilation needed. The documented `TaskPinQt/scripts/test.ps1` ran against that build with a new isolated data directory and the offscreen platform.

| Check | Observed result |
| --- | --- |
| Screenshot QtTest | 41 passed, 0 failed, 0 skipped |
| Integration QtTest | 29 passed, 0 failed, 0 skipped |
| CTest | 2/2 passed |
| Application selftest | 53 passed, 0 failed |
| Real-account Settings Save regression | 4 passed, 0 failed, 0 skipped; exact autostart status/type/raw bytes preserved |
| Freshly extracted portable candidate | 53 passed, 0 failed; Windows platform, Windows-only PATH, isolated data, no SDK paths |

The first rerun incorrectly set `TASKPIN_TEST_FONT` to an absent local Noto file and failed at `initTestCase`, before screenshot cases ran. Removing that override selected the documented installed YaHei font; the complete suite then passed without skips. No product change was needed. Hosted runs use the pinned, hash-verified Noto font instead. Expected offscreen/font-directory and deliberate malformed-settings diagnostics are not hidden.

Local reports are retained outside the public source under the controller's `testing/2026-11-20-background-reports/`, plus the separate real-account Settings Save report. Earlier hosted checks for `97f3dee` are [44500211174](https://github.com/YChoux-Pan/TaskPin/actions/runs/44500211174) and [44500219233](https://github.com/YChoux-Pan/TaskPin/actions/runs/44500219233); both were rechecked as successful.

## Exploratory desktop observations and provenance correction

The unlocked desktop session used a dedicated data directory and had no AI provider enabled. A Chinese quick-add string, `桌面验收：保留原有笔记逻辑`, was visibly entered, submitted with Return, and recorded as one open task by the running instance. The calendar command logged an opened calendar, but it was not available for complete visual acceptance. An earlier capture in that isolated session was recognized using the installed `zh-Hans-CN` Windows OCR runtime and created a task; recognition included character errors. This is a functional observation, not an OCR accuracy benchmark or evidence for every language.

Subsequent byte verification found that the exploratory desktop executable had SHA-256 `85e2565c22d69e102444366b0834574068d3a2ed110889350e51d2bd7dfe9fef`. The currently tested build and candidate archive executable have SHA-256 `986260510c040561056ac30437d63e92c668560c4eca203edbb726369c281589`. A displayed version of 0.3.0 did not establish identical bytes. Consequently, this session's quick-add/OCR/calendar observations are exploratory and must not be described as final-candidate acceptance. Previously recorded native pin geometry remains historical implementation evidence with its original date and limitations.

Repeated external input and minimized/moved windows prevented reliable screenshot acceptance. The desktop controller rejected an attempted Edge restore because its required input-state refresh had not succeeded; no workaround was used. The user then explicitly requested background work first. Native input stopped. Both isolated test processes exited during cleanup; the original installation was not replaced. The previously recorded normal process was no longer running when rechecked, and no command was sent to that normal instance. Its repository remained clean at `e34ef36`.

Only fixture-safe images already in the repository are retained publicly. Captures containing unrelated desktop content and raw local task/configuration logs are excluded.

After the isolated processes exited, the desktop test directory was refreshed from the audited portable candidate and its executable hash matched `986260510c040561056ac30437d63e92c668560c4eca203edbb726369c281589`. It was not started for another interactive session. A separate fresh archive extraction passed selftest 53/0 using only Windows system directories on PATH. The replacement preparation therefore has byte provenance and portable regression evidence; final native acceptance remains pending.

## Remaining release gates

Use the audited candidate bytes for the next native session, with a separate data directory. Complete annotation editing, undo/redo, copy/save and output deduplication; direct pin placement and current menu percentages; note/task/card/calendar persistence after restart; and ordinary `Qt::Tool` window behavior. The user has already authorized these tests; only an idle, controllable desktop is needed.

Keep PR #1 in draft until those gates pass. Then refresh the versioned report, package the final committed source, integrate the PR and push the version tag. The release workflow is the sole publisher. Download and independently hash the actual release assets before backing up and updating the original installation. Clean-OS, physical printer, real AI services and all physical mixed-DPI arrangements remain outside this verification.
