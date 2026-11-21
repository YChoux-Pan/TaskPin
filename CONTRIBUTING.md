# Contributing

Bug reports and focused pull requests are welcome. Chinese and English reports are both useful. Start with [BUILDING](docs/BUILDING.md), [TESTING](docs/TESTING.md) and [ARCHITECTURE](docs/ARCHITECTURE.md).

1. Open an issue for substantial behavior or architecture changes; describe the user action, expected result and proposed scope.
2. Work on a focused branch. Keep C++17 and Qt >= 6.4 compatibility, the task database schema, user settings, notes/card/calendar flows and shortcuts stable.
3. Keep Win32 API includes inside `TaskPinQt/src/platform/`. Keep storage free of UI code. Do not add runtime network dependencies to icons or local screenshot features.
4. Build Release and run the safe test script. Add meaningful behavioral coverage for changed logic. For interaction changes, state which desktop/DPI checks were actually observed and which remain untested.
5. Update user-facing docs and the changelog when behavior changes. Include reproduction steps and test evidence in the pull request.

Never commit databases, API keys, settings, logs, personal screenshots, build caches, toolchains or generated release archives. Use synthetic fixtures and isolated `TASKPIN_DATA`. Redact issue attachments before uploading. Do not run native desktop automation while someone is using the computer.

Keep diffs scoped; avoid unrelated formatting and generated files. Source assets from third parties need attribution and their complete license notices. TaskPin's own code is Unlicense; contributions are submitted under the same project license unless explicitly agreed otherwise. Report suspected credential exposure privately to the repository maintainer rather than including secrets in a public issue.
