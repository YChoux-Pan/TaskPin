# TaskPin OCR performance and pin experience design

User scope: benchmark real local PaddleOCR separately from the configured translation service; make PaddleOCR model selection extensible; fix reversed wheel zoom and ghost edges; make text comparison opaque and proportionate. Final acceptance uses Playwright Test + GitHub Actions without desktop mouse automation. Existing authorization covers implementation, native installation, main merge and release.

TaskPin remains C++17/Qt6.4 Widgets. Playwright's runner invokes real Qt offscreen probes and checks their JSON/images, rather than pretending browser automation can operate QWidget. GitHub Windows builds, full Qt regression, actual model fixture tests, Playwright native contracts, package and selftest are gates. Paid translation benchmarks use generated content and existing credentials in memory only; CI uses deterministic local fixtures, no secrets.

OCR defaults to bundled PP-OCRv5 mobile. A validated model profile selects detection/recognition ONNX paths, model family/type, SHA256 and identity, bounded CPU options. Custom profiles enable compatible PaddleOCR model variants through RapidOCR; incompatible weights fail visibly, never trigger automatic model downloads. Settings preserve cancel/save behavior and notes/translation share selection. Measure cold and repeated work before choosing a process reuse optimization; publish stage timings and limits honestly.

Zoom direction changes must respond immediately from displayed scale, preserve the image point under the wheel cursor, and redraw the full translucent backing surface. Smooth rendering must not switch to a visibly different low-quality image while shrinking. Test negative origins/DPR/reversal and stable anchor geometry with internal Qt events, no OS cursor movement.

Comparison: opaque standalone dialog surface with parent-owned lifetime, full opacity independent of pin opacity; compact heading/status, equal resizable source/Chinese panes taking most height, clear copy buttons. Initial size constrained to available screen, useful minimum size. Generated fixture screenshots validate light/dark parent and long paragraphs.

Version target 0.4.3. Preserve all user tasks/settings/key/history except legitimate new default fields. Source/main/tag/released bytes must match; install downloaded verified package, update shortcut and retain D:/TaskPin app/repo/release only.

User steering: F3 while the F1 editor is active pins the composed selection; outside the editor it pins clipboard content. An empty selection must not cause clipboard pinning. Add a configurable editor-only translation shortcut (default Ctrl+T) to Settings → Hotkeys, clearly scoped to the active selection. Hints/runtime follow configured keys and actual pin fallback. Test defaults/custom keys, focus in annotation text, save/reload/cancel and collisions with global/editor actions.
