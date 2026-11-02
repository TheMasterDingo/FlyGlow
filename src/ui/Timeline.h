#pragma once

#include <QVector>
#include <QWidget>

namespace fg {

class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget* parent = nullptr);

    void setLoopSeconds(double s);
    void setPhase(double phase);
    double phase() const { return m_phase; }
    /** times are loop phases in [0, 1); an empty list means the current layer has no keyframes. */
    void setKeys(const QVector<double>& times, int selected);
    int selectedKey() const { return m_selected; }

    /** A range of the loop picked by dragging below the ruler; from < to. */
    bool hasSelection() const { return m_selTo > m_selFrom; }
    double selectionFrom() const { return m_selFrom; }
    double selectionTo() const { return m_selTo; }
    void clearSelection();

    QSize sizeHint() const override { return {600, 64}; }
    QSize minimumSizeHint() const override { return {200, 64}; }

signals:
    void scrubStarted();
    void phaseScrubbed(double phase);
    void keySelected(int index);
    void keyDragStarted(int index);
    void keyMoved(int index, double time);
    void keyDragFinished();
    void keyContextMenu(int index, const QPoint& globalPos);
    void selectionChanged();
    /** Right click away from the keys. */
    void contextMenuRequested(double phase, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF track() const;
    double xToPhase(double x) const;
    double phaseToX(double ph) const;
    int keyAtPos(const QPointF& p) const;
    /** The band with the ticks and the playhead's head, where dragging moves the playhead. */
    bool onRuler(const QPointF& p) const;
    static constexpr double kRulerHeight = 17.0;
    /** -1 the selection's start edge, 1 its end edge, 0 neither. */
    int selectionEdgeAt(const QPointF& p) const;
    double snapped(double phase, Qt::KeyboardModifiers mods) const;

    double m_loopSeconds = 4;
    double m_phase = 0;
    QVector<double> m_keys;
    int m_selected = -1;
    double m_selFrom = 0, m_selTo = 0;
    double m_selAnchor = 0;
    enum class Drag { None, Scrub, Key, Select } m_drag = Drag::None;
    int m_dragKey = -1;
};

} // namespace fg
