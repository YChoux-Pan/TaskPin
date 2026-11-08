#include "core/Ocr.h"

#include "core/Settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

// ASCII only: this file is written to disk and executed by Windows PowerShell 5.1, which
// reads script files as ANSI unless they carry a BOM.
const char *kHelper = R"PS(
param(
  [Parameter(Mandatory = $true)][string]$Image,
  [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'

function Write-Result($obj) {
  $json = $obj | ConvertTo-Json -Depth 6 -Compress
  [System.IO.File]::WriteAllText($Out, $json, (New-Object System.Text.UTF8Encoding($false)))
}

try {
  Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
  [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
  [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
  [Windows.Storage.StorageFile, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
  [Windows.Globalization.Language, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null

  $asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
      $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
      $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
    })[0]

  function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
  }

  $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Image)) ([Windows.Storage.StorageFile])
  $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
  $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
  $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])

  $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
  if ($null -eq $engine) {
    $lang = New-Object Windows.Globalization.Language 'zh-Hans-CN'
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
  }
  if ($null -eq $engine) { throw 'no OCR language pack is installed' }

  $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
  $lines = @()
  foreach ($line in $result.Lines) { $lines += [string]$line.Text }
  Write-Result ([pscustomobject]@{ ok = $true; text = [string]$result.Text; lines = $lines; language = [string]$engine.RecognizerLanguage.LanguageTag })
  exit 0
}
catch {
  $msg = [string]$_.Exception.Message
  $inner = $_.Exception.InnerException
  while ($null -ne $inner) {
    $msg = $msg + ' | ' + [string]$inner.Message
    $inner = $inner.InnerException
  }
  try { Write-Result ([pscustomobject]@{ ok = $false; error = $msg }) } catch { }
  exit 1
}
)PS";

QString powershellPath()
{
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    const QString path = root + QStringLiteral("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    if (QFileInfo::exists(path)) return path;
    return QStringLiteral("powershell.exe");
}

/// Windows OCR puts a space between CJK glyphs and sometimes splits digits; drop those but keep
/// real word spacing between latin words.
QString normalizeCjkSpacing(const QString &text)
{
    auto wide = [](QChar c) {
        const ushort u = c.unicode();
        // CJK ideographs, CJK punctuation, and full-width forms
        return (u >= 0x3000 && u <= 0x303F) || (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x4E00 && u <= 0x9FFF) ||
               (u >= 0xF900 && u <= 0xFAFF) || (u >= 0xFF00 && u <= 0xFFEF);
    };
    auto digit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };

    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char(' ') && !out.isEmpty() && i + 1 < text.size()) {
            const QChar prev = out.at(out.size() - 1);
            const QChar next = text.at(i + 1);
            if (wide(prev) || wide(next)) continue;
            if (digit(prev) && digit(next)) continue;
        }
        out.append(c);
    }
    return out;
}

}   // namespace

Ocr::Ocr(QObject *parent) : QObject(parent) {}

Ocr::~Ocr()
{
    cleanup();
}

bool Ocr::available()
{
    return QFileInfo::exists(powershellPath());
}

bool Ocr::writeHelper()
{
    const QString dir = Settings::dataDir();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/ocr-helper.ps1");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "cannot write OCR helper:" << path;
        return false;
    }
    f.write(kHelper);
    f.close();
    return true;
}

void Ocr::recognize(const QImage &image)
{
    if (busy()) {
        emit failed(QStringLiteral("上一次识别还没结束"));
        return;
    }
    if (image.isNull()) {
        emit failed(QStringLiteral("空图像"));
        return;
    }
    if (!writeHelper()) {
        emit failed(QStringLiteral("无法写入 OCR 辅助脚本"));
        return;
    }

    const QString dir = Settings::dataDir() + QStringLiteral("/ocr");
    QDir().mkpath(dir);
    m_imagePath = dir + QStringLiteral("/input.png");
    m_resultPath = dir + QStringLiteral("/result.json");
    QFile::remove(m_resultPath);
    if (!image.save(m_imagePath, "PNG")) {
        emit failed(QStringLiteral("无法写入临时图像"));
        return;
    }

    // Windows.Storage rejects forward slashes, and PowerShell reads the arguments verbatim,
    // so every path handed to the helper must be native.
    const QString helper = QDir::toNativeSeparators(Settings::dataDir() + QStringLiteral("/ocr-helper.ps1"));
    const QString nativeImage = QDir::toNativeSeparators(m_imagePath);
    const QString nativeResult = QDir::toNativeSeparators(m_resultPath);

    m_process = new QProcess(this);
    m_process->setProgram(powershellPath());
    m_process->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                             QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"),
                             helper, QStringLiteral("-Image"), nativeImage, QStringLiteral("-Out"), nativeResult});
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QByteArray stdErr = m_process ? m_process->readAll() : QByteArray();
        QFile f(m_resultPath);
        QString reason = QStringLiteral("OCR 失败");
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            if (o.value(QStringLiteral("ok")).toBool()) {
                QString text = o.value(QStringLiteral("text")).toString();
                text = normalizeCjkSpacing(text);
                const QString language = o.value(QStringLiteral("language")).toString();
                qInfo("ocr ok lines=%d lang=%s", int(o.value(QStringLiteral("lines")).toArray().size()),
                      qUtf8Printable(language));
                cleanup();
                emit finished(text);
                return;
            }
            reason = o.value(QStringLiteral("error")).toString(reason);
        } else if (code != 0 && !stdErr.isEmpty()) {
            reason = QString::fromLocal8Bit(stdErr).trimmed();
        }
        finishWithError(reason);
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) finishWithError(QStringLiteral("无法启动 PowerShell"));
    });

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { finishWithError(QStringLiteral("OCR 超时")); });
    m_timeout->start(20000);

    qInfo("ocr start %dx%d", image.width(), image.height());
    m_process->start();
}

void Ocr::finishWithError(const QString &reason)
{
    qWarning() << "ocr failed:" << reason;
    cleanup();
    emit failed(reason);
}

void Ocr::cleanup()
{
    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}
