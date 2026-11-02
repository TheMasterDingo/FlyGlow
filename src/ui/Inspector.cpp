#include "Inspector.h"

#include "Controls.h"
#include "Icons.h"
#include "ParamPanel.h"
#include "PresetControls.h"
#include "Project.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Translate.h"
#include "core/Effect.h"
#include "core/KeyframeEffect.h"
#include "core/LedLayout.h"
#include "shader/ShaderEffect.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

namespace fg {

namespace {

QToolButton* toolButton(QWidget* parent, icons::Id icon)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(18, 18));
    b->setObjectName("Flat");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void setTip(QWidget* w, const QString& tip)
{
    w->setToolTip(tip);
    w->setAccessibleName(tip);
}

QLabel* label(QWidget* parent, const char* objectName, int topMargin = 0)
{
    auto* l = new QLabel(parent);
    l->setObjectName(objectName);
    l->setContentsMargins(0, topMargin, 0, 0);
    return l;
}

class LayerRow : public QWidget {
public:
    LayerRow(QWidget* parent) : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(4, 4, 8, 4);
        h->setSpacing(8);
        eye = new QToolButton(this);
        eye->setObjectName("Flat");
        eye->setIconSize(QSize(16, 16));
        eye->setCursor(Qt::PointingHandCursor);
        eye->setToolTip(Inspector::tr("Show / hide layer"));
        thumb = new QLabel(this);
        thumb->setFixedSize(40, 36);
        auto* texts = new QVBoxLayout();
        texts->setSpacing(0);
        name = new QLabel(this);
        sub = new QLabel(this);
        sub->setObjectName("Dim");
        QFont f = sub->font();
        f.setPointSizeF(8.5);
        sub->setFont(f);
        texts->addWidget(name);
        texts->addWidget(sub);
        h->addWidget(eye);
        h->addWidget(thumb);
        h->addLayout(texts, 1);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    QToolButton* eye;
    QLabel* thumb;
    QLabel* name;
    QLabel* sub;
};

} // namespace

