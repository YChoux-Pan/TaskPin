# Third-party notices

TaskPin's own code is Unlicense; this does not replace the licenses of bundled third-party work. The portable archive includes this file, `LICENSE` and the complete license assets in `licenses/`.

## Embedded icon graphics

The TaskPin application brand icon was created in Canva and prepared for this project. Its preserved export, design provenance and asset preparation steps are documented in [BRANDING.md](https://github.com/YChoux-Pan/TaskPin/blob/main/docs/BRANDING.md).

- **Lucide** SVG paths in `TaskPinQt/src/ui/IconSet.cpp`: ISC, copyright (c) 2026 Lucide Icons and Contributors. The upstream license also retains Feather-derived icons' MIT notice, copyright (c) 2013-present Cole Bemis. Both notices are preserved in [Lucide-LICENSE](licenses/Lucide-LICENSE). Source: https://github.com/lucide-icons/lucide (upstream LICENSE blob `718bb3f0e44153809972abed31839375804bf652`).
- **Material Design Icons / Pictogrammers**, including the numbered-circle graphic: icon graphics are Apache-2.0, not the upstream code MIT license. [MDI-LICENSE](licenses/MDI-LICENSE) preserves the upstream Pictogrammers Free License distinctions between icons, fonts and code; [Apache-2.0.txt](licenses/Apache-2.0.txt) supplies the full Apache license. Source: https://github.com/Templarian/MaterialDesign (LICENSE blob `382f8a138afaaefe9ca2cfa80d33317f7814d997`). Full Apache text: https://github.com/apache/.github/blob/main/LICENSE . Graphics are embedded as SVG path fragments and colored/rendered by TaskPin; no font is bundled.

## Qt 6.4.3 dynamic runtime

Portable Windows builds dynamically link Qt Core, Gui, Widgets, Sql, Network, PrintSupport and Svg, together with deployed plugins. Qt is copyright The Qt Company Ltd. and other contributors, available under its applicable open-source/commercial terms. This distribution uses the open-source runtime under LGPLv3 where available. Complete texts: [Qt-LGPL-3.0.txt](licenses/Qt-LGPL-3.0.txt) and [Qt-GPL-3.0.txt](licenses/Qt-GPL-3.0.txt), copied from qt/qtbase LICENSES. Individual Qt modules and embedded third-party code retain their own notices: https://doc.qt.io/qt-6.4/licenses-used-in-qt.html .

Corresponding Qt 6.4.3 source archives, including module and third-party notices, are available at https://download.qt.io/archive/qt/6.4/6.4.3/ (the `submodules/` directory provides module sources). TaskPin applies no Qt source modifications. TaskPin source and build instructions are provided at https://github.com/YChoux-Pan/TaskPin and in the release source archive. Consumers can rebuild TaskPin or replace the shared Qt DLLs/plugins with compatible modified builds; preserve binary compatibility and the plugin directory structure. Nothing in TaskPin's terms restricts reverse engineering needed to debug modifications to the LGPL-covered libraries. No technical installation lock is applied to replacement Qt runtimes.

## MinGW runtime libraries

The x64 MinGW 11.2 runtime deployed with Qt includes `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll`. GCC libraries use GPLv3 with the GCC Runtime Library Exception where applicable; see [GCC-GPL-3.0.txt](licenses/GCC-GPL-3.0.txt) and [GCC-RUNTIME.txt](licenses/GCC-RUNTIME.txt). Sources: https://gcc.gnu.org/releases.html (GCC 11.2.0) and https://gcc.gnu.org/git/ .

MinGW-w64 and winpthreads carry their respective copyright notices and permissive licenses in [MinGW-w64-runtime.txt](licenses/MinGW-w64-runtime.txt) and [winpthreads-COPYING.txt](licenses/winpthreads-COPYING.txt), copied from the actual toolchain distribution. Source: https://www.mingw-w64.org/ and https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/mingw-w64-libraries/winpthreads/ . These exceptions and licenses permit their use with the independently licensed TaskPin application, subject to their terms.

Windows platform components (including system TLS) are supplied by Windows, not bundled as TaskPin source. No API credentials or Qt/MinGW development toolchain is included in the portable release.


## Bundled PaddleOCR CPU runtime

OCR uses PaddleOCR PP-OCRv5 mobile detection/recognition models with RapidOCR 3.10.0 preprocessing/inference wrappers, ONNX Runtime CPU and an isolated CPython runtime. TaskPin does not bundle or call another product's OCR components. Model and runtime identities, original URLs, SHA256 and dependency versions are recorded in `TaskPinQt/ocr/runtime-lock.json` and its lock file. The portable `ocr/` directory retains upstream model/runtime notices and every deployed wheel's license assets; the preparation script validates downloaded bytes before use. These components retain their own licenses, including PaddleOCR/RapidOCR Apache-2.0, ONNX Runtime MIT and Python PSF terms. Microsoft app-local VC runtime DLLs retain Microsoft's redistribution terms supplied with the official runtime. See `licenses/` and `ocr/licenses/` in the portable distribution for complete bundled notices.
