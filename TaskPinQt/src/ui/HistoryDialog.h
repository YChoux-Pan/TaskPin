#pragma once

#include <QDialog>
#include <QImage>
#include <QStringList>

class QListWidget;

/// Thumbnail browser over the snips kept in history/snip (H3).
class HistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryDialog(QWidget *parent = nullptr);

signals:
    void pinRequested(const QImage &image);
    void printRequested(const QImage &image);

private:
    void reload();
    void updateButtons();
    QImage selectedImage() const;
    void copySelected();
    void deleteSelected();
    void pinSelected();
    void printSelected();
    void openFolder();

    QListWidget *m_list = nullptr;
    QStringList m_files;
};