Inspector::Inspector(Project* project, QWidget* parent) : QWidget(parent), m_project(project)
{
    setObjectName("Panel");
    setAttribute(Qt::WA_StyledBackground);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 12, 10);
    root->setSpacing(4);

    root->addWidget(new PresetControls(project, this));

    auto* head = new QHBoxLayout();
    m_layersTitle = label(this, "SectionTitle");
    head->addWidget(m_layersTitle, 1);
    m_addBtn = toolButton(this, icons::Id::Plus);
    m_dupBtn = toolButton(this, icons::Id::Duplicate);
    m_upBtn = toolButton(this, icons::Id::Up);
    m_downBtn = toolButton(this, icons::Id::Down);
    m_delBtn = toolButton(this, icons::Id::Trash);
    for (QToolButton* b : {m_addBtn, m_dupBtn, m_upBtn, m_downBtn, m_delBtn})
        head->addWidget(b);
    root->addLayout(head);

    m_list = new QListWidget(this);
    m_list->setFixedHeight(200);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    root->addWidget(m_list);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 6, 0);
    cv->setSpacing(4);

    m_layerBox = new QWidget(content);
    auto* lv = new QVBoxLayout(m_layerBox);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(4);
    m_layerTitle = label(m_layerBox, "SectionTitle");
    lv->addWidget(m_layerTitle);
    m_name = new QLineEdit(m_layerBox);
    lv->addWidget(m_name);

    m_blendLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_blendLabel);
    m_blend = new QComboBox(m_layerBox);
    for (int i = 0; i < blendModeNames().size(); ++i)
        m_blend->addItem(QString());
    lv->addWidget(m_blend);

    m_opacityLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_opacityLabel);
    m_opacity = new SliderRow(m_layerBox);
    m_opacity->setRange(0, 100, 1, "%");
    lv->addWidget(m_opacity);

    m_colorLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_colorLabel);
    auto* colorGrid = new QGridLayout();
    colorGrid->setHorizontalSpacing(8);
    colorGrid->setVerticalSpacing(4);
    colorGrid->setColumnStretch(1, 1);
    m_hueLabel = new QLabel(m_layerBox);
    m_hue = new SliderRow(m_layerBox);
    m_hue->setRange(-180, 180, 1, QStringLiteral("°"));
    m_satLabel = new QLabel(m_layerBox);
    m_saturation = new SliderRow(m_layerBox);
    m_saturation->setRange(0, 400, 1, "%");
    m_lightLabel = new QLabel(m_layerBox);
    m_lightness = new SliderRow(m_layerBox);
    m_lightness->setRange(0, 400, 1, "%");
    m_contrastLabel = new QLabel(m_layerBox);
    m_contrast = new SliderRow(m_layerBox);
    m_contrast->setRange(0, 400, 1, "%");
    m_tintLabel = new QLabel(m_layerBox);
    auto* tintBox = new QWidget(m_layerBox);
    auto* tintRow = new QHBoxLayout(tintBox);
    tintRow->setContentsMargins(0, 0, 0, 0);
    tintRow->setSpacing(6);
    m_tint = new ColorButton(tintBox);
    m_tintReset = new QToolButton(tintBox);
    m_tintReset->setObjectName("Flat");
    m_tintReset->setIcon(icons::get(icons::Id::Undo));
    m_tintReset->setCursor(Qt::PointingHandCursor);
    tintRow->addWidget(m_tint, 1);
    tintRow->addWidget(m_tintReset);
    colorGrid->addWidget(m_hueLabel, 0, 0);
    colorGrid->addWidget(m_hue, 0, 1);
    colorGrid->addWidget(m_satLabel, 1, 0);
    colorGrid->addWidget(m_saturation, 1, 1);
    colorGrid->addWidget(m_lightLabel, 2, 0);
    colorGrid->addWidget(m_lightness, 2, 1);
    colorGrid->addWidget(m_contrastLabel, 3, 0);
    colorGrid->addWidget(m_contrast, 3, 1);
    colorGrid->addWidget(m_tintLabel, 4, 0);
    colorGrid->addWidget(tintBox, 4, 1);
    lv->addLayout(colorGrid);

    // Pictures only (media and painted layers): procedural effects are soft or sharp by design.
    m_detailBox = new QWidget(m_layerBox);
    auto* dv = new QVBoxLayout(m_detailBox);
    dv->setContentsMargins(0, 0, 0, 0);
    dv->setSpacing(4);
    m_glowTitle = label(m_detailBox, "Dim", 8);
    dv->addWidget(m_glowTitle);
    auto* glowGrid = new QGridLayout();
    glowGrid->setHorizontalSpacing(8);
    glowGrid->setVerticalSpacing(4);
    glowGrid->setColumnStretch(1, 1);
    m_glowLabel = new QLabel(m_detailBox);
    m_glow = new SliderRow(m_detailBox);
    m_glow->setRange(0, 100, 1, "%");
    m_glowSizeLabel = new QLabel(m_detailBox);
    m_glowSize = new SliderRow(m_detailBox);
    m_glowSize->setRange(0.5, 4, 0.1);
    m_blurLabel = new QLabel(m_detailBox);
    m_blur = new SliderRow(m_detailBox);
    m_blur->setRange(0, 3, 0.1);
    m_sharpenLabel = new QLabel(m_detailBox);
    m_sharpen = new SliderRow(m_detailBox);
    m_sharpen->setRange(0, 200, 1, "%");
    glowGrid->addWidget(m_glowLabel, 0, 0);
    glowGrid->addWidget(m_glow, 0, 1);
    glowGrid->addWidget(m_glowSizeLabel, 1, 0);
    glowGrid->addWidget(m_glowSize, 1, 1);
    glowGrid->addWidget(m_blurLabel, 2, 0);
    glowGrid->addWidget(m_blur, 2, 1);
    glowGrid->addWidget(m_sharpenLabel, 3, 0);
    glowGrid->addWidget(m_sharpen, 3, 1);
    dv->addLayout(glowGrid);
    lv->addWidget(m_detailBox);

    // Keyframe layers keep their timing in the keys, so this box is hidden for them.
    m_timeBox = new QWidget(m_layerBox);
    auto* tv = new QVBoxLayout(m_timeBox);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(4);
    m_timeTitle = label(m_timeBox, "Dim", 8);
    tv->addWidget(m_timeTitle);
    auto* timeGrid = new QGridLayout();
    timeGrid->setHorizontalSpacing(8);
    timeGrid->setVerticalSpacing(4);
    timeGrid->setColumnStretch(1, 1);
    m_speedLabel = new QLabel(m_timeBox);
    m_speed = new SliderRow(m_timeBox);
    m_speed->setRange(0.25, 4, 0.05, QStringLiteral("×"));
    m_offsetLabel = new QLabel(m_timeBox);
    m_offset = new SliderRow(m_timeBox);
    m_offset->setRange(0, 100, 1, "%");
    timeGrid->addWidget(m_speedLabel, 0, 0);
    timeGrid->addWidget(m_speed, 0, 1);
    timeGrid->addWidget(m_offsetLabel, 1, 0);
    timeGrid->addWidget(m_offset, 1, 1);
    tv->addLayout(timeGrid);
    lv->addWidget(m_timeBox);

    m_maskLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_maskLabel);
    auto* maskRow = new QHBoxLayout();
    maskRow->setSpacing(6);
    m_maskPaint = new QToolButton(m_layerBox);
    m_maskPaint->setIcon(icons::get(icons::Id::Mask));
    m_maskPaint->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_maskPaint->setCheckable(true);
    m_maskClear = toolButton(m_layerBox, icons::Id::Clear);
    m_maskInvert = new QToolButton(m_layerBox);
    maskRow->addWidget(m_maskPaint, 1);
    maskRow->addWidget(m_maskInvert);
    maskRow->addWidget(m_maskClear);
    lv->addLayout(maskRow);
    m_maskState = new QLabel(m_layerBox);
    m_maskState->setObjectName("Dim");
    lv->addWidget(m_maskState);
    cv->addWidget(m_layerBox);

    m_effectTitle = label(content, "SectionTitle");
    cv->addWidget(m_effectTitle);
    m_effectHint = new QLabel(content);
    m_effectHint->setWordWrap(true);
    m_effectHint->setObjectName("Dim");
    cv->addWidget(m_effectHint);
    m_shaderEdit = new QPushButton(icons::get(icons::Id::Code), QString(), content);
    m_shaderEdit->setVisible(false);
    cv->addWidget(m_shaderEdit);
    m_params = new ParamPanel(content);
    cv->addWidget(m_params);
    cv->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* addMenu = new QMenu(this);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        std::map<QString, QMenu*> cats;
        for (const auto& e : EffectRegistry::instance().entries()) {
            if (!cats.count(e.category))
                cats[e.category] = addMenu->addMenu(trEffect(e.category));
            const QString t = e.typeId;
            cats[e.category]->addAction(trEffect(e.name), this, [this, t] {
                m_project->checkpoint();
                m_project->addEffectLayer(t);
            });
        }
    });
    m_addBtn->setMenu(addMenu);
    m_addBtn->setPopupMode(QToolButton::InstantPopup);

    connect(m_dupBtn, &QToolButton::clicked, this, [this] {
        if (m_project->currentLayer()) {
            m_project->checkpoint();
            m_project->duplicateLayer(m_project->currentIndex());
        }
    });
    connect(m_delBtn, &QToolButton::clicked, this, [this] {
        if (m_project->currentLayer()) {
            m_project->checkpoint();
            m_project->removeLayer(m_project->currentIndex());
        }
    });
    connect(m_upBtn, &QToolButton::clicked, this, [this] {
        const int i = m_project->currentIndex();
        if (i >= 0 && i + 1 < m_project->layerCount()) {
            m_project->checkpoint();
            m_project->moveLayer(i, i + 1);
        }
    });
    connect(m_downBtn, &QToolButton::clicked, this, [this] {
        const int i = m_project->currentIndex();
        if (i > 0) {
            m_project->checkpoint();
            m_project->moveLayer(i, i - 1);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_syncing && row >= 0)
            m_project->setCurrentIndex(rowToLayer(row));
    });

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l || m_name->text() == l->name)
            return;
        m_project->checkpoint();
        l->name = m_name->text();
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_blend, &QComboBox::currentIndexChanged, this, [this](int idx) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        m_project->checkpoint();
        l->blend = BlendMode(idx);
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_opacity, &SliderRow::editStarted, m_project, &Project::checkpoint);
    connect(m_opacity, &SliderRow::valueChanged, this, [this](double v) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        l->opacity = v / 100.0;
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_hue, &SliderRow::editStarted, m_project, &Project::checkpoint);
    connect(m_hue, &SliderRow::valueChanged, this, [this](double v) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        l->hue = v;
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_saturation, &SliderRow::editStarted, m_project, &Project::checkpoint);
    connect(m_saturation, &SliderRow::valueChanged, this, [this](double v) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        l->saturation = v / 100.0;
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    auto bindSlider = [this](SliderRow* slider, auto apply) {
        connect(slider, &SliderRow::editStarted, m_project, &Project::checkpoint);
        connect(slider, &SliderRow::valueChanged, this, [this, apply](double v) {
            Layer* l = m_project->currentLayer();
            if (m_syncing || !l)
                return;
            apply(l, v);
            m_project->notifyLayerChanged(m_project->currentIndex());
        });
    };
    bindSlider(m_lightness, [](Layer* l, double v) { l->lightness = v / 100.0; });
    bindSlider(m_contrast, [](Layer* l, double v) { l->contrast = v / 100.0; });
    bindSlider(m_glow, [](Layer* l, double v) { l->glow = v / 100.0; });
    bindSlider(m_glowSize, [](Layer* l, double v) { l->glowSize = v; });
    bindSlider(m_blur, [](Layer* l, double v) { l->blur = v; });
    bindSlider(m_sharpen, [](Layer* l, double v) { l->sharpen = v / 100.0; });
    bindSlider(m_speed, [](Layer* l, double v) { l->timeScale = v; });
    bindSlider(m_offset, [](Layer* l, double v) { l->timeOffset = std::min(v / 100.0, 0.999); });
    connect(m_tint, &ColorButton::editStarted, m_project, &Project::checkpoint);
    connect(m_tint, &ColorButton::colorChanged, this, [this](const QColor& c) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        l->tint = c;
        m_tintReset->setEnabled(l->tints());
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_tintReset, &QToolButton::clicked, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l || !l->tints())
            return;
        m_project->checkpoint();
        l->tint = Qt::white;
        m_project->notifyLayerChanged(m_project->currentIndex());
        syncCurrent();
    });
    connect(m_maskPaint, &QToolButton::toggled, this, &Inspector::maskPaintToggled);
    connect(m_shaderEdit, &QPushButton::clicked, this, &Inspector::shaderEditRequested);
    connect(m_maskClear, &QToolButton::clicked, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l || l->mask.empty())
            return;
        m_project->checkpoint();
        l->mask.clear();
        m_project->notifyLayerChanged(m_project->currentIndex());
        syncCurrent();
    });
    connect(m_maskInvert, &QToolButton::clicked, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l)
            return;
        m_project->checkpoint();
        // An empty mask means the whole dock, so its inverse covers nothing.
        if (l->mask.empty())
            l->mask.assign(size_t(LedLayout::cd2().count()), 0.f);
        else
            for (float& w : l->mask)
                w = 1.f - w;
        m_project->notifyLayerChanged(m_project->currentIndex());
        syncCurrent();
    });

    // Right-clicking a setting offers it to the preset settings at the top of the Inspector.
    auto layerName = [this](int li) {
        const Layer* l = m_project->layer(li);
        return l && !l->name.isEmpty() ? l->name : tr("Layer %1").arg(li + 1);
    };
    auto exposable = [this, layerName](QLabel* label, const QString& type, const QString& prop, double min, double max,
                                       double step, const QString& suffix, double scale) {
        label->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(label, &QWidget::customContextMenuRequested, this,
                [=, this](const QPoint& p) {
                    const int li = m_project->currentIndex();
                    if (!m_project->layer(li))
                        return;
                    QJsonObject target{{"layer", li}};
                    if (!prop.isEmpty())
                        target.insert("prop", prop);
                    QJsonObject control{{"type", type}};
                    if (type == QLatin1String("number")) {
                        control.insert("min", min);
                        control.insert("max", max);
                        control.insert("step", step);
                        control.insert("suffix", suffix);
                        if (scale != 1.0) {
                            target.insert("from", min * scale);
                            target.insert("to", max * scale);
                        }
                    }
                    control.insert("targets", QJsonArray{target});
                    offerPresetControl(control, label->text() + QStringLiteral(" · ") + layerName(li),
                                       label->mapToGlobal(p));
                });
    };
    exposable(m_opacityLabel, "opacity", {}, 0, 0, 0, {}, 1);
    for (QLabel* l : {m_colorLabel, m_hueLabel, m_satLabel, m_lightLabel})
        exposable(l, "hue", {}, 0, 0, 0, {}, 1);
    exposable(m_tintLabel, "color", "tint", 0, 0, 0, {}, 1);
    exposable(m_glowLabel, "number", "glow", 0, 100, 1, "%", 0.01);
    exposable(m_glowSizeLabel, "number", "glowSize", 0.5, 4, 0.1, {}, 1);
    exposable(m_blurLabel, "number", "blur", 0, 3, 0.1, {}, 1);
    exposable(m_sharpenLabel, "number", "sharpen", 0, 200, 1, "%", 0.01);
    exposable(m_contrastLabel, "number", "contrast", 0, 400, 1, "%", 0.01);
    connect(m_params, &ParamPanel::paramContextMenu, this, [this, layerName](const QString& id, const QPoint& pos) {
        const int li = m_project->currentIndex();
        const Layer* l = m_project->layer(li);
        if (!l || !l->effect)
            return;
        for (const ParamDesc& d : l->effect->params()) {
            if (d.id != id)
                continue;
            QJsonObject control;
            if (d.type == ParamDesc::Type::Color) {
                control.insert("type", "color");
            } else {
                const bool angle = d.type == ParamDesc::Type::Angle;
                control.insert("type", "number");
                control.insert("min", angle ? 0.0 : d.min);
                control.insert("max", angle ? 360.0 : d.max);
                control.insert("step", d.type == ParamDesc::Type::Int ? std::max(1.0, std::round(d.step)) : d.step);
                control.insert("suffix", angle ? QStringLiteral("°") : d.suffix);
            }
            control.insert("targets", QJsonArray{QJsonObject{{"layer", li}, {"param", id}}});
            offerPresetControl(control, trEffect(d.label) + QStringLiteral(" · ") + layerName(li), pos);
            return;
        }
    });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this, layerName](const QPoint& p) {
        const QListWidgetItem* item = m_list->itemAt(p);
        if (!item)
            return;
        const int li = rowToLayer(m_list->row(item));
        offerPresetControl(QJsonObject{{"type", "toggle"}, {"targets", QJsonArray{QJsonObject{{"layer", li}}}}},
                           layerName(li), m_list->viewport()->mapToGlobal(p));
    });

    connect(m_params, &ParamPanel::editStarted, m_project, &Project::checkpoint);
    connect(m_params, &ParamPanel::changed, this, [this] {
        m_project->notifyLayerChanged(m_project->currentIndex());
    });

    connect(m_project, &Project::layersChanged, this, [this] {
        rebuildList();
        syncCurrent();
    });
    connect(m_project, &Project::currentChanged, this, [this] { syncCurrent(); });
    connect(m_project, &Project::layerChanged, this, [this](int i) {
        refreshRow(i);
        if (i == m_project->currentIndex()) {
            syncEffectHint();   // a shader layer becomes a filter with its input switch
            Layer* l = m_project->currentLayer();
            const int n = l && !l->mask.empty()
                              ? int(std::count_if(l->mask.begin(), l->mask.end(), [](float w) { return w > 0.5f; }))
                              : -1;
            m_maskState->setText(n < 0 ? tr("The layer affects the whole dock")
                                       : tr("The layer affects %1 of 162 LEDs").arg(n));
        }
    });

    retranslateUi();
}

