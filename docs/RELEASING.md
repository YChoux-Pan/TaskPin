# Releasing TaskPin 0.3.0

The standard release artifacts are `TaskPin-0.3.0-windows-x64.zip`, `TaskPin-0.3.0-source.zip` and `SHA256SUMS.txt`. The portable ZIP contains the executable, Release runtime/plugins, end-user README, project license and third-party notices. Source is a `git archive` of the tagged commit; internal implementation work records are export-ignored.

## Before tagging

1. Review the focused branch/PR, version in `TaskPinQt/CMakeLists.txt`, changelog, READMEs, notices and safe screenshots. Commit the final changes; local packaging uses HEAD.
2. Build Release with the pinned reference Qt 6.4.3 / MinGW 11.2 x64 kit, run `test.ps1` and inspect the reports. Windows CI must pass too.
3. Complete the pending native editor/output/notes acceptance with the user; record exact actions, scaling and observed results. Do not promote automated DPI fixtures or old screenshots into native acceptance. Document printer/AI/physical-monitor limits honestly.
4. Run `package.ps1`, inspect the fresh staging manifest and ZIP contents for runtime completeness and excluded private/development files. Test the extracted executable in a separate data directory and, where available, on a clean Windows machine.
5. Review Qt dynamic-linking replacement rights, corresponding upstream source links and all bundled license notices. Keep notices for the actual deployed compiler/runtime versions.

## Publish once

After review and successful Windows branch CI, the maintainer/controller pushes tag `v0.3.0` at the approved commit. `.github/workflows/release.yml` independently checks the tag against CMake's version, builds Release, runs QtTest and selftest, packages both archives and computes SHA-256 only after tests succeed. Its final release step uploads the three exact files. Failing tests or packaging prevent release upload; reports are retained even on failure.

The workflow is the publisher for the standard assets. The maintainer/controller waits for that tag workflow to finish before changing final release text or adding supplemental assets. Do not pre-create/upload a competing set of assets while it runs. GitHub's automatically generated source downloads are separate from the explicit, checksummed source ZIP.

## Verify the distributed bytes

Download both ZIPs and `SHA256SUMS.txt` from the actual GitHub release after upload. Recompute each checksum:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\TaskPin-0.3.0-windows-x64.zip
Get-FileHash -Algorithm SHA256 -LiteralPath .\TaskPin-0.3.0-source.zip
```

Compare both values with the downloaded manifest, inspect archive paths and extract into a fresh folder. Launch only an isolated `TASKPIN_DATA` session for smoke testing. Preserve a user's existing normal process until the coordinated upgrade. Rebuilding locally alone does not verify the uploaded bytes; ZIP timestamps may also make independently rebuilt archives differ.

Release text should state the capture/pin improvements, original-position fix, supported Windows/Qt baseline, known limitations and observed test evidence. Never claim physical printer/AI/mixed-monitor acceptance when only fixtures ran. If release preparation fails, repair on the branch and repeat verification; do not silently replace an already consumed tag with unrelated source.

## Workflow maintenance

Actions were checked against upstream definitions on 2026-11-19: checkout `v7.0.1`, upload-artifact `v7.0.2` and action-gh-release `v3.0.4` use Node 24; install-qt-action `v4.5.0` is composite and supports explicit version/arch/modules/tools. Windows hosted runners provide the compatible action runtime. The Qt kit is pinned to the 6.4.3 `win64_mingw` base package and `tools: tools_mingw90,qt.tools.win64_mingw900`. Despite the legacy repository/variant names, Qt metadata identifies this as MinGW 11.2.0 64-bit (archive `mingw-w64-x86_64-11.2.0-release-posix-seh-rt_v9-rev3.7z`); the workflow also requires compiler-reported GCC version `11.2.0`. The action converts commas to spaces and aqt 3.3 accepts a tool name plus variant; adding an empty version field does not select a different package. This base package already contains QtSvg; do not request `modules: qtsvg`, which aqt rejects for 6.4.3. The Release build configures CMake with a required SVG component, so a missing QtSvg cannot silently pass. Verify current upstream action/tool definitions before changing versions. Do not invent commit hashes or assume an older major is latest.
