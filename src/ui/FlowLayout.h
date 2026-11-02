#pragma once

#include <QLayout>
#include <QList>

namespace fg {

/** Lays its items out left to right and wraps onto the next row when they run out of width, like words. */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override { return int(m_items.size()); }
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& rect) override;

private:
    /** Places the items in rect (or only measures, when apply is false); returns the height used. */
    int arrange(const QRect& rect, bool apply) const;

    QList<QLayoutItem*> m_items;
    int m_gap;
};

} // namespace fg
