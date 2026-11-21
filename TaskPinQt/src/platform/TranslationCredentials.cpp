#include "platform/TranslationCredentials.h"
#include <QByteArray>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace TranslationCredentials {
static bool failure(QString *error)
{
    if (error) *error = QStringLiteral("Windows 凭据存储不可用，密钥未写入明文文件。");
    return false;
}
bool write(const QString &target, const QString &secret, QString *error)
{
#ifdef Q_OS_WIN
    QByteArray bytes = secret.toUtf8();
    if (bytes.isEmpty() || bytes.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return failure(error);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(target.utf16()));
    credential.CredentialBlob = reinterpret_cast<BYTE *>(bytes.data());
    credential.CredentialBlobSize = DWORD(bytes.size()); credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const bool ok = CredWriteW(&credential,0);
    SecureZeroMemory(bytes.data(),SIZE_T(bytes.size()));
    return ok ? true : failure(error);
#else
    Q_UNUSED(target) Q_UNUSED(secret) return failure(error);
#endif
}
QString read(const QString &target, QString *error)
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(reinterpret_cast<const wchar_t *>(target.utf16()),CRED_TYPE_GENERIC,0,&credential)) {
        if (GetLastError() != ERROR_NOT_FOUND) failure(error);
        return {};
    }
    const QString result = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),int(credential->CredentialBlobSize));
    CredFree(credential); return result;
#else
    Q_UNUSED(target) failure(error); return {};
#endif
}
bool remove(const QString &target, QString *error)
{
#ifdef Q_OS_WIN
    if (CredDeleteW(reinterpret_cast<const wchar_t *>(target.utf16()),CRED_TYPE_GENERIC,0) || GetLastError() == ERROR_NOT_FOUND) return true;
#else
    Q_UNUSED(target)
#endif
    return failure(error);
}
}
