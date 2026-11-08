#pragma once

#include <QDialog>

class QLineEdit;

/// Frameless one-line capture box, the Qt counterpart of the C# QuickInputForm.
class QuickAddDialog : public QDialog {
    Q_OBJECT
public:
    explicit QuickAddDialog(QWidget *parent = nullptr);
    QString text() const;

protected:
    void showEvent(QShowEvent *e) override;

private:
    QLineEdit *m_edit;
};
