# Testing and evidence

Latest performance and interaction work: [0.4.3 report](reports/2026-11-21-ocr-performance-0.4.3.md). Final local Release: QtTest 212/0/0 (screenshot59, integration48, experience30, translation57, canvas18), selftest53/0 and Playwright10/10. Playwright Test orchestrates native Qt fixtures and real local OCR, and both Windows CI and tag releases require the acceptance gate. No physical desktop input is injected. See [automation details](AUTOMATED-ACCEPTANCE.md) for commands, scope, HTML/JUnit and native screenshots. Versioned evidence retains its own test counts and limitations.

Latest integrated translation/PaddleOCR evidence: [0.4.2 report](reports/2026-11-21-translation-pin-paddle.md). Screenshot 52/0, integration 35/0, experience 20/0, translation 46/0, canvas 18/0; CTest 5/5 and selftest 53/0, zero skips. Translation enters through the F1 editor, outputs a desktop pin and shares PaddleOCR PP-OCRv5 with note recognition. Real model behavior tests and a DeepSeek fixture chain also pass; historical Windows OCR reports below do not describe the new engine.

Latest 0.4.1 repair: [2026-11-21 report](reports/2026-11-21-translation-repair.md). Five CTest targets cover screenshot, integration, experience, translation and image canvas. It adds shared-key save/reload/cancel, nonmodal settings/reentry, endpoint-consent draft preservation, uncached connection tests and a real DeepSeek synthetic OCR→translation→image run. Earlier records below retain their own scope and limitations.

Latest screen-translation verification: [2026-11-21 report](reports/2026-11-21-screen-translation.md). Four CTest targets pass: screenshot 43/0, integration 35/0, experience 20/0, translation 49/0; selftest 53/0, zero skips. Controlled Windows OCR and result-window probes use generated fixtures only. Native clipboard access was unavailable; paid services, Windows 10 and physical mixed-DPI acceptance remain unverified. Historical reports retain their original scope.

Latest Canva icon verification: [2026-11-20 brand update](reports/2026-11-20-canva-icon.md). The final icon build reran all three CTest targets and selftest successfully; source provenance, resource packaging, visual size checks and native-observation limits are recorded there.

Latest experience work: [2026-11-20 report](reports/2026-11-20-experience-update.md). It adds `TaskPinExperienceTests` as a third CTest target and includes controlled localhost AI delay/error/timeout tests. Current local totals are screenshot 43/0, integration 35/0, experience 20/0 and selftest 53/0; native Windows subset 12/0 including setup/cleanup. Earlier records below describe their own commits and do not substitute for the latest run.

## Safe automated entry point

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/test.ps1 `
  -BuildDir TaskPinQt/build -ReportDir docs/artifacts/automated
```

The script uses a new GUID-named `TASKPIN_DATA` directory for each run, sets `QT_QPA_PLATFORM=offscreen` and `TASKPIN_AUTOMATION=1`, executes all five CTest targets and then starts `TaskPinQt.exe --selftest` with `Start-Process -Wait -PassThru`. It checks exit status, saves CTest text/JUnit and selftest reports and restores its environment. CTest stops the sequence on failure; selftest failures also return nonzero. No command is sent to the normal user instance and no existing process is terminated. The selftest has its own disposable database and may deliberately exercise broken settings recovery.

CI first runs `scripts/prepare-test-font.ps1` to fetch Noto Sans CJK SC Regular 2.004 (SIL OFL 1.1) from a pinned upstream commit and verify its Git blob hash. `TASKPIN_TEST_FONT` selects that font, avoiding optional YaHei installation assumptions. Local tests default to Windows YaHei; an explicit font path must load successfully or the Chinese tests fail rather than skip. For offline setup, pass `-FontPath` to prepare a previously downloaded pinned font. The font is test-only and never enters the portable archive; its full upstream license is retained in `TaskPinQt/tests/Noto-OFL.txt`.

Reports default to ignored `docs/artifacts/automated/`: `ctest.log`, `ctest.xml`, QtTest text/JUnit case reports, `selftest.txt`, `selftest.stdout.txt`, `selftest.stderr.txt` and isolated data. Tests must not run against a real user data directory. CI uploads only report files, not generated databases or screenshots.

