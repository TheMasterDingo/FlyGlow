#pragma once

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

namespace fg {

class ColorButton;
class ParamPanel;
class Project;
class SliderRow;

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(Project* project, QWidget* parent = nullptr);
    /** Updates the area button without emitting maskPaintToggled(). */
    void setMaskPainting(bool on);

signals:
    void maskPaintToggled(bool on);
    void shaderEditRequested();

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    void rebuildList();
    void refreshRow(int layerIndex);
    void syncCurrent();
    /** The hint above the effect settings, for painted layers and filters. */
    void syncEffectHint();
    int rowToLayer(int row) const;
    int layerToRow(int layer) const;
    /**
     * Offers a menu with "Show in preset settings…"; taking it asks for a name and appends control to the scene's
     * preset controls (docs/presets.md).
     */
    void offerPresetControl(QJsonObject control, const QString& name, const QPoint& globalPos);

    Project* m_project;
    QLabel* m_layersTitle;
    QListWidget* m_list;
    QToolButton* m_addBtn;
    QToolButton* m_dupBtn;
    QToolButton* m_delBtn;
    QToolButton* m_upBtn;
    QToolButton* m_downBtn;

    QWidget* m_layerBox;
    QLabel* m_layerTitle;
    QLineEdit* m_name;
    QLabel* m_blendLabel;
    QComboBox* m_blend;
    QLabel* m_opacityLabel;
    SliderRow* m_opacity;
    QLabel* m_colorLabel;
    QLabel* m_hueLabel;
    SliderRow* m_hue;
    QLabel* m_satLabel;
    SliderRow* m_saturation;
    QLabel* m_lightLabel;
    SliderRow* m_lightness;
    QLabel* m_contrastLabel;
    SliderRow* m_contrast;
    QLabel* m_tintLabel;
    ColorButton* m_tint;
    QToolButton* m_tintReset;
    QWidget* m_detailBox;
    QLabel* m_glowTitle;
    QLabel* m_glowLabel;
    SliderRow* m_glow;
    QLabel* m_glowSizeLabel;
    SliderRow* m_glowSize;
    QLabel* m_blurLabel;
    SliderRow* m_blur;
    QLabel* m_sharpenLabel;
    SliderRow* m_sharpen;
    QWidget* m_timeBox;
    QLabel* m_timeTitle;
    QLabel* m_speedLabel;
    SliderRow* m_speed;
    QLabel* m_offsetLabel;
    SliderRow* m_offset;
    QLabel* m_maskLabel;
    QToolButton* m_maskPaint;
    QToolButton* m_maskClear;
    QToolButton* m_maskInvert;
    QLabel* m_maskState;

    QLabel* m_effectTitle;
    QLabel* m_effectHint;
    QPushButton* m_shaderEdit;
    ParamPanel* m_params;
    bool m_syncing = false;
};

} // namespace fg
