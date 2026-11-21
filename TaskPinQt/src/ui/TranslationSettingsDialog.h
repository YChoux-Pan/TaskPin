#pragma once
#include <QDialog>
class QComboBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QSpinBox;
class TranslationSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit TranslationSettingsDialog(QWidget *parent = nullptr);
private:
    void save();
    void endpointChanged();
    QComboBox *m_mode;
    QLineEdit *m_endpoint, *m_model, *m_key;
    QCheckBox *m_consent;
    QLabel *m_destination, *m_error;
    QSpinBox *m_timeout;
    bool m_forgetKey = false;
};
