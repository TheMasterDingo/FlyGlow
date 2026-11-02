// Loads a .flyglow exactly like Project::load, checks its "preset" block (docs/presets.md) and renders it at
// several loop phases into one contact sheet, showing each LED as the dock would light it: encoded with the
// scene's output gamma, values <= 8 dark, light ~ (v/255)^1.4.
#include "core/Effect.h"
#include "core/KeyframeEffect.h"
#include "core/LedLayout.h"
#include "core/MediaEffect.h"
#include "core/Scene.h"
#include "shader/ShaderEffect.h"
#include "shader/ShaderRenderer.h"

#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <cmath>
#include <cstdio>

using namespace fg;

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fputs("usage: scene_check <scene.flyglow> <out.png> [frames=12] [cols=4] [px=12] [phase0=0] [phase1=1]\n",
                   stderr);
        return 2;
    }
    registerBuiltinEffects();
    EffectRegistry::instance().add<KeyframeEffect>();
    EffectRegistry::instance().add<MediaEffect>();
    EffectRegistry::instance().add<ShaderEffect>();
    ShaderRenderer shaderRenderer;

    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        std::fprintf(stderr, "JSON error: %s at %d\n", qPrintable(pe.errorString()), pe.offset);
        return 1;
    }
    Scene scene;
    QString error;
    if (!scene.fromJson(doc.object(), &error)) {
        std::fprintf(stderr, "scene error: %s\n", qPrintable(error));
        return 1;
    }
    // SCENE_CHECK_TRIM=from:to trims the loop like the timeline's "Trim the loop to the selection" first.
    if (const QStringList trim = qEnvironmentVariable("SCENE_CHECK_TRIM").split(':'); trim.size() == 2)
        scene.trim(trim[0].toDouble(), trim[1].toDouble());
    const QJsonArray jl = doc.object().value("layers").toArray();
    std::printf("layers in file %d, loaded %d%s\n", int(jl.size()), int(scene.layers.size()),
                jl.size() != qsizetype(scene.layers.size()) ? "  <-- SKIPPED LAYERS" : "");
    for (const auto& l : scene.layers) {
        std::printf("  %-22s %-12s op %.2f blend %d mask %s", qPrintable(l->name), qPrintable(l->effect->typeId()),
                    l->opacity, int(l->blend), l->mask.empty() ? "no" : "yes");
        if (l->recolors())
            std::printf(" hue %.0f sat %.2f light %.2f", l->hue, l->saturation, l->lightness);
        if (l->contrast != 1.0)
            std::printf(" contrast %.2f", l->contrast);
        if (l->tints())
            std::printf(" tint %s", qPrintable(l->tint.name()));
        for (const ColorSwap& s : l->swaps)
            std::printf(" swap %s>%s", qPrintable(s.from.name()), qPrintable(s.to.name()));
        if (l->sharpen > 0)
            std::printf(" sharpen %.2f", l->sharpen);
        if (l->blur > 0)
            std::printf(" blur %.1f", l->blur);
        if (l->glow > 0)
            std::printf(" glow %.2f/%.1f", l->glow, l->glowSize);
        if (auto* k = dynamic_cast<KeyframeEffect*>(l->effect.get()))
            std::printf(" keys %d", k->keyCount());
        if (auto* s = dynamic_cast<ShaderEffect*>(l->effect.get())) {
            const ShaderCompileResult r = shaderRenderer.compile(s->source());
            std::printf(" shader %s seam %.0f", r.ok ? "ok" : "FAILED", s->value("seam").toDouble());
            for (const ShaderIssue& is : r.issues)
                std::printf("\n      line %d: %s", is.line, qPrintable(is.text));
        }
        std::printf("\n");
    }
    {
        const QJsonObject preset = scene.preset;
        const QJsonObject title = preset.value("title").toObject();
        if (preset.isEmpty())
            std::printf("preset: none  <-- PRESET MISSING\n");
        else
            std::printf("preset: \"%s\" / \"%s\"%s%s\n", qUtf8Printable(title.value("en").toString()),
                        qUtf8Printable(title.value("ru").toString()),
                        title.value("en").toString().isEmpty() || title.value("ru").toString().isEmpty()
                            ? "  <-- TITLE MISSING"
                            : "",
                        preset.contains("description") ? "" : "  (no description)");
        const QStringList types = {"hue", "saturation", "opacity", "toggle", "color", "number", "choice"};
        const QStringList numericProps = {"opacity", "hue",  "saturation", "lightness", "contrast",
                                          "sharpen", "blur", "glow",       "glowSize"};
        // kind: "color" or "number" for what the target must hold, "any" for a choice option's value.
        auto checkTarget = [&](const QJsonObject& t, const QString& kind, QStringList& problems) {
            const int li = t.value("layer").toInt(-1);
            if (li < 0 || li >= int(scene.layers.size())) {
                problems << QString("layer %1 out of range").arg(li);
                return;
            }
            if (t.contains("visible"))
                return;
            if (t.contains("swap")) {
                const int k = t.value("swap").toInt(-1);
                if (kind != "color")
                    problems << "a swap target needs a color control";
                else if (k < 0 || k >= int(scene.layers[size_t(li)]->swaps.size()))
                    problems << QString("layer %1 has no swap %2").arg(li).arg(k);
                return;
            }
            if (t.contains("prop")) {
                const QString prop = t.value("prop").toString();
                const bool color = prop == "tint", number = numericProps.contains(prop);
                if (!color && !number)
                    problems << QString("unknown layer prop \"%1\"").arg(prop);
                else if ((kind == "color" && !color) || (kind == "number" && !number))
                    problems << QString("layer prop %1 does not fit a %2 control").arg(prop, kind);
                return;
            }
            const QString param = t.value("param").toString();
            const Effect* e = scene.layers[size_t(li)]->effect.get();
            for (const ParamDesc& d : e->params())
                if (d.id == param) {
                    const bool numeric = d.type == ParamDesc::Type::Float || d.type == ParamDesc::Type::Int
                                      || d.type == ParamDesc::Type::Angle;
                    if (kind == "color" && d.type != ParamDesc::Type::Color)
                        problems << QString("layer %1 param %2 is not a colour").arg(li).arg(param);
                    if (kind == "number" && !numeric)
                        problems << QString("layer %1 param %2 is not a number").arg(li).arg(param);
                    return;
                }
            problems << QString("layer %1 (%2) has no param \"%3\"").arg(li).arg(e->typeId(), param);
        };
        for (const QJsonValue& cv : preset.value("controls").toArray()) {
            const QJsonObject c = cv.toObject();
            const QString type = c.value("type").toString();
            QStringList problems;
            if (!types.contains(type))
                problems << "unknown type";
            const QJsonObject label = c.value("label").toObject();
            if (label.value("en").toString().isEmpty() || label.value("ru").toString().isEmpty())
                problems << "label needs en and ru";
            const QJsonArray targets = c.value("targets").toArray();
            if (type == "choice") {
                const QJsonArray options = c.value("options").toArray();
                if (options.size() < 2)
                    problems << "a choice needs at least two options";
                for (const QJsonValue& ov : options) {
                    const QJsonObject o = ov.toObject();
                    const QJsonObject ol = o.value("label").toObject();
                    if (ol.value("en").toString().isEmpty() || ol.value("ru").toString().isEmpty())
                        problems << "option label needs en and ru";
                    if (o.value("set").toArray().isEmpty())
                        problems << "option sets nothing";
                    for (const QJsonValue& sv : o.value("set").toArray()) {
                        const QJsonObject s = sv.toObject();
                        if (!s.contains("visible") && !s.contains("value"))
                            problems << "set entry needs visible or value";
                        checkTarget(s, "any", problems);
                    }
                }
            } else if (targets.isEmpty()) {
                problems << "no targets";
            }
            for (const QJsonValue& tv : targets) {
                const QJsonObject t = tv.toObject();
                if (t.contains("scene")) {
                    if (type != "number" || t.value("scene").toString() != "loopSeconds")
                        problems << "scene target must be loopSeconds on a number control";
                    continue;
                }
                if (type == "color" || type == "number")
                    checkTarget(t, type, problems);
                else
                    checkTarget(QJsonObject{{"layer", t.value("layer")}, {"visible", true}}, type, problems);
            }
            std::printf("  control %-10s %-24s %s\n", qPrintable(type), qUtf8Printable(label.value("en").toString()),
                        problems.isEmpty() ? "ok" : qUtf8Printable("<-- CONTROL ERROR: " + problems.join("; ")));
        }
    }
    int bakeFrames = 0, period = 0;
    bakeTiming(scene, 200, &bakeFrames, &period);
    std::printf("loop %.2fs gamma %.2f lift %.2f brightness %d animated %d -> bake %d frames x %d ms\n",
                scene.loopSeconds, scene.outputGamma, scene.darkLift, scene.brightness, int(scene.isAnimated()),
                bakeFrames, period * 20);

    {
        // Seam check on the frames bake() would send: the wrap step (last -> first) against the largest
        // step inside the loop, in 8-bit output values.
        const BakeResult baked = bake(scene, LedLayout::cd2(), 200);
        // SCENE_CHECK_DUMP=file writes the baked output values: frames x LEDs x RGB bytes.
        if (const QString dump = qEnvironmentVariable("SCENE_CHECK_DUMP"); !dump.isEmpty()) {
            QFile f(dump);
            if (f.open(QIODevice::WriteOnly))
                f.write(baked.rgb);
        }
        const int n = LedLayout::cd2().count() * 3;
        auto step = [&](int a, int b) {
            int worst = 0;
            for (int i = 0; i < n; ++i)
                worst = std::max(worst, std::abs(int(uchar(baked.rgb[a * n + i])) - int(uchar(baked.rgb[b * n + i]))));
            return worst;
        };
        int inner = 0;
        double mean = 0;
        for (int k = 0; k + 1 < baked.frameCount; ++k) {
            const int s = step(k, k + 1);
            inner = std::max(inner, s);
            mean += s;
        }
        if (baked.frameCount > 1) {
            mean /= baked.frameCount - 1;
            const int wrap = step(baked.frameCount - 1, 0);
            std::printf("steps: max %d mean %.1f, wrap %d%s\n", inner, mean, wrap,
                        wrap > std::max(24, int(mean * 3 + 8)) && wrap > inner ? "  <-- SEAM?" : "");
        }
        // LEDs the scene lights but the dock leaves dark (brightest channel 1..kDockDarkValue) for most of the
        // loop: a background or gradient that fades out before it reaches them.
        const int leds = LedLayout::cd2().count();
        int lost = 0;
        for (int led = 0; led < leds; ++led) {
            int dark = 0;
            for (int k = 0; k < baked.frameCount; ++k) {
                const uchar* c = reinterpret_cast<const uchar*>(baked.rgb.constData()) + (k * leds + led) * 3;
                const int v = std::max({int(c[0]), int(c[1]), int(c[2])});
                dark += v > 0 && v <= kDockDarkValue;
            }
            lost += dark * 2 >= baked.frameCount;
        }
        std::printf("lost: %d LEDs lit too dim for the dock most of the loop%s\n", lost,
                    lost >= 8 ? "  <-- TOO DARK" : "");
    }
    const int frames = argc > 3 ? std::max(1, atoi(argv[3])) : 12;
    const int cols = argc > 4 ? std::max(1, atoi(argv[4])) : 4;
    const double px = argc > 5 ? atof(argv[5]) : 12.0;
    const double ph0 = argc > 6 ? atof(argv[6]) : 0.0;
    const double ph1 = argc > 7 ? atof(argv[7]) : 1.0;
    const LedLayout& L = LedLayout::cd2();
    const QRectF box = L.bounds().adjusted(-0.9, -0.9, 0.9, 0.9);
    const QSize cell = (box.size() * px).toSize() + QSize(0, 14);
    const int rows = (frames + cols - 1) / cols;
    QImage sheet(cell.width() * cols, cell.height() * rows, QImage::Format_RGB32);
    sheet.fill(QColor(0x30, 0x30, 0x34));
    QPainter p(&sheet);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    std::vector<Rgb> out;
    std::vector<uchar> bytes(size_t(L.count()) * 3);
    for (int k = 0; k < frames; ++k) {
        const double phase = ph0 + (ph1 - ph0) * k / frames;
        scene.render(phase, L, out);
        encodeFrame(out, scene.outputGamma, bytes.data(), scene.darkLift);
        const QPointF origin((k % cols) * cell.width(), (k / cols) * cell.height());
        p.fillRect(QRectF(origin, QSizeF(cell.width() - 2, cell.height() - 2)), QColor(0x0c, 0x0c, 0x10));
        int lit = 0;
        for (const Led& led : L.leds()) {
            float rgb[3];
            bool on = false;
            for (int c = 0; c < 3; ++c) {
                const int v = bytes[size_t(led.index) * 3 + c];
                const float light = v <= kDockDarkValue ? 0.f : float(std::pow(v / 255.0, kDockGamma));
                rgb[c] = color::linearToSrgb(light);
                on = on || v > kDockDarkValue;
            }
            lit += on;
            p.setPen(QPen(QColor(0x26, 0x26, 0x2c), 1));
            p.setBrush(QColor::fromRgbF(rgb[0], rgb[1], rgb[2]));
            p.drawEllipse(origin + (led.pos - box.topLeft()) * px, px * 0.44, px * 0.44);
        }
        p.setPen(QColor(200, 200, 200));
        p.drawText(QRectF(origin + QPointF(4, cell.height() - 15), QSizeF(cell.width(), 14)),
                   QString("%1  lit %2").arg(phase, 0, 'f', 3).arg(lit));
    }
    p.end();
    sheet.save(QString::fromLocal8Bit(argv[2]));
    return 0;
}
