#include "platform/Autostart.h"

#include <QDebug>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t *kValue = L"TaskPinQt";
#endif

}   // namespace

namespace Autostart {

bool isEnabled()
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, kValue, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_SZ && buffer[0] != L'\0';
#else
    return false;
#endif
}

void setEnabled(bool enabled, const QString &executablePath)
{
#ifdef Q_OS_WIN
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return;

    if (!enabled) {
        RegDeleteValueW(key, kValue);
        RegCloseKey(key);
        return;
    }

    const QString native = QDir::toNativeSeparators(executablePath);
    const QString command = QStringLiteral("\"%1\"").arg(native);
    const std::wstring wide = command.toStdWString();
    RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE *>(wide.c_str()),
                   DWORD((wide.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    qInfo("autostart set: %s", qUtf8Printable(command));
#else
    Q_UNUSED(enabled) Q_UNUSED(executablePath)
#endif
}

}   // namespace Autostart
