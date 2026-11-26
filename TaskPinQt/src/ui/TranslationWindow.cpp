#include "ui/TranslationWindow.h"
#include "ui/AppIcon.h"
#include "ui/TranslationCanvas.h"
#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

TranslationWindow::TranslationWindow(QWidget *parent):PinWindow(QImage(),QString(),parent)
{
    setWindowTitle(QStringLiteral("TaskPin 翻译贴图"));setObjectName("translationPin");setWindowIcon(taskpinIcon());
    m_renderer=new TranslationCanvas(this);m_renderer->hide();
    m_ocr=new Translation::Ocr(this);m_client=new Translation::Client(this);
    connect(m_ocr,&Translation::Ocr::finished,this,&TranslationWindow::presentOcr);
    connect(m_ocr,&Translation::Ocr::failed,this,[this](const QString &id,const QString &error){if(id==m_taskId)setStatus("FAILED",error);});
    connect(m_client,&Translation::Client::updated,this,[this](const QString &id,const QList<Translation::Result> &results,int done,int total){
        if(id!=m_taskId)return;presentResults(results);setStatus("TRANSLATING",QStringLiteral("翻译中… %1/%2 · 右键可取消").arg(done).arg(total));
    });
    connect(m_client,&Translation::Client::finished,this,[this](const QString &id,const QList<Translation::Result> &results){
        if(id!=m_taskId)return;presentResults(results);bool failed=false,mock=false;
        for(const auto &r:results){failed|=r.status=="failed";mock|=r.status=="mock";}
        setStatus("PRESENTING",mock?QStringLiteral("Mock 测试 · 非真实译文 · 右键查看对照"):failed?QStringLiteral("部分未译，保留原图 · 右键查看对照"):QStringLiteral("翻译完成 · 拖动移动，滚轮缩放，右键更多"));
    });
    connect(m_client,&Translation::Client::failed,this,[this](const QString &id,const QString &error){if(id==m_taskId)setStatus("FAILED",error);});
}
TranslationWindow::~TranslationWindow(){m_ocr->cancel();m_client->cancel();}
bool TranslationWindow::busy() const{return m_ocr->busy()||m_client->busy();}
void TranslationWindow::setStatus(const QString &state,const QString &label)
{
    m_state=state;m_message=label;m_statusVisible=true;const auto revision=++m_statusRevision;
    setToolTip((m_warning.isEmpty()?QString():m_warning+"\n")+label);update();
    if(state=="PRESENTING")QTimer::singleShot(3000,this,[this,revision]{if(revision==m_statusRevision){m_statusVisible=false;update();}});
}
void TranslationWindow::setCapture(const QImage &image,const QRect &region)
{
    cancel();m_closing=false;m_taskId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_originalImage=image;m_originalImage.setDevicePixelRatio(1);m_translatedImage=m_originalImage;
    m_sourceLogicalSize=region.isValid()?region.size():image.size();m_original=false;
    m_blocks.clear();m_sourceText.clear();m_targetText.clear();m_warning.clear();
    replaceFrame(m_originalImage,m_sourceLogicalSize,true);
    if(region.isValid())move(pos()+region.topLeft()-contentGeometry().topLeft());
    if(m_source)m_source->clear();if(m_target)m_target->clear();
    emit processingRequested(this);
    show();raise();m_ocr->recognize(m_taskId,m_originalImage);
    if(m_ocr->busy())setStatus("OCR_RUNNING",QStringLiteral("识别并翻译中… · 可拖动，右键可取消"));
}
void TranslationWindow::presentOcr(const QString &id,const Translation::OcrResult &result)
{
    if(id!=m_taskId||m_state=="CANCELLED"||m_closing)return;
    QStringList warnings;m_blocks=Translation::buildBlocks(result,&warnings);
    warnings.append(QStringLiteral("%1 本地识别；技术符号请切换原图核对。").arg(result.engine.isEmpty()?QStringLiteral("OCR"):result.engine));
    m_warning=warnings.join('\n');
    if(m_blocks.isEmpty()){setStatus("PRESENTING",QStringLiteral("未识别到文字 · 右键重新截图"));return;}
    for(auto &block:m_blocks)block=Translation::protect(block,m_taskId.left(8));
    m_sourceText=Translation::sourceText(m_blocks);presentResults({});translate();
}
void TranslationWindow::translate()
{
    if(busy()||m_closing)return;
    if(m_blocks.isEmpty()){
        if(m_originalImage.isNull())return;
        m_taskId=QUuid::createUuid().toString(QUuid::WithoutBraces);
        emit processingRequested(this);m_ocr->recognize(m_taskId,m_originalImage);
        if(m_ocr->busy())setStatus("OCR_RUNNING",QStringLiteral("重新识别并翻译中… · 右键可取消"));
        return;
    }
    const auto config=Translation::Config::load();
    if(config.mode=="local"){setStatus("PRESENTING",QStringLiteral("仅本地OCR · 右键查看文字或打开翻译设置"));return;}
    emit processingRequested(this);
    m_client->start(m_taskId,m_blocks,config,config.mode=="cloud"?config.apiKey():QString());
    if(m_client->busy())setStatus("TRANSLATING",config.mode=="mock"?QStringLiteral("Mock流程测试中…"):QStringLiteral("翻译中… · 仅发送已授权文字"));
}
void TranslationWindow::cancel()
{
    m_taskId=QUuid::createUuid().toString(QUuid::WithoutBraces);m_ocr->cancel();m_client->cancel();
    setStatus("CANCELLED",QStringLiteral("已取消处理 · 右键可重试；已发送文字无法撤回"));
}
void TranslationWindow::presentResults(const QList<Translation::Result> &results)
{
    m_targetText=Translation::resultText(m_blocks,results);m_renderer->setContent(m_originalImage,m_blocks,results);
    m_renderer->setOriginal(false);m_translatedImage=m_renderer->renderedImage();refreshFrame();
    if(m_source)m_source->setPlainText(m_sourceText);if(m_target)m_target->setPlainText(m_targetText);
}
void TranslationWindow::refreshFrame()
{
    const QImage frame=m_original?m_originalImage:m_translatedImage;if(frame.isNull()||m_originalImage.isNull())return;
    const QSize logical(qMax(1,qRound(double(frame.width())*m_sourceLogicalSize.width()/m_originalImage.width())),
                        qMax(1,qRound(double(frame.height())*m_sourceLogicalSize.height()/m_originalImage.height())));
    replaceFrame(frame,logical);
}
void TranslationWindow::toggleOriginal(){m_original=!m_original;refreshFrame();}
void TranslationWindow::paintEvent(QPaintEvent *event)
{
    PinWindow::paintEvent(event);if(!m_statusVisible||width()<50||height()<25)return;
    QPainter p(this);const QRect box(5,qMax(4,height()-48),qMax(1,width()-10),qMin(42,height()-8));
    QFont font=p.font();font.setPixelSize(12);p.setFont(font);p.setPen(Qt::red);
    p.drawText(box.adjusted(5,2,-5,-2),Qt::AlignVCenter|Qt::TextWordWrap,m_message);
}
void TranslationWindow::extendMenu(QMenu *menu)
{
    auto *status=menu->addAction(m_message.isEmpty()?QStringLiteral("翻译贴图"):m_message);status->setEnabled(false);
    menu->addAction(m_original?QStringLiteral("显示中文译图"):QStringLiteral("查看原图"),this,&TranslationWindow::toggleOriginal);
    menu->addAction(QStringLiteral("查看文字对照…"),this,&TranslationWindow::showComparison);
    auto *copy=menu->addAction(QStringLiteral("复制译文"),this,[this]{QApplication::clipboard()->setText(m_targetText);});copy->setEnabled(!m_targetText.isEmpty());
    auto *retry=menu->addAction(QStringLiteral("重新翻译"),this,&TranslationWindow::translate);retry->setEnabled(!busy()&&!m_originalImage.isNull());
    if(busy())menu->addAction(QStringLiteral("取消翻译"),this,&TranslationWindow::cancel);
    menu->addAction(QStringLiteral("翻译设置…"),this,[this]{if(busy())cancel();emit settingsRequested();});
    menu->addAction(QStringLiteral("重新截图…"),this,[this]{cancel();emit recaptureRequested();});menu->addSeparator();
}
void TranslationWindow::showComparison()
{
    if(m_comparison){m_comparison->show();m_comparison->raise();return;}
    auto *dialog=new QDialog(this,Qt::Dialog|Qt::WindowStaysOnTopHint);
    dialog->setObjectName("translationComparison");dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("TaskPin 翻译文字对照"));dialog->setWindowIcon(taskpinIcon());
    dialog->setAttribute(Qt::WA_TranslucentBackground,false);dialog->setAttribute(Qt::WA_NoSystemBackground,false);
    dialog->setWindowOpacity(1.0);dialog->setAutoFillBackground(true);
    // Explicit brushes prevent a translucent pin's palette from resolving into this normal window.
    QPalette surface=QApplication::palette();
    for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}){
        for(int role=0;role<QPalette::NColorRoles;++role){
            const auto colorRole=static_cast<QPalette::ColorRole>(role);
            auto brush=surface.brush(group,colorRole);
            if(colorRole==QPalette::Window||colorRole==QPalette::Base||colorRole==QPalette::AlternateBase||colorRole==QPalette::Button){
                auto color=brush.color();color.setAlpha(255);brush.setColor(color);
            }
            surface.setBrush(group,colorRole,brush);
        }
    }
    dialog->setPalette(surface);m_comparison=dialog;
    auto *layout=new QVBoxLayout(dialog);layout->setContentsMargins(12,12,12,12);layout->setSpacing(10);
    auto *header=new QWidget(dialog);header->setObjectName("translationComparisonHeader");
    header->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    auto *headingLayout=new QHBoxLayout(header);headingLayout->setContentsMargins(0,0,0,0);headingLayout->setSpacing(14);
    auto *heading=new QLabel(QStringLiteral("文字对照"),header);auto headingFont=heading->font();headingFont.setBold(true);heading->setFont(headingFont);
    headingLayout->addWidget(heading,0,Qt::AlignTop);
    auto *warning=new QLabel(m_warning.isEmpty()?QStringLiteral("可选中文字，或使用复制按钮。"):m_warning,header);
    warning->setObjectName("translationComparisonStatus");warning->setTextFormat(Qt::PlainText);warning->setWordWrap(true);
    warning->setToolTip(m_warning);warning->setMaximumHeight(warning->fontMetrics().lineSpacing()*2+2);
    warning->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);headingLayout->addWidget(warning,1);
    layout->addWidget(header,0);
    auto *splitter=new QSplitter(Qt::Horizontal,dialog);splitter->setObjectName("translationComparisonPanes");
    splitter->setChildrenCollapsible(false);layout->addWidget(splitter,1);
    auto pane=[this,splitter](const QString &title,const QString &name,const QString &text,QPlainTextEdit **editor){
        auto *panel=new QWidget(splitter);auto *box=new QVBoxLayout(panel);box->setContentsMargins(0,0,0,0);box->setSpacing(6);
        auto *row=new QHBoxLayout;row->addWidget(new QLabel(title,panel));row->addStretch();
        auto *copy=new QPushButton(QStringLiteral("复制"),panel);copy->setObjectName(name+"Copy");
        copy->setMinimumWidth(72);copy->setMaximumWidth(120);row->addWidget(copy);box->addLayout(row);
        *editor=new QPlainTextEdit(text,panel);(*editor)->setReadOnly(true);(*editor)->setObjectName(name);
        (*editor)->setMinimumSize(120,100);box->addWidget(*editor,1);
        connect(copy,&QPushButton::clicked,this,[this,editor]{const auto text=(*editor)->toPlainText();QApplication::clipboard()->setText(text);if(QApplication::clipboard()->text()!=text)setStatus(m_state,QStringLiteral("剪贴板被占用，复制未完成。"));});
    };
    pane(QStringLiteral("OCR 原文"),"translationSource",m_sourceText,&m_source);pane(QStringLiteral("中文译文"),"translationTarget",m_targetText,&m_target);
    splitter->setStretchFactor(0,1);splitter->setStretchFactor(1,1);
    QScreen *screen=QGuiApplication::screenAt(contentGeometry().center());if(!screen)screen=this->screen();
    const QRect available=screen?screen->availableGeometry():QRect(0,0,800,600);
    const QSize bounds=available.size()-QSize(qMin(24,available.width()/10),qMin(24,available.height()/10));
    dialog->setMinimumSize(QSize(560,360).boundedTo(bounds));
    dialog->resize(QSize(qMin(900,qRound(available.width()*.72)),qMin(620,qRound(available.height()*.72))).expandedTo(dialog->minimumSize()).boundedTo(bounds));
    dialog->move(available.center()-QPoint(dialog->width()/2,dialog->height()/2));
    connect(dialog,&QObject::destroyed,this,[this]{m_source=nullptr;m_target=nullptr;});dialog->show();
    layout->activate();
    const int paneWidth=qMax(1,splitter->width()-splitter->handleWidth());
    splitter->setSizes({paneWidth/2,paneWidth-paneWidth/2});
}
void TranslationWindow::hideEvent(QHideEvent *event){if(m_comparison)m_comparison->hide();PinWindow::hideEvent(event);}
void TranslationWindow::closeEvent(QCloseEvent *event){m_closing=true;cancel();if(m_comparison)m_comparison->close();PinWindow::closeEvent(event);}
