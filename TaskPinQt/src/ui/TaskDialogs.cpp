#include "ui/TaskDialogs.h"
#include "core/Store.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

void showTaskDetails(Store *store, const QString &id, QWidget *parent)
{
    const QString name = "task-details-" + id;
    if (auto *existing = parent->findChild<QDialog *>(name)) {
        existing->show(); existing->raise(); existing->activateWindow(); return;
    }
    if (store->task(id).id.isEmpty()) return;
    auto *dialog = new QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint);
    dialog->setObjectName(name);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("TaskPin · 任务详情"));
    dialog->resize(560, 420);
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(dialog);
    auto *toggle = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    const auto reload = [=] {
        const Task t = store->task(id);
        if (t.id.isEmpty()) { dialog->close(); return; }
        QString content = t.title;
        if (!t.note.isEmpty()) content += QStringLiteral("\n\n备注\n") + t.note;
        if (!t.ocrText.isEmpty()) content += QStringLiteral("\n\n截图原文\n") + t.ocrText;
        if (!t.due.isEmpty()) content += QStringLiteral("\n\n截止时间：") + t.due;
        if (text->toPlainText() != content) text->setPlainText(content);
        toggle->setText(t.isDone() ? QStringLiteral("恢复待办") : QStringLiteral("标记完成"));
        toggle->setEnabled(t.state != "dropped" && t.aiState != "pending");
    };
    QObject::connect(store, &Store::changed, dialog, reload);
    QObject::connect(toggle, &QPushButton::clicked, dialog, [=] {
        const auto t = store->task(id);
        if (!t.id.isEmpty()) store->setDone(id, !t.isDone());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    reload();
    dialog->show(); dialog->raise(); dialog->activateWindow();
}

void showCompletedTasks(Store *store, QWidget *parent)
{
    if (auto *existing = parent->findChild<QDialog *>("completed-tasks")) {
        existing->show(); existing->raise(); existing->activateWindow(); return;
    }
    auto *dialog = new QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint);
    dialog->setObjectName("completed-tasks");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("TaskPin · 已完成"));
    dialog->resize(580, 430);
    auto *layout = new QVBoxLayout(dialog);
    auto *search = new QLineEdit(dialog);
    search->setPlaceholderText(QStringLiteral("搜索已完成任务"));
    layout->addWidget(search);
    auto *list = new QListWidget(dialog);
    list->setWordWrap(true);
    list->setTextElideMode(Qt::ElideNone);
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(dialog);
    auto *restore = buttons->addButton(QStringLiteral("恢复待办"), QDialogButtonBox::ActionRole);
    auto *details = buttons->addButton(QStringLiteral("查看全文"), QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    const auto reload = [=] {
        const QString selected = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        list->clear();
        for (const auto &t : store->completedTasks()) {
            if (!t.title.contains(search->text(), Qt::CaseInsensitive)) continue;
            auto *item = new QListWidgetItem(t.title + "\n" + t.completed.left(16), list);
            item->setData(Qt::UserRole, t.id);
            item->setToolTip(t.title);
            if (t.id == selected) list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
        restore->setEnabled(list->currentItem()); details->setEnabled(list->currentItem());
    };
    QObject::connect(store, &Store::changed, dialog, reload);
    QObject::connect(search, &QLineEdit::textChanged, dialog, reload);
    QObject::connect(restore, &QPushButton::clicked, dialog, [=] {
        if (auto *item = list->currentItem()) {
            const QString id = item->data(Qt::UserRole).toString();
            store->setDone(id, false);
        }
    });
    const auto open = [=] {
        if (auto *item = list->currentItem()) showTaskDetails(store, item->data(Qt::UserRole).toString(), dialog);
    };
    QObject::connect(details, &QPushButton::clicked, dialog, open);
    QObject::connect(list, &QListWidget::itemDoubleClicked, dialog, open);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    reload(); dialog->show(); dialog->raise(); dialog->activateWindow();
}
