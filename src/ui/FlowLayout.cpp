#include "FlowLayout.h"

#include <QWidget>
#include <algorithm>

namespace fg {

FlowLayout::FlowLayout(QWidget* parent, int spacing) : QLayout(parent), m_gap(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items[index] : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, true);
}

int FlowLayout::arrange(const QRect& rect, bool apply) const
{
    const QRect area = rect.marginsRemoved(contentsMargins());
    int x = area.x(), y = area.y(), rowHeight = 0;
    for (QLayoutItem* item : m_items) {
        if (item->isEmpty())
            continue;
        const QSize hint = item->sizeHint();
        if (x > area.x() && x + hint.width() > area.right() + 1) {
            x = area.x();
            y += rowHeight + m_gap;
            rowHeight = 0;
        }
        if (apply)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + m_gap;
        rowHeight = std::max(rowHeight, hint.height());
    }
    return y + rowHeight - rect.y() + contentsMargins().bottom();
}

} // namespace fg
