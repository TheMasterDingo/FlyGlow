#include "ShaderEffect.h"

#include "ShaderRenderer.h"
#include "core/LedLayout.h"
#include "media/LedSampler.h"

#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <array>
#include <map>
#include <tuple>

namespace fg {

namespace {

ParamDesc floatParam(const QString& id, const QString& label, double def, double min, double max, double step,
                     const QString& suffix)
{
    ParamDesc p;
    p.id = id;
    p.label = label;
    p.type = ParamDesc::Type::Float;
    p.def = def;
    p.min = min;
    p.max = max;
    p.step = step;
    p.suffix = suffix;
    return p;
}

const char* kDefaultSource = R"(// iTime: seconds. iPhase: 0..1 through the loop. iLoop: loop length in seconds.
// Animated through iPhase, a shader loops on its own; set the loop crossfade to 0 then.

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = 6.2831853 * iPhase;
    float rings = 0.5 + 0.5 * sin(14.0 * length(uv) - 2.0 * a);
    vec3 col = 0.5 + 0.5 * cos(a + atan(uv.y, uv.x) + vec3(0.0, 2.1, 4.2));
    fragColor = vec4(col * rings, 1.0);
}
)";

media::Framing framingOf(double zoom, double panX, double panY)
{
    media::Framing framing;
    framing.fit = media::FitMode::Stretch;
    framing.zoom = std::max(1.0, zoom);
    framing.panX = -panX / 100.0;
    framing.panY = -panY / 100.0;
    return framing;
}

/**
 * The LED whose hex cell each pixel of the shader frame falls in (top row first), -1 outside the dock. Placed with
 * the same framing the LEDs are sampled with, so a shader that passes iChannel0 through gives the picture back.
 * GUI thread only, like every shader render; cached per layout and framing.
 */
const std::vector<int>& cellMap(const LedLayout& layout, const media::Framing& framing)
{
    static std::map<std::tuple<const LedLayout*, long, long, long>, std::vector<int>> cache;
    auto& map = cache[{&layout, std::lround(framing.zoom * 1000), std::lround(framing.panX * 1000),
                       std::lround(framing.panY * 1000)}];
    if (map.empty()) {
        const QSize size = ShaderRenderer::frameSize();
        const QTransform toLayout = media::layoutToView(layout, size, framing).inverted();
        map.resize(size_t(size.width()) * size_t(size.height()));
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                map[size_t(y) * size_t(size.width()) + size_t(x)] = layout.ledAt(toLayout.map(QPointF(x + 0.5, y + 0.5)),
                                                                                 0.6);
    }
    return map;
}

} // namespace

QString ShaderEffect::defaultSource()
{
    return QString::fromUtf8(kDefaultSource);
}

QString ShaderEffect::name() const
{
    return QT_TRANSLATE_NOOP("Effects", "Shader");
}

QString ShaderEffect::category() const
{
    return QT_TRANSLATE_NOOP("Effects", "Drawing");
}

QList<ParamDesc> ShaderEffect::params() const
{
    ParamDesc input;
    input.id = QStringLiteral("input");
    input.label = QT_TRANSLATE_NOOP("Effects", "Process layers below (iChannel0)");
    input.type = ParamDesc::Type::Bool;
    input.def = false;
    return {
        input,
        floatParam("speed", QT_TRANSLATE_NOOP("Effects", "Speed"), 1.0, 0.05, 4.0, 0.05, QStringLiteral("×")),
        floatParam("seam", QT_TRANSLATE_NOOP("Effects", "Loop crossfade"), 20, 0, 50, 1, QStringLiteral("%")),
        floatParam("brightness", QT_TRANSLATE_NOOP("Effects", "Brightness"), 100, 0, 300, 1, QStringLiteral("%")),
        floatParam("saturation", QT_TRANSLATE_NOOP("Effects", "Saturation"), 100, 0, 200, 1, QStringLiteral("%")),
        floatParam("zoom", QT_TRANSLATE_NOOP("Effects", "Zoom"), 1.0, 1.0, 4.0, 0.05, QStringLiteral("×")),
        floatParam("panX", QT_TRANSLATE_NOOP("Effects", "Shift right"), 0, -50, 50, 1, QStringLiteral("%")),
        floatParam("panY", QT_TRANSLATE_NOOP("Effects", "Shift down"), 0, -50, 50, 1, QStringLiteral("%")),
    };
}

