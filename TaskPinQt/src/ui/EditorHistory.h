#pragma once
#include "ui/Annotation.h"

// A gesture stores the document before it changes; intermediate mouse moves never
// become separate undo steps. Implicitly shared vectors keep snapshots inexpensive.
class EditorHistory
{
public:
    void record(const QVector<Annotation> &before)
    {
        m_undo.append(before);
        m_redo.clear();
    }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void undo(QVector<Annotation> &items)
    {
        if (!canUndo()) return;
        m_redo.append(items);
        items = m_undo.takeLast();
    }
    void redo(QVector<Annotation> &items)
    {
        if (!canRedo()) return;
        m_undo.append(items);
        items = m_redo.takeLast();
    }

private:
    QVector<QVector<Annotation>> m_undo, m_redo;
};