## What is covered

At the final code-fix verification on 2026-11-19 (`f3747c3`), both CTest targets passed (2/2); the application selftest reported 53 passed / 0 failed. Tests cover editor actions/layout, history and annotation mutations, pixel-based exports, image pin behavior, task/card/calendar workflows and instance isolation. `TaskPinScreenshotTests` has 41 passing cases and `TaskPinIntegrationTests` 29 (including QtTest setup/cleanup), with no skipped cases. Hosted [Windows run 44499197407](https://github.com/YChoux-Pan/TaskPin/actions/runs/44499197407) installed the pinned Noto font, built Release and passed the same counts; its downloaded reports were checked. The fixed actual-dialog Save regression separately passed 4/0 under the real user account with exact raw autostart comparisons, and the extracted portable app passed selftest 53/0 with a Windows-only PATH. See the [versioned record](reports/2026-11-19-0.3.0-verification.md). Use actual current reports when assessing a later commit.

DPI fixtures include multiple DPR values, negative monitor origins and constrained viewports. These are programmatic fixtures: a passing fixture does not prove physical monitor transitions, color management, window manager behavior or every display arrangement. Offscreen Qt plugin warnings about unsupported window operations can occur; check failing assertions and exit status rather than treating an expected platform limitation as native desktop evidence.

## Actual desktop evidence and pending acceptance

The checked-in `screenshots/taskpin-0.3.0-editor.png` and `taskpin-0.3.0-pin-menu.png` were cropped from the real application displaying a dedicated safe fixture. They show the new editor layout and grouped pin menu. Capture occurred after the Task 2 implementation but before the version string was bumped from 0.2.3. They are not re-labeled screenshots from the old 0.2.x implementation. Older `test-*.png`, `toolbar-*.png` and `docs/reports/` are historical 0.2.x evidence and must not be cited as a fresh 0.3.0 run.

Observed on one physical primary screen at DPR 1.25: a selection at logical `(474,165,1097,526)` pinned at exactly the same content geometry. The raw image was `1371x658`; the outer window `(468,159,1109,538)` included 6 logical pixels of shadow on each side. Wheel zoom and the grouped right-click menu were exercised. A second physical screen with negative origin was enumerated, but not physically exercised. The normal application and its autostart registry setting were preserved during this isolated session.

Full native annotation editing, output copy/save and note interaction acceptance remains pending. After unlocking on 2026-11-20, external input/minimization interrupted the session and the user requested background work first. Chinese quick-add was visibly entered and persisted, and an exploratory OCR run created a task, but the executable used for that session differed from the current candidate. Those observations do not qualify the final binary. The [follow-up record](reports/2026-11-20-background-verification.md) records this boundary and fresh automated checks. Resume using the audited candidate, recording its hash, scaling, actions, actual results and fixture screenshots. Printing, a physical printer, real AI endpoints, fresh Windows machines and varied OCR language packs have not been accepted by these checks. The selftest's OCR availability probe is not an end-to-end OCR language/accuracy test.

## Manual desktop checks

Coordinate with the user before any native input. Launch a dedicated test instance with a new absolute `TASKPIN_DATA`, keep the user's normal process alive, use fixture-only windows, and retain only cropped non-sensitive evidence. Check editor tools/parameters, undo/redo, annotation edits, keyboard actions, clipboard image pixels, save output, direct pin placement/zoom/menu, task notes and calendar persistence after restart. Test each required physical monitor and DPI combination explicitly.

`TASKPIN_AUTOMATION=1` makes screenshot, pin and card widgets `Qt::Window` for official desktop accessibility tools. Normal sessions use `Qt::Tool`; the event handlers are shared, but window-manager behavior can differ and should be checked in normal mode too. Do not set this in ordinary use.

`TaskPinQt/scripts/test-snip.ps1` is a legacy desktop-input script. It is **never run automatically** by the new build/test/package scripts or CI. Review its assumptions and coordinate a dedicated interactive session before running it. Do not use historical counts from that script as current release acceptance.
