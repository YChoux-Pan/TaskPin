#pragma once
#include "translation/TranslationConfig.h"
#include "translation/OcrProfiles.h"
#include <QWidget>
#include <QPointer>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
class QComboBox; class QCheckBox; class QSpinBox; class QLabel; class QPushButton; class QLineEdit;
class QProcess; class QTimer;
namespace Translation { class Client; }
class TranslationSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit TranslationSettingsPage(QWidget *parent=nullptr);
    ~TranslationSettingsPage() override;
    void setConnection(const QString &endpoint,const QString &model,const QString &key);
    bool save();
    void cancelTest();
signals:
    void editAiRequested();
    void saveReady();
    void validationBusyChanged(bool busy);
private:
    Translation::Config configuration() const;
    Translation::OcrProfiles ocrConfiguration() const;
    QByteArray ocrDraftKey(const Translation::OcrProfiles &ocr) const;
    void beginOcrValidation(const Translation::OcrProfiles &ocr,const QByteArray &draftKey);
    void cancelOcrValidation(bool notify);
    void failOcrValidation(const QString &message);
    void invalidateOcrValidation();
    void refreshOcrCatalog();
    void finishOcrCatalog(const QJsonArray &models,const QString &error=QString());
    void cancelOcrCatalog();
    void testConnection();
    QComboBox *m_mode;
    QComboBox *m_ocrProfile;
    QLineEdit *m_ocrPath;
    QLabel *m_ocrStatus;
    QCheckBox *m_consent;
    QSpinBox *m_timeout;
    QLabel *m_connection,*m_status;
    QPushButton *m_test,*m_cancel;
    Translation::Client *m_client;
    QString m_endpoint,m_model,m_key,m_authorizedEndpoint;
    bool m_initialized=false;
    QPointer<QProcess> m_ocrValidation;
    QTimer *m_ocrValidationTimeout;
    QByteArray m_validationOutput,m_validationDraft,m_validatedDraft;
    quint64 m_validationGeneration=0;
    QPointer<QProcess> m_ocrCatalog;
    QTimer *m_catalogTimeout;
    QPushButton *m_ocrRefresh;
    QByteArray m_catalogOutput;
    QMap<QString,QJsonObject> m_catalogEntries;
    quint64 m_catalogGeneration=0;
};
