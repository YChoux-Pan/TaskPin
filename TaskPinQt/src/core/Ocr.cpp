#include "core/Ocr.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include <QUuid>
Ocr::Ocr(QObject *parent):QObject(parent),m_engine(new Translation::Ocr(this))
{
    connect(m_engine,&Translation::Ocr::finished,this,[this](const QString &id,const Translation::OcrResult &result){
        if(id!=m_taskId)return;m_taskId.clear();QStringList lines;
        for(const auto &line:Translation::readingLines(result.lines))lines.append(line.text);
        emit finished(lines.join('\n'));
    });
    connect(m_engine,&Translation::Ocr::failed,this,[this](const QString &id,const QString &error){
        if(id!=m_taskId)return;m_taskId.clear();emit failed(error);
    });
}
Ocr::~Ocr(){m_engine->cancel();}
bool Ocr::busy() const{return m_engine->busy();}
bool Ocr::available(){return Translation::Ocr::available();}
void Ocr::recognize(const QImage &image)
{
    if(busy()){emit failed(QStringLiteral("上一次识别还没结束"));return;}
    m_taskId=QUuid::createUuid().toString(QUuid::WithoutBraces);m_engine->recognize(m_taskId,image);
}
