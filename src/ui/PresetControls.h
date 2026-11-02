#pragma once

#include "core/Color.h"

#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <vector>

class QGridLayout;
class QLabel;

namespace fg {

class Project;

/**
 * The open scene's preset controls (docs/presets.md, section 3) at the top of the Inspector. Each control reads its
 * value from its first target and writes all of them, so it never disagrees with the layer and effect fields it
 * drives; hidden when the scene has none.
 */
class PresetControls : public QWidget {
    Q_OBJECT
public:
    explicit PresetControls(Project* project, QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* e) override;

private:
    void rebuild();
    void sync();
    /** Rename or remove the control at index in the scene's preset block. */
    void showControlMenu(int index, const QPoint& globalPos);
    /** A hue control's "base" colour, else the dominant saturated colour of the layer at phase. */
    Lab baseColor(const QJsonObject& control, int layer, double phase) const;

    Project* m_project;
    QLabel* m_title;
    QGridLayout* m_grid;
    std::vector<QWidget*> m_rowWidgets;
    /** One per control: puts the scene's current value into the editor without emitting anything. */
    std::vector<std::function<void()>> m_syncers;
    bool m_syncing = false;
};

} // namespace fg
