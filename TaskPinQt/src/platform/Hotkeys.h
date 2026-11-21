#pragma once

#include <QList>
#include <QObject>
#include <QString>

/// Global hotkeys through RegisterHotKey on a hidden message window (thread messages are not
/// reliably delivered to Qt's native event filter).
class Hotkeys : public QObject {
    Q_OBJECT
public:
    enum Action {
        Snip = 1,
        Full = 2,
        Last = 3,
        Pin = 4,
        Pins = 5,
        QuickAdd = 6,
        ToggleCard = 7,
        History = 8,
    };

    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys() override;

    /// Registers one chord like "Ctrl+Alt+A". Returns false when the OS refuses it.
    bool bind(int id, const QString &chord);
    void unbindAll();
    /// The chord that is actually registered for this action, empty when registration failed.
    QString label(int id) const;

signals:
    void triggered(int id);

private slots:
    void relay(int id);

private:
    struct Binding {
        int id;
        unsigned mods;
        unsigned vk;
        QString chord;
    };
    QList<Binding> m_bindings;
    void *m_hwnd = nullptr;
};
