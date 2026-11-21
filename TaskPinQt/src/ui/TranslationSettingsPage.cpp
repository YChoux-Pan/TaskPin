#include "ui/TranslationSettingsPage.h"
#include "translation/TranslationClient.h"
#include "translation/TranslationText.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TranslationSettingsPage::TranslationSettingsPage(QWidget *parent):QWidget(parent)
{
    setObjectName("translationSettingsPage");
    const auto config=Translation::Config::load();m_authorizedEndpoint=config.consentEndpoint;
    auto *box=new QVBoxLayout(this);
    auto *intro=new QLabel(QStringLiteral("与 AI 页共用接口、模型和 API Key，无需重复填写。便签 AI 归纳开关与截图翻译分别控制。"),this);
    intro->setWordWrap(true);box->addWidget(intro);
    auto *form=new QFormLayout;
    m_mode=new QComboBox(this);m_mode->setObjectName("translationMode");
    m_mode->addItem(QStringLiteral("云端翻译 · 使用 AI 页连接"),"cloud");
    m_mode->addItem(QStringLiteral("仅本地 OCR · 不翻译"),"local");
    m_mode->addItem(QStringLiteral("离线 Mock · 仅供流程测试"),"mock");
    m_mode->setCurrentIndex(qMax(0,m_mode->findData(config.mode)));form->addRow(QStringLiteral("模式"),m_mode);
    m_timeout=new QSpinBox(this);m_timeout->setRange(5,120);m_timeout->setSuffix(QStringLiteral(" 秒"));m_timeout->setValue(config.timeoutMs/1000);
    form->addRow(QStringLiteral("请求超时"),m_timeout);box->addLayout(form);
    m_connection=new QLabel(this);m_connection->setObjectName("translationSharedConnection");m_connection->setWordWrap(true);m_connection->setTextFormat(Qt::PlainText);box->addWidget(m_connection);
    auto *edit=new QPushButton(QStringLiteral("前往 AI 页编辑连接 / 密钥"),this);edit->setObjectName("translationEditAi");
    connect(edit,&QPushButton::clicked,this,&TranslationSettingsPage::editAiRequested);box->addWidget(edit);
    m_consent=new QCheckBox(QStringLiteral("允许主动截图后，将识别文字发送到上述接口翻译（不发送图片）"),this);
    m_consent->setObjectName("translationConsent");box->addWidget(m_consent);
    auto *hint=new QLabel(QStringLiteral("结果默认在原图文字位置显示，可切换原图或对照。复杂背景使用译文底板；代码与公式保留。取消设置不会保存更改，取消请求不能撤回已发送文字。"),this);
    hint->setWordWrap(true);box->addWidget(hint);
    auto *row=new QHBoxLayout;
    m_test=new QPushButton(QStringLiteral("测试连接 · 仅发送测试短句"),this);m_test->setObjectName("translationTestConnection");
    m_cancel=new QPushButton(QStringLiteral("取消测试"),this);m_cancel->setEnabled(false);
    row->addWidget(m_test);row->addWidget(m_cancel);box->addLayout(row);
    m_status=new QLabel(this);m_status->setObjectName("translationConnectionStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);box->addWidget(m_status);box->addStretch();
    m_client=new Translation::Client(this);
    connect(m_test,&QPushButton::clicked,this,&TranslationSettingsPage::testConnection);
    connect(m_cancel,&QPushButton::clicked,this,[this] {cancelTest();m_status->setText(QStringLiteral("已取消连接测试。"));});
    connect(m_client,&Translation::Client::finished,this,[this](const QString &,const QList<Translation::Result> &results) {
        m_test->setEnabled(true);m_cancel->setEnabled(false);
        bool ok=results.size()==1 && results.first().status=="ok";
        m_status->setText(ok?QStringLiteral("连接成功 · 已收到真实译文：%1").arg(results.first().text):QStringLiteral("连接成功，但测试译文未通过完整性校验，请检查模型。"));
    });
    connect(m_client,&Translation::Client::failed,this,[this](const QString &,const QString &error) {m_test->setEnabled(true);m_cancel->setEnabled(false);m_status->setText(error);});
}
void TranslationSettingsPage::setConnection(const QString &endpoint,const QString &model,const QString &key)
{
    const bool changed=m_endpoint!=endpoint.trimmed() || m_model!=model.trimmed() || m_key!=key.trimmed();
    if(changed && m_client->busy())cancelTest();
    Translation::Config before;before.endpoint=m_endpoint;
    m_endpoint=endpoint.trimmed();m_model=model.trimmed();m_key=key.trimmed();
    Translation::Config cfg;cfg.endpoint=m_endpoint;
    if(!m_initialized)m_consent->setChecked(m_authorizedEndpoint==cfg.requestUrl().toString(QUrl::FullyEncoded));
    else if(before.requestUrl()!=cfg.requestUrl())m_consent->setChecked(false);
    m_initialized=true;
    m_connection->setText(QStringLiteral("共享接口：%1\n模型：%2\nAPI Key：%3").arg(cfg.requestUrl().toDisplayString(),m_model,m_key.isEmpty()?QStringLiteral("尚未配置，请到 AI 页填写"):QStringLiteral("已配置 · 与 AI 页共用")));
    if(changed)m_status->clear();
}
Translation::Config TranslationSettingsPage::configuration() const
{
    Translation::Config cfg;cfg.mode=m_mode->currentData().toString();cfg.endpoint=m_endpoint;cfg.model=m_model;
    cfg.timeoutMs=m_timeout->value()*1000;
    cfg.consentEndpoint=m_consent->isChecked()?cfg.requestUrl().toString(QUrl::FullyEncoded):QString();return cfg;
}
bool TranslationSettingsPage::save()
{
    const auto cfg=configuration();
    if(cfg.mode=="cloud" && (!cfg.authorized() || m_model.isEmpty() || m_key.isEmpty())) {
        m_status->setText(QStringLiteral("请在 AI 页配置有效的 HTTPS 接口、模型和密钥，并勾选文字翻译授权。"));return false;
    }
    QString error;if(!cfg.save(&error)){m_status->setText(error);return false;}
    cancelTest();return true;
}
void TranslationSettingsPage::cancelTest(){m_client->cancel();m_test->setEnabled(true);m_cancel->setEnabled(false);}
void TranslationSettingsPage::testConnection()
{
    auto cfg=configuration();cfg.mode="cloud";
    cfg.useCache=false;
    // This button authorizes only the fixed synthetic request. It does not save screenshot consent.
    cfg.consentEndpoint=cfg.requestUrl().toString(QUrl::FullyEncoded);
    if(!cfg.authorized() || m_model.isEmpty() || m_key.isEmpty()){m_status->setText(QStringLiteral("请先在 AI 页填写有效的 HTTPS 接口、模型和密钥。"));return;}
    Translation::Block block;block.id="connection-test";block.kind="paragraph";block.source="The default value is 10.";
    block=Translation::protect(block,"connection");
    m_test->setEnabled(false);m_cancel->setEnabled(true);m_status->setText(QStringLiteral("正在连接并翻译固定测试短句…"));
    m_client->start("settings-test",{block},cfg,m_key);
}