void Inspector::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(e);
}

void Inspector::retranslateUi()
{
    m_layersTitle->setText(tr("Layers").toUpper());
    setTip(m_addBtn, tr("Add layer"));
    setTip(m_dupBtn, tr("Duplicate layer"));
    setTip(m_upBtn, tr("Move layer up"));
    setTip(m_downBtn, tr("Move layer down"));
    setTip(m_delBtn, tr("Delete layer"));

    m_layerTitle->setText(tr("Layer").toUpper());
    m_name->setPlaceholderText(tr("Layer name"));
    m_blendLabel->setText(tr("Blend mode"));
    const QStringList blends = trEffects(blendModeNames());
    for (int i = 0; i < blends.size(); ++i)
        m_blend->setItemText(i, blends[i]);
    m_opacityLabel->setText(tr("Opacity"));
    m_colorLabel->setText(tr("Colour"));
    m_hueLabel->setText(tr("Hue"));
    m_hue->setToolTip(tr("Turns every colour of the layer around the colour wheel; brightness stays"));
    m_satLabel->setText(tr("Saturation"));
    m_saturation->setToolTip(tr("0% turns the layer grey, 200% makes its colours twice as vivid"));
    m_lightLabel->setText(tr("Lightness"));
    m_lightness->setToolTip(tr("0% turns the layer black, above 100% brightens it towards white"));
    m_contrastLabel->setText(tr("Contrast"));
    m_contrast->setToolTip(tr("Above 100% dark parts get darker and light parts lighter; below, both move towards "
                              "grey. Colours keep their hue"));
    m_tintLabel->setText(tr("Tint"));
    m_tint->setToolTip(tr("Turns the layer into shades of one colour: white and full colours take it exactly"));
    m_tintReset->setToolTip(tr("No tint"));
    m_glowTitle->setText(tr("Glow and sharpness"));
    m_glowLabel->setText(tr("Glow"));
    m_glow->setToolTip(tr("Lit LEDs light up their neighbours, like light spilling through frosted plastic"));
    m_glowSizeLabel->setText(tr("Glow size"));
    m_glowSize->setToolTip(tr("How far the glow reaches, in LEDs"));
    m_blurLabel->setText(tr("Blur"));
    m_blur->setToolTip(tr("Softens the layer across neighbouring LEDs, in LEDs"));
    m_sharpenLabel->setText(tr("Sharpen"));
    m_sharpen->setToolTip(tr("Sets each LED apart from its neighbours so outlines read crisper; 100% is the usual "
                             "amount"));
    m_timeTitle->setText(tr("Time"));
    m_speedLabel->setText(tr("Speed"));
    m_speed->setToolTip(tr("How many of the layer's own loops play per scene loop. Whole numbers loop seamlessly."));
    m_offsetLabel->setText(tr("Offset"));
    m_offset->setToolTip(tr("Starts the layer's loop later, as a share of its length"));
    m_maskLabel->setText(tr("Area"));
    m_maskPaint->setText(tr("Paint area"));
    m_maskPaint->setToolTip(tr("Brush over the LEDs this layer should affect.\n"
                               "The eraser in the tool strip removes them from the area."));
    setTip(m_maskClear, tr("Whole dock (reset the area)"));
    m_maskInvert->setText(tr("Invert"));
    m_maskInvert->setToolTip(tr("Invert the area"));
    m_shaderEdit->setText(tr("Edit shader code…"));

    rebuildList();
    syncCurrent();
}

