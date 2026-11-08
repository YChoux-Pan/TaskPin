#pragma once

#include <QString>

/// Headless self test: store CRUD on a throw-away database plus DPI math.
/// Returns 0 when every assertion passed, 1 otherwise; writes <outDir>/selftest.txt.
int runSelfTest(const QString &outDir);
