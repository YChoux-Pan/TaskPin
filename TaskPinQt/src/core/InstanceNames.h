#pragma once
#include <QString>

struct InstanceNames {
    QString command;
    QString singleInstance;
};

/// An empty custom directory preserves the normal session's legacy pipe names.
InstanceNames instanceNamesFor(const QString &customDataDirectory);
bool taskPinAutomation();
