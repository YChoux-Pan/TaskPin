#include "ui/TranslationWindow.h"
#include "ui/AppIcon.h"
#include "ui/TranslationSettingsDialog.h"
#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include "platform/TranslationCredentials.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QScrollArea>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

TranslationWindow::TranslationWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("TaskPin 截图翻译")); setObjectName("translationWindow"); setWindowIcon(taskpinIcon());
    setAttribute(Qt::WA_DeleteOnClose); resize(980,680); setMinimumSize(700,480);
    auto *layout = new QVBoxLayout(this); auto *toolbar = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("截图翻译"),this); title->setStyleSheet("font-size:20px;font-weight:600"); toolbar->addWidget(title);
    toolbar->addStretch();
    auto *configure = new QPushButton(QStringLiteral("翻译设置"),this); configure->setObjectName("translationSettings");
    connect(configure,&QPushButton::clicked,this,&TranslationWindow::settings); toolbar->addWidget(configure);
    auto *pin = new QCheckBox(QStringLiteral("置顶"),this);
    connect(pin,&QCheckBox::toggled,this,[this](bool enabled) { setWindowFlag(Qt::WindowStaysOnTopHint,enabled); show(); }); toolbar->addWidget(pin);
    auto *recapture = new QPushButton(QStringLiteral("重新截图"),this); recapture->setObjectName("translationRecapture");
    connect(recapture,&QPushButton::clicked,this,[this] { cancel(); emit recaptureRequested(); }); toolbar->addWidget(recapture); layout->addLayout(toolbar);
    m_status = new QLabel(QStringLiteral("Ctrl+Alt+T 框选屏幕，截图只在本机识别。"),this); m_status->setObjectName("translationStatus");
    m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
    m_warning = new QLabel(this); m_warning->setTextFormat(Qt::PlainText); m_warning->setWordWrap(true); m_warning->setStyleSheet("color:#a77827"); layout->addWidget(m_warning);
    auto *splitter = new QSplitter(this);
    auto pane = [this,splitter](const QString &label,const QString &name,QPlainTextEdit **editor) {
        auto *panel = new QWidget(splitter); auto *box = new QVBoxLayout(panel); auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label,panel)); row->addStretch();
        auto *copy = new QPushButton(QStringLiteral("复制"),panel); copy->setObjectName(name+"Copy"); row->addWidget(copy);
        *editor = new QPlainTextEdit(panel); (*editor)->setReadOnly(true); (*editor)->setObjectName(name);
        (*editor)->setLineWrapMode(QPlainTextEdit::WidgetWidth); box->addLayout(row); box->addWidget(*editor);
        connect(copy,&QPushButton::clicked,this,[this,editor] {
            const QString text = (*editor)->toPlainText();
            QApplication::clipboard()->setText(text);
            m_status->setText(QApplication::clipboard()->text() == text ? QStringLiteral("已复制文本。") :
                QStringLiteral("剪贴板被其他程序占用，复制未完成，请稍后重试。"));
        });
    };
    pane(QStringLiteral("OCR 原文"),"translationSource",&m_source); pane(QStringLiteral("简体中文"),"translationTarget",&m_target);
    layout->addWidget(splitter,1);
    auto *bottom = new QHBoxLayout;
    m_preview = new QLabel(this); m_preview->setObjectName("translationPreview"); m_preview->setFixedSize(220,110); m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background:#ECEEF3;border-radius:6px;color:#2E333B"); m_preview->setText(QStringLiteral("原图预览")); bottom->addWidget(m_preview);
    auto *view = new QPushButton(QStringLiteral("查看原图"),this);
    connect(view,&QPushButton::clicked,this,[this] {
        if (m_image.isNull()) return;
        auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(QStringLiteral("截图原图 · 请核对公式与参数"));
        auto *vbox = new QVBoxLayout(dialog); auto *scroll = new QScrollArea(dialog);
        auto *label = new QLabel; label->setPixmap(QPixmap::fromImage(m_image)); scroll->setWidget(label); vbox->addWidget(scroll);
        dialog->resize(800,550); dialog->show();
    }); bottom->addWidget(view); bottom->addStretch();
    m_cancel = new QPushButton(QStringLiteral("取消处理"),this); m_cancel->setObjectName("translationCancel");
    connect(m_cancel,&QPushButton::clicked,this,&TranslationWindow::cancel); bottom->addWidget(m_cancel);
    m_retry = new QPushButton(QStringLiteral("翻译 / 重试"),this); m_retry->setObjectName("translationRetry");
    connect(m_retry,&QPushButton::clicked,this,&TranslationWindow::translate); bottom->addWidget(m_retry);
    auto *close = new QPushButton(QStringLiteral("关闭"),this); connect(close,&QPushButton::clicked,this,&QWidget::close); bottom->addWidget(close);
    layout->addLayout(bottom);
    m_ocr = new Translation::Ocr(this); m_client = new Translation::Client(this);
    connect(m_ocr,&Translation::Ocr::finished,this,&TranslationWindow::presentOcr);
    connect(m_ocr,&Translation::Ocr::failed,this,[this](const QString &id,const QString &error) { if (id == m_taskId) setStatus("FAILED",error); });
    connect(m_client,&Translation::Client::updated,this,[this](const QString &id,const QList<Translation::Result> &results,int done,int total) {
        if (id != m_taskId) return;
        m_target->setPlainText(Translation::resultText(m_blocks,results));
        setStatus("TRANSLATING",QStringLiteral("正在翻译… %1 / %2 块；截图未上传").arg(done).arg(total));
    });
    connect(m_client,&Translation::Client::finished,this,[this](const QString &id,const QList<Translation::Result> &results) {
        if (id != m_taskId) return;
        m_target->setPlainText(Translation::resultText(m_blocks,results));
        bool failed = false, mock = false;
        for (const auto &result : results) { failed |= result.status == "failed"; mock |= result.status == "mock"; }
        setStatus("PRESENTING",mock ? QStringLiteral("离线 Mock 已完成 · 仅测试流程，不是真实译文。") :
            failed ? QStringLiteral("部分文本未通过校验，已标记并保留原文。") : QStringLiteral("翻译完成 · 代码、参数和公式请对照原图核对。"));
    });
    connect(m_client,&Translation::Client::failed,this,[this](const QString &id,const QString &error) { if (id == m_taskId) setStatus("FAILED",error); });
    setStatus("IDLE",m_status->text());
}
TranslationWindow::~TranslationWindow() { m_ocr->cancel(); m_client->cancel(); }
bool TranslationWindow::busy() const { return m_ocr->busy() || m_client->busy(); }
void TranslationWindow::setStatus(const QString &state,const QString &label)
{
    m_state = state; m_status->setText(label);
    m_retry->setEnabled(!busy() && !m_blocks.isEmpty()); m_cancel->setEnabled(busy());
}
void TranslationWindow::setCapture(const QImage &image,const QRect &region)
{
    Q_UNUSED(region)
    cancel(); m_taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_image = image; m_blocks.clear(); m_source->clear(); m_target->clear(); m_warning->clear();
    m_preview->setPixmap(QPixmap::fromImage(image).scaled(m_preview->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    show(); raise(); activateWindow();
    m_ocr->recognize(m_taskId,image);
    if (m_ocr->busy()) setStatus("OCR_RUNNING",QStringLiteral("本地 OCR 识别中… 图片不上传，不写入截图历史。"));
}
void TranslationWindow::presentOcr(const QString &taskId,const Translation::OcrResult &result)
{
    if (taskId != m_taskId || m_state == "CANCELLED") return;
    QStringList warnings; m_blocks = Translation::buildBlocks(result,&warnings);
    if (!result.language.startsWith("en",Qt::CaseInsensitive))
        warnings.append(QStringLiteral("本次 OCR 语言为 %1；英文技术文档的下划线、l/I、数字与比较符需核对原图。").arg(result.language));
    if (m_blocks.isEmpty()) { setStatus("PRESENTING",QStringLiteral("选区未识别到文字，请重新截图。")); return; }
    for (auto &block : m_blocks) block = Translation::protect(block,m_taskId.left(8));
    m_source->setPlainText(Translation::sourceText(m_blocks)); m_warning->setText(warnings.join('\n'));
    setStatus("PRESENTING",QStringLiteral("本地识别完成（%1）").arg(result.language));
    translate();
}
void TranslationWindow::translate()
{
    if (busy() || m_blocks.isEmpty()) return;
    const auto config = Translation::Config::load();
    if (config.mode == "local") { setStatus("PRESENTING",QStringLiteral("纯本地 OCR · 未联网。需要译文时打开“翻译设置”选择服务并授权。")); return; }
    QString error;
    const QString key = config.mode == "cloud" ? TranslationCredentials::read(config.credentialTarget(),&error) : QString();
    if (!error.isEmpty()) { setStatus("FAILED",error); return; }
    m_client->start(m_taskId,m_blocks,config,key);
    if (m_client->busy()) setStatus("TRANSLATING",config.mode == "mock" ? QStringLiteral("离线 Mock 测试中…") : QStringLiteral("正在发送已授权的 OCR 文字，截图不上传…"));
}
void TranslationWindow::cancel()
{
    m_taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_ocr->cancel(); m_client->cancel();
    setStatus("CANCELLED",QStringLiteral("已取消。已发出的文字和费用无法撤回；原文仍可复制。"));
}
void TranslationWindow::settings()
{
    if (busy()) cancel();
    TranslationSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted && !m_blocks.isEmpty()) translate();
}
void TranslationWindow::closeEvent(QCloseEvent *event) { cancel(); QWidget::closeEvent(event); }
