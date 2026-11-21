#pragma once
#include <QString>

namespace TranslationCredentials {
bool write(const QString &target, const QString &secret, QString *error);
QString read(const QString &target, QString *error = nullptr);
bool remove(const QString &target, QString *error);
}
