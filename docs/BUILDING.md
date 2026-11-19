# Build and package on Windows

Use Windows 10/11 x64, PowerShell 5.1 or newer, Git, CMake >= 3.20, Ninja and a matching Qt/MinGW kit. The baseline is Qt 6.4.3 `mingw_64` with MinGW 11.2 x64 and C++17. Install the Qt SVG module (`qtsvg`) and Qt Test alongside Core, Gui, Widgets, Sql, Network and PrintSupport. Newer Qt 6 kits must be verified separately; keep the source compatible with Qt >= 6.4.

## Configure and build

From the repository root:

```powershell
$env:QT_KIT = 'C:/Qt/6.4.3/mingw_64'
$env:PATH = 'C:/Qt/Tools/mingw1120_64/bin;C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;' + $env:PATH
cmd /c TaskPinQt\build.cmd
```

The `.cmd` wrapper forwards arguments and the PowerShell exit status. Equivalent explicit invocation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/build.ps1 `
  -QtKit 'C:/Qt/6.4.3/mingw_64' `
  -Compiler 'C:/Qt/Tools/mingw1120_64/bin/g++.exe' `
  -CMake 'C:/Qt/Tools/CMake_64/bin/cmake.exe' `
  -Ninja 'C:/Qt/Tools/Ninja/ninja.exe' `
  -BuildDir 'TaskPinQt/build' -Parallel 2
```

Inputs accept parameters or corresponding environment variables:

| Parameter | Environment | Meaning |
|---|---|---|
| `QtKit` | `QT_KIT` | Kit root containing `bin/` and `lib/cmake/Qt6/` |
| `QtRoot` | `QT_ROOT` | Installation root; discovers version folders with `mingw_64` |
| `Qt6Dir` | `Qt6_DIR` | Directory containing `Qt6Config.cmake` |
| `CmakePrefixPath` | `CMAKE_PREFIX_PATH` | Semicolon-separated CMake prefixes; kit prefixes supported |
| `Compiler` | `CXX` | Matching `g++.exe` |
| `CMake` / `Ninja` | `CMAKE` / `NINJA` | Tool executable paths |

When unspecified, tools are found in the Qt installation's `Tools/` directories or PATH. A Qt kit can also be inferred from `qmake.exe` on PATH. If several Qt kits are installed, choose one explicitly. CMake paths are normalized to forward slashes, including paths passed on Windows. The build is Release with `BUILD_TESTING=ON`; existing QtTest targets remain enabled. All failed native commands cause a failing script exit. Use a new build directory when changing compiler or Qt kit; do not reuse a cache from another kit.

The application and deployed Qt DLLs are in `TaskPinQt/build/`. CMake's `windeployqt` step also deploys test dependencies; this directory is a developer build, not the release archive.

## Test, then package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File TaskPinQt/scripts/package.ps1 -QtKit 'C:/Qt/6.4.3/mingw_64'
```

`test.ps1` also accepts `BuildDir`, `ReportDir` and `CMake`. `package.ps1` accepts the build toolchain parameters and `BuildDir` / `OutputDir` (default `dist`). It requires a CMake Release build, creates a unique fresh staging directory, copies only the application executable, and runs `windeployqt --compiler-runtime` using that kit's explicit `qtpaths.exe`. Deployment detects the Release executable rather than forcing a plugin build-type filter (Qt 6.4 MinGW kits can share plugin filenames). It adds the end-user README and license notices. Staging remains for inspection; only final named assets are published. It never copies the developer build tree, database, logs, QtTest executables, CMake caches or import libraries.

Outputs:

- `TaskPin-0.3.0-windows-x64.zip`
- `TaskPin-0.3.0-source.zip`
- `SHA256SUMS.txt` (SHA-256 of the completed archives)

The source ZIP uses `git archive HEAD`, **not uncommitted edits**. Commit reviewed release changes before generating the source archive. `.gitattributes` excludes internal implementation work records. Run build/test against that same committed state. See [RELEASING.md](RELEASING.md) for release gates, download verification and licensing obligations.
