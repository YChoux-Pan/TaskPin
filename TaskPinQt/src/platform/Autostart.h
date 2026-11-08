#pragma once

#include <QString>

/// HKCU\...\CurrentVersion\Run entry so the app comes back after a logon.
namespace Autostart {

bool isEnabled();
void setEnabled(bool enabled, const QString &executablePath);

}   // namespace Autostart
