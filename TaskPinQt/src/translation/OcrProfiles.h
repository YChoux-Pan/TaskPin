#pragma once
#include <QString>

namespace Translation {
// Local-only selection shared by note capture and screenshot translation.
// Model options are deliberately bounded to the tested RapidOCR adapter.
struct OcrProfiles {
    QString profile = QStringLiteral("builtin-ppocrv5-mobile");
    QString customProfilePath;
    static QString settingsPath();
    static OcrProfiles load(QString *error = nullptr);
    QString profilePath() const;
    bool validate(QString *error = nullptr,bool verifyHashes = true) const;
    bool save(QString *error = nullptr,bool verifyHashes = true) const;
};
}