void Inspector::setMaskPainting(bool on)
{
    const QSignalBlocker block(m_maskPaint);
    m_maskPaint->setChecked(on);
}

void Inspector::offerPresetControl(QJsonObject control, const QString& name, const QPoint& globalPos)
{
    QMenu menu(this);
    QAction* add = menu.addAction(tr("Show in preset settings…"));
    add->setToolTip(tr("Puts this setting among the preset settings at the top of the Inspector; they are saved with "
                       "the scene and with Save as preset"));
    if (menu.exec(globalPos) != add)
        return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Preset setting"), tr("Name in the preset settings:"),
                                                QLineEdit::Normal, name, &ok)
                              .trimmed();
    if (!ok || label.isEmpty())
        return;
    control.insert("label", QJsonObject{{QLocale().name().left(2), label}});
    m_project->checkpoint();
    QJsonObject preset = m_project->scene().preset;
    QJsonArray controls = preset.value("controls").toArray();
    controls.append(control);
    preset.insert("controls", controls);
    m_project->scene().preset = preset;
    m_project->notifyLayersChanged();
}

int Inspector::rowToLayer(int row) const
{
    return m_project->layerCount() - 1 - row;
}

int Inspector::layerToRow(int layer) const
{
    return m_project->layerCount() - 1 - layer;
}

