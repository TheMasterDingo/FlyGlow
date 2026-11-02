#include "PresetControls.h"

#include "Controls.h"
#include "Icons.h"
#include "PresetLibrary.h"
#include "Project.h"
#include "core/Effect.h"
#include "core/LedLayout.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QHBoxLayout>
#include <QToolButton>
#include <QJsonArray>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace fg {

namespace {

struct Target {
    int layer = -1;          /**< -1 for the scene */
    QString param;           /**< effect parameter, or "loopSeconds" on the scene */
    QString prop;            /**< a layer's own setting instead of an effect parameter, see layerProp() */
    int swap = -1;           /**< index into the layer's colour swaps, for a colour control */
    bool mapped = false;
    double from = 0, to = 1;
};

std::vector<Target> targetsOf(const QJsonObject& control)
{
    std::vector<Target> out;
    for (const QJsonValue& v : control.value(QStringLiteral("targets")).toArray()) {
        const QJsonObject o = v.toObject();
        Target t;
        if (o.contains(QStringLiteral("scene"))) {
            t.param = o.value(QStringLiteral("scene")).toString();
        } else {
            t.layer = o.value(QStringLiteral("layer")).toInt(-1);
            t.param = o.value(QStringLiteral("param")).toString();
            t.prop = o.value(QStringLiteral("prop")).toString();
            t.swap = o.value(QStringLiteral("swap")).toInt(-1);
        }
        t.mapped = o.contains(QStringLiteral("from")) && o.contains(QStringLiteral("to"));
        t.from = o.value(QStringLiteral("from")).toDouble();
        t.to = o.value(QStringLiteral("to")).toDouble();
        out.push_back(t);
    }
    return out;
}

const ParamDesc* findParam(const Effect* e, const QString& id, ParamDesc& storage)
{
    if (!e)
        return nullptr;
    for (const ParamDesc& d : e->params())
        if (d.id == id) {
            storage = d;
            return &storage;
        }
    return nullptr;
}

/** A JSON value as the parameter's own type: "#rrggbb" for colours, whole numbers for ints and choices. */
QVariant paramValue(const ParamDesc& d, const QJsonValue& v)
{
    switch (d.type) {
    case ParamDesc::Type::Color:
        return QColor(v.toString());
    case ParamDesc::Type::Int:
    case ParamDesc::Type::Choice:
        return int(std::lround(std::clamp(v.toDouble(), d.min, d.max)));
    case ParamDesc::Type::Bool:
        return v.toBool();
    default:
        return std::clamp(v.toDouble(), d.min, d.max);
    }
}

/** A layer's numeric settings by name, with the ranges the scene file allows. */
struct LayerProp {
    double Layer::*field;
    double min, max;
};

const LayerProp* layerProp(const QString& name)
{
    static const std::map<QString, LayerProp> props{
        {QStringLiteral("opacity"), {&Layer::opacity, 0, 1}},
        {QStringLiteral("hue"), {&Layer::hue, -180, 180}},
        {QStringLiteral("saturation"), {&Layer::saturation, 0, 10}},
        {QStringLiteral("lightness"), {&Layer::lightness, 0, 10}},
        {QStringLiteral("contrast"), {&Layer::contrast, 0, 4}},
        {QStringLiteral("blur"), {&Layer::blur, 0, 3}},
        {QStringLiteral("sharpen"), {&Layer::sharpen, 0, 2}},
        {QStringLiteral("glow"), {&Layer::glow, 0, 1}},
        {QStringLiteral("glowSize"), {&Layer::glowSize, 0.5, 4}},
    };
    const auto it = props.find(name);
    return it == props.end() ? nullptr : &it->second;
}

/** One entry of a choice option's "set": a layer's visibility, setting or effect parameter. */
bool applySet(Project* project, const QJsonObject& s)
{
    Layer* l = project->layer(s.value(QStringLiteral("layer")).toInt(-1));
    if (!l)
        return false;
    const QJsonValue value = s.value(QStringLiteral("value"));
    if (s.contains(QStringLiteral("visible"))) {
        l->visible = s.value(QStringLiteral("visible")).toBool();
    } else if (const QString prop = s.value(QStringLiteral("prop")).toString(); prop == QLatin1String("tint")) {
        l->tint = QColor(value.toString());
    } else if (const LayerProp* p = layerProp(prop)) {
        l->*(p->field) = std::clamp(value.toDouble(), p->min, p->max);
    } else {
        ParamDesc storage;
        const QString param = s.value(QStringLiteral("param")).toString();
        const ParamDesc* d = findParam(l->effect.get(), param, storage);
        if (!d)
            return false;
        l->effect->setValue(param, paramValue(*d, value));
    }
    return true;
}

bool matchesSet(Project* project, const QJsonObject& s)
{
    const Layer* l = project->layer(s.value(QStringLiteral("layer")).toInt(-1));
    if (!l)
        return false;
    const QJsonValue value = s.value(QStringLiteral("value"));
    if (s.contains(QStringLiteral("visible")))
        return l->visible == s.value(QStringLiteral("visible")).toBool();
    const QString prop = s.value(QStringLiteral("prop")).toString();
    if (prop == QLatin1String("tint"))
        return l->tint.rgb() == QColor(value.toString()).rgb();
    if (const LayerProp* p = layerProp(prop))
        return std::abs(l->*(p->field) - value.toDouble()) < 1e-6;
    ParamDesc storage;
    const QString param = s.value(QStringLiteral("param")).toString();
    const ParamDesc* d = findParam(l->effect.get(), param, storage);
    if (!d)
        return false;
    const QVariant current = l->effect->value(param);
    const QVariant wanted = paramValue(*d, value);
    if (d->type == ParamDesc::Type::Color)
        return current.value<QColor>().rgb() == wanted.value<QColor>().rgb();
    if (d->type == ParamDesc::Type::Bool)
        return current.toBool() == wanted.toBool();
    return std::abs(current.toDouble() - wanted.toDouble()) < 1e-6;
}

} // namespace

