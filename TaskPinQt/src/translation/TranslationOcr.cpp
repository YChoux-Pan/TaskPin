#include "translation/TranslationOcr.h"
#include <QBuffer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <cmath>

namespace Translation {
// The screenshot travels through stdin and an in-memory WinRT stream. Neither image nor
// recognized text is saved by this adapter. stdout is bounded and never logged.
static const char helper[] = R"PS(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
try {
 Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
 [Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null
 [Windows.Graphics.Imaging.BitmapDecoder,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null
 [Windows.Storage.Streams.InMemoryRandomAccessStream,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null
 [Windows.Storage.Streams.DataWriter,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null
 [Windows.Globalization.Language,Windows.Foundation,ContentType=WindowsRuntime] | Out-Null
 $method = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
 function Await($operation,$type) {
  $task = $method.MakeGenericMethod($type).Invoke($null,@($operation))
  $task.Wait(-1) | Out-Null
  $task.Result
 }
 $inputData = [Console]::In.ReadToEnd() | ConvertFrom-Json
 $bytes = [Convert]::FromBase64String([string]$inputData.image)
 $stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
 $writer = New-Object Windows.Storage.Streams.DataWriter($stream.GetOutputStreamAt(0))
 $writer.WriteBytes($bytes)
 $null = Await ($writer.StoreAsync()) ([uint32])
 $null = Await ($writer.FlushAsync()) ([bool])
 $writer.DetachStream() | Out-Null
 $stream.Seek(0)
 $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
 $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
 $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
 if ($null -eq $engine) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language 'en-US')) }
 if ($null -eq $engine) { throw 'ocr_language_unavailable' }
 $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
 $lines = @()
 foreach ($line in $result.Lines) {
  $left = [double]::PositiveInfinity; $top = [double]::PositiveInfinity
  $right = 0.0; $bottom = 0.0
  foreach ($word in $line.Words) {
   $r = $word.BoundingRect
   $left = [Math]::Min($left,$r.X); $top = [Math]::Min($top,$r.Y)
   $right = [Math]::Max($right,$r.X+$r.Width); $bottom = [Math]::Max($bottom,$r.Y+$r.Height)
  }
  if ($right -gt $left -and $bottom -gt $top) {
   $lines += @{text=[string]$line.Text;x=$left;y=$top;width=$right-$left;height=$bottom-$top}
  }
 }
 [Console]::Out.WriteLine((@{ok=$true;language=[string]$engine.RecognizerLanguage.LanguageTag;lines=$lines} | ConvertTo-Json -Depth 6 -Compress))
 $bitmap.Dispose(); $stream.Dispose()
} catch {
 [Console]::Out.WriteLine('{"ok":false}')
 exit 1
}
)PS";
Ocr::Ocr(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer,&QTimer::timeout,this,[this] { fail(QStringLiteral("本地 OCR 超时，请缩小选区重试。")); });
}
Ocr::~Ocr() { cancel(); }
void Ocr::cancel()
{
    ++m_generation; m_timer->stop(); m_output.clear();
    if (m_process) {
        auto *process = m_process.data(); m_process.clear();
        disconnect(process,nullptr,this,nullptr);
        process->setParent(nullptr);
        if (process->state() != QProcess::NotRunning) {
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
            process->kill();
        } else process->deleteLater();
    }
}
void Ocr::fail(const QString &message)
{
    const QString id = m_taskId; cancel(); emit failed(id,message);
}
void Ocr::recognize(const QString &taskId, const QImage &image)
{
    cancel(); m_taskId = taskId;
    if (image.isNull() || image.width() < 8 || image.height() < 8 || qint64(image.width())*image.height()>16000000) {
        emit failed(taskId,QStringLiteral("截图为空、过小或超过 1600 万像素，请重新框选。")); return;
    }
    const QString shell = qEnvironmentVariable("SystemRoot","C:/Windows")+"/System32/WindowsPowerShell/v1.0/powershell.exe";
    if (!QFileInfo::exists(shell)) { emit failed(taskId,QStringLiteral("未找到 Windows OCR 运行环境。")); return; }
    const QString script = QString::fromLatin1(helper);
    const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()),script.size()*2);
    m_size = image.size();
    auto *process = new QProcess(this); m_process = process;
    process->setProgram(shell);
    process->setArguments({"-NoProfile","-NonInteractive","-EncodedCommand",QString::fromLatin1(utf16.toBase64())});
    const auto generation = m_generation;
    connect(process,&QProcess::started,this,[this,process,image,generation] {
        if (generation != m_generation) return;
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG")) { fail(QStringLiteral("无法准备 OCR 输入。")); return; }
        process->write(QJsonDocument(QJsonObject{{"image",QString::fromLatin1(png.toBase64())}}).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
    });
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,generation] {
        if (generation != m_generation) return;
        m_output += process->readAllStandardOutput();
        if (m_output.size()>1024*1024) fail(QStringLiteral("OCR 文本超过大小限制。"));
    });
    connect(process,&QProcess::readyReadStandardError,this,[process] { process->readAllStandardError(); });
    connect(process,&QProcess::finished,this,[this,process,generation](int,QProcess::ExitStatus) {
        if (generation != m_generation || m_process != process) return;
        m_output += process->readAllStandardOutput();
        OcrResult result; QString error;
        const bool ok = decode(m_output,m_size,&result,&error);
        const QString id = m_taskId; cancel();
        if (ok) emit finished(id,result); else emit failed(id,error);
    });
    connect(process,&QProcess::errorOccurred,this,[this,generation](QProcess::ProcessError error) {
        if (generation == m_generation && error == QProcess::FailedToStart) fail(QStringLiteral("无法启动本地 OCR。"));
    });
    m_timer->start(30000); process->start();
}
bool Ocr::decode(const QByteArray &json, const QSize &imageSize, OcrResult *result, QString *error)
{
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(json.trimmed(),&parse);
    const auto object = document.object();
    *error = QStringLiteral("Windows OCR 无法识别，请检查系统 OCR 语言包，或缩小截图范围。原图未上传。");
    if (json.size()>1024*1024 || parse.error != QJsonParseError::NoError || !object.value("ok").toBool() || !object.value("lines").isArray()) return false;
    result->imageSize = imageSize; result->language = object.value("language").toString();
    const auto lines = object.value("lines").toArray();
    if (lines.size()>512) { *error = QStringLiteral("OCR 行数过多，请缩小范围。"); return false; }
    int chars = 0;
    for (const auto &value : lines) {
        const auto line = value.toObject();
        QRectF rect(line.value("x").toDouble(-1),line.value("y").toDouble(-1),line.value("width").toDouble(-1),line.value("height").toDouble(-1));
        const QString text = line.value("text").toString(); chars += text.size();
        if (chars > 32000 || !rect.isValid() || rect.left()<0 || rect.top()<0 || rect.right()>imageSize.width()+1 || rect.bottom()>imageSize.height()+1) return false;
        if (text.trimmed().isEmpty()) continue;
        result->lines.append({QStringLiteral("line-%1").arg(result->lines.size()+1),text,rect,std::nullopt});
    }
    return true;
}
}
