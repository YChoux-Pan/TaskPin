#pragma once
#include "translation/TranslationConfig.h"
#include <QWidget>
class QComboBox; class QCheckBox; class QSpinBox; class QLabel; class QPushButton;
namespace Translation { class Client; }
class TranslationSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit TranslationSettingsPage(QWidget *parent=nullptr);
    void setConnection(const QString &endpoint,const QString &model,const QString &key);
    bool save();
    void cancelTest();
signals:
    void editAiRequested();
private:
    Translation::Config configuration() const;
    void testConnection();
    QComboBox *m_mode;
    QCheckBox *m_consent;
    QSpinBox *m_timeout;
    QLabel *m_connection,*m_status;
    QPushButton *m_test,*m_cancel;
    Translation::Client *m_client;
    QString m_endpoint,m_model,m_key,m_authorizedEndpoint;
    bool m_initialized=false;
};
