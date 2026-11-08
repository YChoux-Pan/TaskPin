#include "ui/QuickAddDialog.h"

#include <QGuiApplication>
#include <QLineEdit>
#include <QScreen>
#include <QVBoxLayout>

QuickAddDialog::QuickAddDialog(QWidget *parent) : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedWidth(520);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);

    auto *box = new QWidget(this);
    box->setObjectName("box");
    box->setStyleSheet(
        "#box { background: rgba(20,25,32,240); border: 1px solid rgba(255,255,255,40); border-radius: 10px; }");
    auto *inner = new QVBoxLayout(box);
    inner->setContentsMargins(14, 12, 14, 12);

    m_edit = new QLineEdit(box);
    m_edit->setPlaceholderText(QStringLiteral("写点什么…  例如：明天15:00前把报价单发给王总"));
    m_edit->setStyleSheet(
        "QLineEdit { background: transparent; border: none; color: #E8ECF2; font-size: 15px; }");
    inner->addWidget(m_edit);

    outer->addWidget(box);

    connect(m_edit, &QLineEdit::returnPressed, this, &QDialog::accept);
    connect(this, &QDialog::finished, this, &QObject::deleteLater);

    if (auto *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.center().x() - width() / 2, area.center().y() - height() / 2);
    }
}

QString QuickAddDialog::text() const { return m_edit->text().trimmed(); }

void QuickAddDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    m_edit->setFocus();
}
