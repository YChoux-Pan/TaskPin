# Isolated desktop regression checks

Use a disposable `TASKPIN_DATA` directory **and** `TASKPIN_AUTOMATION=1` when
running development copies alongside an installed TaskPin session. A custom data
directory selects stable path-derived command and single-instance pipe names;
without a custom directory, the legacy normal-session names remain compatible.
Every CLI invocation must inherit the same environment as its target app.

Automation mode skips global hotkey registration and logon/autostart writes,
including after settings are applied. Capture commands, notes, settings and pins
remain available. Screenshot, card and pin windows use the ordinary window type
in this mode so native automation tools can enumerate them; their real input
handlers and all other window flags are retained. Normal sessions continue using
tool windows. Automation windows can therefore appear in the Windows taskbar and
Alt+Tab list and participate in ordinary window activation/focus; normal tool
windows omit that taskbar/Alt+Tab presence. Frameless styling remains in both
modes. This diagnostic difference enables native desktop automation and must not
be treated as normal-session taskbar/focus behavior. This is a testing setting,
not a separate product mode.

Build with `BUILD_TESTING=ON`, then run `ctest --output-on-failure`. Both Qt test
targets deploy their Qt Test and offscreen runtime dependencies on Windows.
`TaskPinIntegrationTests` runs real pin widgets, invokes settings application on
the real host, and starts two isolated child apps to verify command routing and
single-instance behavior. Its temporary data is removed after the test.

Use `QT_QPA_PLATFORM=offscreen` for widget/IPC tests. If fonts are missing, set
`QT_QPA_FONTDIR` to the operating system's font directory. Native capture, visual
appearance and native click-through still require a desktop check; offscreen
tests verify internal click-through state and recovery, not Windows hit testing.

`--dump <file>` adds `instance` identity, `pins.items` state, and `snip.actions`
entries with stable action ids, enabled flags and global logical geometry while
preserving existing fields. Dumps are local diagnostics and can contain local
paths and user configuration. Publish only reviewed, sanitized evidence; do not
commit raw dumps, databases or logs. On shells where QtTest stdout is empty,
request a local result file with `-o <file>,txt` and inspect it separately.
