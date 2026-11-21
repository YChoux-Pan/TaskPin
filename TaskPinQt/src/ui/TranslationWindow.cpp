#include "ui/TranslationWindow.h"
#include "ui/AppIcon.h"
#include "ui/TranslationCanvas.h"
#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include <QComboBox>
#include <QStackedWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QScreen>
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
    m_view=new QComboBox(this);m_view->setObjectName("translationView");
    m_view->addItems({QStringLiteral("译图 · 原位覆盖"),QStringLiteral("原图"),QStringLiteral("文字对照")});toolbar->insertWidget(1,m_view);
    m_stack=new QStackedWidget(this);m_scroll=new QScrollArea(this);m_scroll->setAlignment(Qt::AlignCenter);
    m_canvas=new TranslationCanvas;m_canvas->setObjectName("translationCanvas");m_scroll->setWidget(m_canvas);m_stack->addWidget(m_scroll);
    connect(m_view,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {m_stack->setCurrentIndex(index==2?1:0);m_canvas->setOriginal(index==1);});
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
    m_stack->addWidget(splitter);layout->addWidget(m_stack,1);
    auto *bottom = new QHBoxLayout;
    auto *zoom=new QComboBox(this);zoom->setObjectName("translationZoom");
    for(int value:{50,75,100,125,150,200})zoom->addItem(QString::number(value)+"%",value);
    zoom->setCurrentIndex(2);connect(zoom,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,zoom](int) {m_canvas->setZoom(zoom->currentData().toDouble()/100.);});bottom->addWidget(zoom);
    auto *copyText=new QPushButton(QStringLiteral("复制译文"),this);copyText->setObjectName("translationCopyText");
    connect(copyText,&QPushButton::clicked,this,[this] {const auto text=m_target->toPlainText();QApplication::clipboard()->setText(text);
        m_status->setText(QApplication::clipboard()->text()==text?QStringLiteral("译文已复制。"):QStringLiteral("剪贴板被占用，复制未完成。"));});bottom->addWidget(copyText);
    auto *save=new QPushButton(QStringLiteral("保存译图"),this);save->setObjectName("translationSaveImage");
    connect(save,&QPushButton::clicked,this,&TranslationWindow::saveTranslatedImage);bottom->addWidget(save);bottom->addStretch();
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
        presentResults(results);
        setStatus("TRANSLATING",QStringLiteral("正在翻译… %1 / %2 块；截图未上传").arg(done).arg(total));
    });
    connect(m_client,&Translation::Client::finished,this,[this](const QString &id,const QList<Translation::Result> &results) {
        if (id != m_taskId) return;
        presentResults(results);
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
    cancel(); m_taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_image = image; m_blocks.clear(); m_source->clear(); m_target->clear(); m_warning->clear();
    m_canvas->setContent(image,{},{});m_view->setCurrentIndex(0);
    if(!region.isEmpty()) {
        auto *targetScreen=QApplication::screenAt(region.center());
        if(!targetScreen)targetScreen=screen();
        const auto available=targetScreen->availableGeometry();
        const int wantedWidth=qMin(available.width(),qMax(900,region.width()+30));
        const int wantedHeight=qMin(available.height(),qMax(560,region.height()+200));
        resize(wantedWidth,wantedHeight);
        move(qBound(available.left(),region.left(),qMax(available.left(),available.right()-width()+1)),qBound(available.top(),region.top(),qMax(available.top(),available.bottom()-height()+1)));
    }
    QTimer::singleShot(0,this,[this] {if(!m_image.isNull())m_canvas->setZoom(qMin(1.,double(m_scroll->viewport()->width()-20)/m_image.width()));});
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
    presentResults({});
    setStatus("PRESENTING",QStringLiteral("本地识别完成（%1）").arg(result.language));
    translate();
}
void TranslationWindow::translate()
{
    if (busy() || m_blocks.isEmpty()) return;
    const auto config = Translation::Config::load();
    if (config.mode == "local") { setStatus("PRESENTING",QStringLiteral("纯本地 OCR · 未联网。需要译文时打开“翻译设置”选择服务并授权。")); return; }
    const QString key = config.mode == "cloud" ? config.apiKey() : QString();
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
    emit settingsRequested();
}
void TranslationWindow::presentResults(const QList<Translation::Result> &results)
{
    m_target->setPlainText(Translation::resultText(m_blocks,results));
    m_canvas->setContent(m_image,m_blocks,results);
}
void TranslationWindow::saveTranslatedImage()
{
    if(m_image.isNull()){m_status->setText(QStringLiteral("请先截图。"));return;}
    const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("保存译图"),"TaskPin-translation.png","PNG (*.png)");
    if(path.isEmpty())return;
    const bool original=m_canvas->original();m_canvas->setOriginal(false);const QImage output=m_canvas->renderedImage();m_canvas->setOriginal(original);
    if(!output.save(path,"PNG")){m_status->setText(QStringLiteral("译图保存失败，请检查目录权限。"));return;}
    QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    m_status->setText(QApplication::clipboard()->text()==QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())?QStringLiteral("译图已保存，文件路径已复制。"):QStringLiteral("译图已保存；剪贴板被占用，路径未复制。"));
}
void TranslationWindow::closeEvent(QCloseEvent *event) { cancel(); QWidget::closeEvent(event); }