void ShaderEffect::render(const RenderContext& ctx, Frame& out) const
{
    std::fill(out.begin(), out.end(), Rgba(0, 0, 0, 0));
    ShaderRenderer* renderer = ShaderRenderer::instance();
    if (!renderer || QThread::currentThread() != QCoreApplication::instance()->thread())
        return;
    const LedLayout& layout = ctx.layout ? *ctx.layout : LedLayout::cd2();
    const double span = ctx.loopSeconds * std::max(0.0, f("speed"));

    // A filter sees the layers below as one flat hex cell per LED, sRGB-encoded like any Shadertoy texture.
    ShaderInput input;
    const bool filter = isFilter();
    if (filter) {
        std::vector<std::array<float, 3>> cells(size_t(layout.count()), {0.f, 0.f, 0.f});
        if (ctx.below)
            for (size_t i = 0; i < cells.size() && i < ctx.below->size(); ++i) {
                const Rgb& c = (*ctx.below)[i];
                cells[i] = {color::linearToSrgb(std::clamp(c.r, 0.f, 1.f)),
                            color::linearToSrgb(std::clamp(c.g, 0.f, 1.f)),
                            color::linearToSrgb(std::clamp(c.b, 0.f, 1.f))};
            }
        const QSize size = ShaderRenderer::frameSize();
        const std::vector<int>& map = cellMap(layout, framingOf(f("zoom"), f("panX"), f("panY")));
        input.rgba.assign(size_t(size.width()) * size_t(size.height()) * 4, 0.f);
        for (int y = 0; y < size.height(); ++y) {
            // GL rows run bottom-up.
            float* row = input.rgba.data() + size_t(size.height() - 1 - y) * size_t(size.width()) * 4;
            for (int x = 0; x < size.width(); ++x) {
                const int led = map[size_t(y) * size_t(size.width()) + size_t(x)];
                float* px = row + size_t(x) * 4;
                if (led >= 0) {
                    px[0] = cells[size_t(led)][0];
                    px[1] = cells[size_t(led)][1];
                    px[2] = cells[size_t(led)][2];
                }
                px[3] = 1.f;
            }
        }
    }
    const ShaderInput* in = filter ? &input : nullptr;

    std::vector<Rgb> now;
    if (!sample(*renderer, layout, ctx.phase * span, ctx, in, now))
        return;

    // iTime would run on past the loop's end, so the last stretch fades into the moments just before
    // time zero, which lead straight into the first frame.
    const double seam = std::clamp(f("seam") / 100.0, 0.0, 0.5);
    std::vector<Rgb> before;
    if (seam > 0 && ctx.phase > 1.0 - seam
        && sample(*renderer, layout, (ctx.phase - 1.0) * span, ctx, in, before)) {
        const float w = float(fx::smooth(1.0 - seam, 1.0, ctx.phase));
        for (size_t i = 0; i < now.size() && i < before.size(); ++i)
            now[i] = {now[i].r + (before[i].r - now[i].r) * w, now[i].g + (before[i].g - now[i].g) * w,
                      now[i].b + (before[i].b - now[i].b) * w};
    }
    for (size_t i = 0; i < out.size() && i < now.size(); ++i)
        out[i] = Rgba(now[i], 1.f);
}

bool ShaderEffect::sample(ShaderRenderer& renderer, const LedLayout& layout, double time, const RenderContext& ctx,
                          const ShaderInput* input, std::vector<Rgb>& out) const
{
    ShaderTime t;
    t.time = time;
    t.phase = ctx.phase;
    t.loop = ctx.loopSeconds;
    const QImage image = renderer.render(m_source, t, input);
    if (image.isNull())
        return false;
    const media::Framing framing = framingOf(f("zoom"), f("panX"), f("panY"));
    media::Adjustments adjust;
    adjust.brightness = std::max(0.0, f("brightness") / 100.0);
    adjust.saturation = std::max(0.0, f("saturation") / 100.0);
    // A filter's patch stays clear of the cell edges, so iChannel0 passed through gives the layers below back
    // exactly and stacked passes do not blur.
    media::LedSampler(layout, input ? 0.6 : 1.0).sample(image, framing, adjust, out);
    return true;
}

std::unique_ptr<Effect> ShaderEffect::clone() const
{
    auto copy = std::make_unique<ShaderEffect>();
    copy->m_values = m_values;
    copy->m_source = m_source;
    return copy;
}

QJsonObject ShaderEffect::toJson() const
{
    QJsonObject o = Effect::toJson();
    o.insert("source", m_source);
    return o;
}

void ShaderEffect::fromJson(const QJsonObject& o)
{
    Effect::fromJson(o);
    m_source = o.value("source").toString(defaultSource());
}

} // namespace fg
