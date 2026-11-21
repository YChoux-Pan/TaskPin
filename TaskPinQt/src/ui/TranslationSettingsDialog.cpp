#include "ui/TranslationSettingsDialog.h"
#include "translation/TranslationConfig.h"
#include "core/Settings.h"
#include "platform/TranslationCredentials.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TranslationSettingsDialog::TranslationSettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("截图翻译设置")); resize(600,500);
    const auto config = Translation::Config::load();
    auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout;
    auto *intro = new QLabel(QStringLiteral("截图翻译独立于便签 AI。截图仅本机 OCR；云端只接收识别文字。密钥保存在 Windows 凭据中。"),this);
    intro->setWordWrap(true); layout->addWidget(intro);
    m_mode = new QComboBox(this); m_mode->setObjectName("translationMode");
    m_mode->addItem(QStringLiteral("纯本地 OCR · 不联网"),"local");
    m_mode->addItem(QStringLiteral("离线 Mock · 流程测试，非真实译文"),"mock");
    m_mode->addItem(QStringLiteral("DeepSeek / OpenAI 兼容 · 云端文本翻译"),"cloud");
    m_mode->setCurrentIndex(qMax(0,m_mode->findData(config.mode))); form->addRow(QStringLiteral("模式"),m_mode);
    m_endpoint = new QLineEdit(config.endpoint,this); m_endpoint->setObjectName("translationEndpoint");
    m_model = new QLineEdit(config.model,this); m_model->setObjectName("translationModel");
    m_key = new QLineEdit(this); m_key->setEchoMode(QLineEdit::Password); m_key->setObjectName("translationKey");
    m_key->setPlaceholderText(QStringLiteral("留空保留此接口已存的密钥"));
    m_timeout = new QSpinBox(this); m_timeout->setRange(5,120); m_timeout->setValue(config.timeoutMs/1000); m_timeout->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("HTTPS 接口地址"),m_endpoint); form->addRow(QStringLiteral("模型"),m_model);
    form->addRow(QStringLiteral("API Key"),m_key); form->addRow(QStringLiteral("单次网络超时"),m_timeout);
    layout->addLayout(form);
    auto *import = new QPushButton(QStringLiteral("从现有 TaskPin AI 配置填入（保存后使用）"),this);
    connect(import,&QPushButton::clicked,this,[this] {
        const auto &settings = Settings::instance();
        m_endpoint->setText(settings.aiBaseUrl); m_model->setText(settings.aiModel); m_key->setText(settings.aiApiKey);
        m_consent->setChecked(false); m_forgetKey = false;
    }); layout->addWidget(import);
    auto *forget = new QPushButton(QStringLiteral("删除此接口已存密钥（保存后生效）"),this);
    connect(forget,&QPushButton::clicked,this,[this] { m_forgetKey = true; m_key->clear(); m_key->setPlaceholderText(QStringLiteral("保存后删除此接口密钥")); });
    layout->addWidget(forget);
    m_destination = new QLabel(this); m_destination->setTextFormat(Qt::PlainText); m_destination->setWordWrap(true); layout->addWidget(m_destination);
    m_consent = new QCheckBox(QStringLiteral("允许主动截图后，将 OCR 文字发送到上述接口用于翻译；不发送图片。"),this);
    m_consent->setObjectName("translationConsent"); m_consent->setChecked(config.authorized()); layout->addWidget(m_consent);
    auto *warning = new QLabel(QStringLiteral("取消可停止后续处理，但不能撤回已发送的文字或已产生的服务费用。修改接口后需要重新授权。"),this);
    warning->setWordWrap(true); layout->addWidget(warning);
    m_error = new QLabel(this); m_error->setWordWrap(true); m_error->setTextFormat(Qt::PlainText); m_error->setStyleSheet("color:#d45959"); layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);
    connect(buttons,&QDialogButtonBox::accepted,this,&TranslationSettingsDialog::save);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject); layout->addWidget(buttons);
    connect(m_endpoint,&QLineEdit::textChanged,this,&TranslationSettingsDialog::endpointChanged);
    Translation::Config copy = config;
    m_destination->setText(QStringLiteral("文字发送目标：%1").arg(copy.requestUrl().toDisplayString()));
}
void TranslationSettingsDialog::endpointChanged()
{
    m_consent->setChecked(false); m_forgetKey = false;
    Translation::Config config; config.endpoint = m_endpoint->text();
    m_destination->setText(QStringLiteral("文字发送目标：%1").arg(config.requestUrl().toDisplayString()));
}
void TranslationSettingsDialog::save()
{
    Translation::Config config; config.mode = m_mode->currentData().toString();
    config.endpoint = m_endpoint->text().trimmed(); config.model = m_model->text().trimmed(); config.timeoutMs = m_timeout->value()*1000;
    config.consentEndpoint = m_consent->isChecked() ? config.requestUrl().toString(QUrl::FullyEncoded) : QString();
    if (config.mode == "cloud" && (!config.authorized() || config.model.isEmpty())) {
        m_error->setText(QStringLiteral("请填写有效的 HTTPS 服务和模型，并确认文字外发授权。")); return;
    }
    QString error;
    if (m_forgetKey && !TranslationCredentials::remove(config.credentialTarget(),&error)) { m_error->setText(error); return; }
    if (!m_key->text().trimmed().isEmpty() && !TranslationCredentials::write(config.credentialTarget(),m_key->text().trimmed(),&error)) {
        m_error->setText(error); return;
    }
    if (config.mode == "cloud" && TranslationCredentials::read(config.credentialTarget(),&error).isEmpty()) {
        m_error->setText(error.isEmpty()?QStringLiteral("请填入密钥。纯本地 OCR 无需密钥。"):error); return;
    }
    if (!config.save(&error)) { m_error->setText(error); return; }
    m_key->clear(); accept();
}