PresetControls::PresetControls(Project* project, QWidget* parent) : QWidget(parent), m_project(project)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 6);
    v->setSpacing(4);
    m_title = new QLabel(this);
    m_title->setObjectName("SectionTitle");
    v->addWidget(m_title);
    m_grid = new QGridLayout();
    m_grid->setHorizontalSpacing(10);
    m_grid->setVerticalSpacing(6);
    m_grid->setColumnStretch(1, 1);
    v->addLayout(m_grid);

    connect(m_project, &Project::layersChanged, this, &PresetControls::rebuild);
    connect(m_project, &Project::layerChanged, this, &PresetControls::sync);
    connect(m_project, &Project::settingsChanged, this, &PresetControls::sync);
    rebuild();
}

void PresetControls::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        rebuild();
    QWidget::changeEvent(e);
}

void PresetControls::rebuild()
{
    for (QWidget* w : m_rowWidgets) {
        m_grid->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    m_rowWidgets.clear();
    m_syncers.clear();

    const QJsonObject preset = m_project->scene().preset;
    const QJsonArray controls = preset.value(QStringLiteral("controls")).toArray();
    setVisible(!controls.isEmpty());
    if (controls.isEmpty())
        return;
    const QString title = presetTitle(preset);
    m_title->setText((title.isEmpty() ? tr("Preset") : tr("Preset · %1").arg(title)).toUpper());

    int row = 0;
    for (int controlIndex = 0; controlIndex < controls.size(); ++controlIndex) {
        const QJsonObject c = controls[controlIndex].toObject();
        const QString type = c.value(QStringLiteral("type")).toString();
        const std::vector<Target> targets = targetsOf(c);
        if (targets.empty() && type != QLatin1String("choice"))
            continue;
        auto* label = new QLabel(localizedText(c.value(QStringLiteral("label"))), this);
        label->setObjectName("Dim");
        QWidget* editor = nullptr;
        Project* project = m_project;

        // Layer targets that still exist; a preset's layers can be deleted or reordered after it is opened.
        auto layers = [project, targets] {
            std::vector<std::pair<int, Layer*>> out;
            for (const Target& t : targets)
                if (Layer* l = project->layer(t.layer))
                    out.emplace_back(t.layer, l);
            return out;
        };

        if (type == QLatin1String("hue")) {
            const Lab base = baseColor(c, targets.front().layer,
                                       preset.value(QStringLiteral("cover")).toDouble(0.25));
            const double baseHue = std::atan2(base.b, base.a);
            const double baseChroma = std::max(0.02, double(std::hypot(base.a, base.b)));
            const double baseL = std::max(0.02, double(base.L));
            auto* box = new QWidget(this);
            auto* h = new QHBoxLayout(box);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(6);
            auto* button = new ColorButton(box);
            auto* reset = new QToolButton(box);
            reset->setObjectName("Flat");
            reset->setIcon(icons::get(icons::Id::Undo));
            reset->setToolTip(tr("Back to the preset's own colours"));
            reset->setCursor(Qt::PointingHandCursor);
            h->addWidget(button, 1);
            h->addWidget(reset);
            connect(button, &ColorButton::editStarted, m_project, &Project::checkpoint);
            connect(button, &ColorButton::colorChanged, this,
                    [this, layers, baseHue, baseChroma, baseL](const QColor& color) {
                if (m_syncing)
                    return;
                // The turn, chroma and lightness that take the preset's base colour exactly to the picked one.
                const Lab picked = color::toOklab(color::fromQColor(color));
                const double chroma = std::hypot(picked.a, picked.b);
                for (auto [index, l] : layers()) {
                    // A grey keeps the turn and only drains the colour.
                    if (chroma >= 0.02) {
                        double turn = (std::atan2(picked.b, picked.a) - baseHue) * 180.0 / 3.14159265358979;
                        turn = std::remainder(turn, 360.0);
                        l->hue = std::clamp(turn, -180.0, 180.0);
                    }
                    l->saturation = std::clamp(chroma / baseChroma, 0.0, 10.0);
                    l->lightness = std::clamp(picked.L / baseL, 0.0, 10.0);
                    m_project->notifyLayerChanged(index);
                }
            });
            connect(reset, &QToolButton::clicked, this, [this, layers] {
                m_project->checkpoint();
                for (auto [index, l] : layers()) {
                    l->hue = 0;
                    l->saturation = 1;
                    l->lightness = 1;
                    m_project->notifyLayerChanged(index);
                }
            });
            m_syncers.push_back([button, reset, layers, baseHue, baseChroma, baseL] {
                const auto ls = layers();
                button->setEnabled(!ls.empty());
                reset->setEnabled(!ls.empty());
                if (ls.empty())
                    return;
                const Layer* l = ls.front().second;
                const double angle = baseHue + l->hue * 3.14159265358979 / 180.0;
                const double chroma = baseChroma * l->saturation;
                const Lab shown{float(baseL * l->lightness), float(chroma * std::cos(angle)),
                                float(chroma * std::sin(angle))};
                button->setColor(color::toQColor(color::fromOklab(shown)));
            });
            editor = box;
        } else if (type == QLatin1String("saturation") || type == QLatin1String("opacity")) {
            auto* slider = new SliderRow(this);
            slider->setRange(0, type == QLatin1String("saturation") ? 200 : 100, 1, QStringLiteral("%"));
            auto get = [type](const Layer* l) {
                return type == QLatin1String("saturation") ? l->saturation * 100.0 : l->opacity * 100.0;
            };
            connect(slider, &SliderRow::editStarted, m_project, &Project::checkpoint);
            connect(slider, &SliderRow::valueChanged, this, [this, layers, type](double v) {
                if (m_syncing)
                    return;
                for (auto [index, l] : layers()) {
                    if (type == QLatin1String("saturation"))
                        l->saturation = v / 100.0;
                    else
                        l->opacity = v / 100.0;
                    m_project->notifyLayerChanged(index);
                }
            });
            m_syncers.push_back([slider, layers, get] {
                const auto ls = layers();
                slider->setEnabled(!ls.empty());
                if (!ls.empty())
                    slider->setValue(get(ls.front().second));
            });
            editor = slider;
        } else if (type == QLatin1String("toggle")) {
            auto* toggle = new ToggleSwitch(this);
            connect(toggle, &ToggleSwitch::clicked, this, [this, layers](bool on) {
                m_project->checkpoint();
                for (auto [index, l] : layers()) {
                    l->visible = on;
                    m_project->notifyLayerChanged(index);
                }
            });
            m_syncers.push_back([toggle, layers] {
                const auto ls = layers();
                toggle->setEnabled(!ls.empty());
                if (!ls.empty()) {
                    const QSignalBlocker block(toggle);
                    toggle->setChecked(ls.front().second->visible);
                }
            });
            editor = toggle;
        } else if (type == QLatin1String("color")) {
            // A swap target: the layer's swaps[index].to, with a button back to the colour as drawn.
            auto swapOf = [project](const Target& t) -> ColorSwap* {
                Layer* l = project->layer(t.layer);
                return l && t.swap >= 0 && t.swap < int(l->swaps.size()) ? &l->swaps[size_t(t.swap)] : nullptr;
            };
            const bool swaps = targets.front().swap >= 0;
            auto* box = new QWidget(this);
            auto* h = new QHBoxLayout(box);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(6);
            auto* button = new ColorButton(box);
            h->addWidget(button, 1);
            QToolButton* reset = nullptr;
            if (swaps) {
                reset = new QToolButton(box);
                reset->setObjectName("Flat");
                reset->setIcon(icons::get(icons::Id::Undo));
                reset->setToolTip(tr("Back to the preset's own colour"));
                reset->setCursor(Qt::PointingHandCursor);
                h->addWidget(reset);
                connect(reset, &QToolButton::clicked, this, [this, targets, swapOf] {
                    m_project->checkpoint();
                    for (const Target& t : targets)
                        if (ColorSwap* s = swapOf(t)) {
                            s->to = s->from;
                            m_project->notifyLayerChanged(t.layer);
                        }
                });
            }
            connect(button, &ColorButton::editStarted, m_project, &Project::checkpoint);
            connect(button, &ColorButton::colorChanged, this, [this, project, targets, swapOf](const QColor& color) {
                if (m_syncing)
                    return;
                for (const Target& t : targets) {
                    Layer* l = project->layer(t.layer);
                    if (!l)
                        continue;
                    if (ColorSwap* s = swapOf(t))
                        s->to = color;
                    else if (t.prop == QLatin1String("tint"))
                        l->tint = color;
                    else if (l->effect && t.swap < 0)
                        l->effect->setValue(t.param, color);
                    m_project->notifyLayerChanged(t.layer);
                }
            });
            m_syncers.push_back([button, reset, project, targets, swapOf] {
                const Target& t = targets.front();
                Layer* l = project->layer(t.layer);
                const ColorSwap* s = swapOf(t);
                const bool tint = t.prop == QLatin1String("tint");
                const bool ok = s || (l && (tint || (l->effect && t.swap < 0)));
                button->setEnabled(ok);
                if (reset)
                    reset->setEnabled(s && s->to.rgb() != s->from.rgb());
                if (s)
                    button->setColor(s->to);
                else if (l && tint)
                    button->setColor(l->tint);
                else if (l && l->effect && t.swap < 0)
                    button->setColor(l->effect->value(t.param).value<QColor>());
            });
            editor = box;
        } else if (type == QLatin1String("number")) {
            const double lo = c.value(QStringLiteral("min")).toDouble(0);
            const double hi = std::max(lo + 1e-6, c.value(QStringLiteral("max")).toDouble(100));
            auto* slider = new SliderRow(this);
            slider->setRange(lo, hi, c.value(QStringLiteral("step")).toDouble(1),
                             c.value(QStringLiteral("suffix")).toString());
            auto toTarget = [lo, hi](const Target& t, double v) {
                return t.mapped ? t.from + (v - lo) / (hi - lo) * (t.to - t.from) : v;
            };
            auto fromTarget = [lo, hi](const Target& t, double x) {
                return t.mapped && t.to != t.from ? lo + (x - t.from) / (t.to - t.from) * (hi - lo) : x;
            };
            connect(slider, &SliderRow::editStarted, m_project, &Project::checkpoint);
            connect(slider, &SliderRow::valueChanged, this, [this, project, targets, toTarget](double v) {
                if (m_syncing)
                    return;
                bool sceneChanged = false;
                for (const Target& t : targets) {
                    const double x = toTarget(t, v);
                    if (t.layer < 0) {
                        if (t.param == QLatin1String("loopSeconds")) {
                            project->scene().loopSeconds = std::clamp(x, 0.2, 60.0);
                            sceneChanged = true;
                        }
                        continue;
                    }
                    Layer* l = project->layer(t.layer);
                    if (const LayerProp* p = layerProp(t.prop); l && p) {
                        l->*(p->field) = std::clamp(x, p->min, p->max);
                        m_project->notifyLayerChanged(t.layer);
                        continue;
                    }
                    ParamDesc storage;
                    const ParamDesc* d = l ? findParam(l->effect.get(), t.param, storage) : nullptr;
                    if (!d)
                        continue;
                    const double clamped = std::clamp(x, d->min, d->max);
                    l->effect->setValue(t.param, d->type == ParamDesc::Type::Int ? QVariant(int(std::lround(clamped)))
                                                                                  : QVariant(clamped));
                    m_project->notifyLayerChanged(t.layer);
                }
                if (sceneChanged)
                    m_project->notifySettingsChanged();
            });
            m_syncers.push_back([slider, project, targets, fromTarget] {
                const Target& t = targets.front();
                double x = 0;
                bool ok = false;
                const LayerProp* p = layerProp(t.prop);
                if (t.layer < 0 && t.param == QLatin1String("loopSeconds")) {
                    x = project->scene().loopSeconds;
                    ok = true;
                } else if (Layer* l = project->layer(t.layer); l && p) {
                    x = l->*(p->field);
                    ok = true;
                } else if (l && l->effect) {
                    x = l->effect->value(t.param).toDouble(&ok);
                }
                slider->setEnabled(ok);
                if (ok)
                    slider->setValue(fromTarget(t, x));
            });
            editor = slider;
        } else if (type == QLatin1String("choice")) {
            const QJsonArray options = c.value(QStringLiteral("options")).toArray();
            auto* combo = new QComboBox(this);
            for (const QJsonValue& ov : options)
                combo->addItem(localizedText(ov.toObject().value(QStringLiteral("label"))));
            connect(combo, &QComboBox::activated, this, [this, project, options](int index) {
                if (index < 0 || index >= options.size())
                    return;
                m_project->checkpoint();
                std::set<int> changed;
                for (const QJsonValue& sv : options[index].toObject().value(QStringLiteral("set")).toArray())
                    if (applySet(project, sv.toObject()))
                        changed.insert(sv.toObject().value(QStringLiteral("layer")).toInt());
                for (int layer : changed)
                    m_project->notifyLayerChanged(layer);
            });
            // The option whose settings all hold; none after the user changes one of them by hand.
            m_syncers.push_back([combo, project, options] {
                int current = -1;
                for (int i = 0; i < options.size() && current < 0; ++i) {
                    const QJsonArray set = options[i].toObject().value(QStringLiteral("set")).toArray();
                    bool all = !set.isEmpty();
                    for (const QJsonValue& sv : set)
                        all = all && matchesSet(project, sv.toObject());
                    if (all)
                        current = i;
                }
                const QSignalBlocker block(combo);
                combo->setCurrentIndex(current);
            });
            editor = combo;
        }
        if (!editor) {
            delete label;
            continue;
        }
        label->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(label, &QWidget::customContextMenuRequested, this, [this, label, controlIndex](const QPoint& p) {
            showControlMenu(controlIndex, label->mapToGlobal(p));
        });
        m_grid->addWidget(label, row, 0);
        m_grid->addWidget(editor, row, 1);
        m_rowWidgets.push_back(label);
        m_rowWidgets.push_back(editor);
        ++row;
    }
    sync();
}

Lab PresetControls::baseColor(const QJsonObject& control, int layer, double phase) const
{
    const QColor named(control.value(QStringLiteral("base")).toString());
    if (named.isValid())
        return color::toOklab(color::fromQColor(named));
    // The layer as drawn: the saturated LEDs' mean hue direction, their mean chroma and lightness.
    const Lab fallback{0.75f, 0.12f, 0.f};
    Layer* l = m_project->layer(layer);
    if (!l || !l->effect)
        return fallback;
    const LedLayout& layout = LedLayout::cd2();
    Frame frame(size_t(layout.count()));
    RenderContext ctx;
    ctx.phase = phase;
    ctx.loopSeconds = m_project->scene().loopSeconds;
    ctx.layout = &layout;
    l->effect->render(ctx, frame);
    double sumA = 0, sumB = 0, sumChroma = 0, sumL = 0;
    int n = 0;
    for (const Rgba& px : frame) {
        if (px.a < 0.5f)
            continue;
        const Lab q = color::toOklab(px.rgb());
        const double chroma = std::hypot(q.a, q.b);
        if (chroma < 0.04)
            continue;
        sumA += q.a;
        sumB += q.b;
        sumChroma += chroma;
        sumL += q.L;
        ++n;
    }
    if (n == 0)
        return fallback;
    const double angle = std::atan2(sumB, sumA);
    const double chroma = sumChroma / n;
    return {float(sumL / n), float(chroma * std::cos(angle)), float(chroma * std::sin(angle))};
}

void PresetControls::showControlMenu(int index, const QPoint& globalPos)
{
    QMenu menu(this);
    QAction* rename = menu.addAction(tr("Rename…"));
    QAction* remove = menu.addAction(tr("Remove from preset settings"));
    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    QJsonObject preset = m_project->scene().preset;
    QJsonArray controls = preset.value(QStringLiteral("controls")).toArray();
    if (index < 0 || index >= controls.size())
        return;
    if (chosen == rename) {
        QJsonObject c = controls[index].toObject();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Preset setting"), tr("Name in the preset settings:"),
                                                   QLineEdit::Normal, localizedText(c.value(QStringLiteral("label"))),
                                                   &ok)
                                 .trimmed();
        if (!ok || name.isEmpty())
            return;
        // Only the UI language's text changes; the others stay as they were.
        QJsonObject label = c.value(QStringLiteral("label")).toObject();
        label.insert(QLocale().name().left(2), name);
        c.insert(QStringLiteral("label"), label);
        controls[index] = c;
    } else if (chosen == remove) {
        controls.removeAt(index);
    }
    m_project->checkpoint();
    preset.insert(QStringLiteral("controls"), controls);
    m_project->scene().preset = preset;
    m_project->notifyLayersChanged();
}

void PresetControls::sync()
{
    if (!isVisible() && m_syncers.empty())
        return;
    m_syncing = true;
    for (const auto& s : m_syncers)
        s();
    m_syncing = false;
}

} // namespace fg