void Inspector::rebuildList()
{
    m_syncing = true;
    m_list->clear();
    for (int row = 0; row < m_project->layerCount(); ++row) {
        const int li = rowToLayer(row);
        auto* item = new QListWidgetItem();
        item->setSizeHint(QSize(10, 48));
        m_list->addItem(item);
        auto* w = new LayerRow(m_list);
        m_list->setItemWidget(item, w);
        connect(w->eye, &QToolButton::clicked, this, [this, li] {
            Layer* l = m_project->layer(li);
            if (!l)
                return;
            m_project->checkpoint();
            l->visible = !l->visible;
            m_project->notifyLayerChanged(li);
        });
        refreshRow(li);
    }
    m_list->setCurrentRow(layerToRow(m_project->currentIndex()));
    m_syncing = false;
}

void Inspector::refreshRow(int layerIndex)
{
    Layer* l = m_project->layer(layerIndex);
    QListWidgetItem* item = m_list->item(layerToRow(layerIndex));
    if (!l || !item)
        return;
    auto* w = static_cast<LayerRow*>(m_list->itemWidget(item));
    if (!w)
        return;
    w->eye->setIcon(icons::get(l->visible ? icons::Id::Eye : icons::Id::EyeOff,
                               l->visible ? theme::kText : theme::kTextDim));
    w->name->setText(l->name.isEmpty() ? tr("Layer %1").arg(layerIndex + 1) : l->name);
    QStringList bits;
    if (l->effect)
        bits << trEffect(l->effect->name());
    if (l->effect && l->effect->isFilter())
        bits << tr("filter");
    if (l->blend != BlendMode::Normal)
        bits << trEffect(blendModeNames().value(int(l->blend)));
    if (l->opacity < 0.999)
        bits << QStringLiteral("%1%").arg(int(std::lround(l->opacity * 100)));
    if (!l->mask.empty())
        bits << tr("area");
    w->sub->setText(bits.join(QStringLiteral(" · ")));
    if (l->effect)
        w->thumb->setPixmap(renderEffectThumbnail(*l->effect, 0.18, QSize(40, 36), 2.0, l));
    w->setEnabled(true);
    w->name->setStyleSheet(l->visible ? QString() : QStringLiteral("color: %1;").arg(theme::kTextDim.name()));
}

