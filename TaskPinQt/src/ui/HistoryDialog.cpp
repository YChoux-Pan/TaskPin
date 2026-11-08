#include "ui/HistoryDialog.h"

#include "core/History.h"
#include "core/Settings.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

HistoryDialog::HistoryDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("TaskPin 截屏历史"));
    resize(760, 520);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);

    auto *layout = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(150, 100));
    m_list->setGridSize(QSize(170, 140));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(6);
    layout->addWidget(m_list);

    auto *bar = new QHBoxLayout();
    auto *pin = new QPushButton(QStringLiteral("贴到桌面"), this);
    auto *copy = new QPushButton(QStringLiteral("复制"), this);
    auto *print = new QPushButton(QStringLiteral("打印"), this);
    auto *open = new QPushButton(QStringLiteral("打开文件夹"), this);
    auto *del = new QPushButton(QStringLiteral("删除"), this);
    auto *close = new QPushButton(QStringLiteral("关闭"), this);
    bar->addWidget(pin);
    bar->addWidget(copy);
    bar->addWidget(print);
    bar->addWidget(open);
    bar->addStretch(1);
    bar->addWidget(del);
    bar->addWidget(close);
    layout->addLayout(bar);

    connect(pin, &QPushButton::clicked, this, &HistoryDialog::pinSelected);
    connect(copy, &QPushButton::clicked, this, &HistoryDialog::copySelected);
    connect(print, &QPushButton::clicked, this, &HistoryDialog::printSelected);
    connect(open, &QPushButton::clicked, this, &HistoryDialog::openFolder);
    connect(del, &QPushButton::clicked, this, &HistoryDialog::deleteSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    reload();
}

void HistoryDialog::reload()
{
    m_list->clear();
    m_files = History::recent();
    for (const QString &path : m_files) {
        QPixmap pm(path);
        if (pm.isNull()) continue;
        const QPixmap thumb = pm.scaled(150, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QString stamp = QFileInfo(path).lastModified().toString(QStringLiteral("MM-dd HH:mm:ss"));
        auto *item = new QListWidgetItem(QIcon(thumb), stamp);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        m_list->addItem(item);
    }
    setWindowTitle(QStringLiteral("TaskPin 截屏历史 — %1 张").arg(m_list->count()));
    if (m_list->count() > 0) m_list->setCurrentRow(0);
}

QImage HistoryDialog::selectedImage() const
{
    auto *item = m_list->currentItem();
    if (!item) return {};
    return QImage(item->data(Qt::UserRole).toString());
}

void HistoryDialog::copySelected()
{
    const QImage img = selectedImage();
    if (img.isNull()) return;
    QApplication::clipboard()->setImage(img);
}

void HistoryDialog::pinSelected()
{
    const QImage img = selectedImage();
    if (!img.isNull()) emit pinRequested(img);
}

void HistoryDialog::printSelected()
{
    const QImage img = selectedImage();
    if (!img.isNull()) emit printRequested(img);
}

void HistoryDialog::deleteSelected()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, QStringLiteral("TaskPin"), QStringLiteral("从历史中删除这张截图？"),
                              QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes)
        return;
    if (!QFile::remove(path)) qWarning() << "history delete failed:" << path;
    reload();
}

void HistoryDialog::openFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(History::dir()));
}