void Inspector::syncCurrent()
{
    m_syncing = true;
    const int cur = m_project->currentIndex();
    if (m_list->currentRow() != layerToRow(cur))
        m_list->setCurrentRow(layerToRow(cur));
    Layer* l = m_project->currentLayer();
    m_layerBox->setVisible(l != nullptr);
    m_dupBtn->setEnabled(l != nullptr);
    m_delBtn->setEnabled(l != nullptr);
    m_upBtn->setEnabled(l && cur + 1 < m_project->layerCount());
    m_downBtn->setEnabled(l && cur > 0);
    if (l) {
        m_name->setText(l->name);
        m_blend->setCurrentIndex(int(l->blend));
        m_opacity->setValue(l->opacity * 100.0);
        m_hue->setValue(l->hue);
        m_saturation->setValue(l->saturation * 100.0);
        m_lightness->setValue(l->lightness * 100.0);
        m_contrast->setValue(l->contrast * 100.0);
        m_tint->setColor(l->tint);
        m_tintReset->setEnabled(l->tints());
        m_glow->setValue(l->glow * 100.0);
        m_glowSize->setValue(l->glowSize);
        m_blur->setValue(l->blur);
        m_sharpen->setValue(l->sharpen * 100.0);
        // A layer of another kind that already has glow, blur or sharpening (from a file) still shows them.
        const QString type = l->effect ? l->effect->typeId() : QString();
        m_detailBox->setVisible(type == QLatin1String("media") || type == QLatin1String("keyframes") || l->glow > 0.0
                                || l->blur > 0.0 || l->sharpen > 0.0);
        m_speed->setValue(l->timeScale);
        m_offset->setValue(l->timeOffset * 100.0);
        m_timeBox->setVisible(!dynamic_cast<KeyframeEffect*>(l->effect.get()) || l->timeScale != 1.0
                              || l->timeOffset != 0.0);
        const int n = l->mask.empty()
                          ? -1
                          : int(std::count_if(l->mask.begin(), l->mask.end(), [](float w) { return w > 0.5f; }));
        m_maskState->setText(n < 0 ? tr("The layer affects the whole dock")
                                   : tr("The layer affects %1 of 162 LEDs").arg(n));
        const bool isKeys = dynamic_cast<KeyframeEffect*>(l->effect.get()) != nullptr;
        m_effectTitle->setText(l->effect ? tr("Effect · %1").arg(trEffect(l->effect->name())).toUpper()
                                         : tr("Effect").toUpper());
        syncEffectHint();
        m_shaderEdit->setVisible(dynamic_cast<ShaderEffect*>(l->effect.get()) != nullptr);
        m_params->setEffect(isKeys ? nullptr : l->effect.get());
    } else {
        m_effectTitle->setText(tr("Add a layer from the effect library on the left"));
        m_effectHint->setVisible(false);
        m_shaderEdit->setVisible(false);
        m_params->setEffect(nullptr);
    }
    m_syncing = false;
}

void Inspector::syncEffectHint()
{
    const Layer* l = m_project->currentLayer();
    const bool isKeys = l && dynamic_cast<KeyframeEffect*>(l->effect.get()) != nullptr;
    const bool isFilter = l && l->effect && l->effect->isFilter();
    m_effectHint->setVisible(isKeys || isFilter);
    if (isKeys)
        m_effectHint->setText(tr("Paint right on the dock: brush, eraser, fill and eyedropper are in the strip above it.\n"
                                 "Strokes go into the keyframe at the playhead; if there is none, one is created. "
                                 "Colours flow smoothly between keyframes."));
    else if (isFilter)
        m_effectHint->setText(tr("A filter: it reworks everything below this layer. Opacity, blend mode and area say how "
                                 "much of the result goes over the original; the colour settings apply to the result."));
}

} // namespace fg
