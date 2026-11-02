#include "Thumbnail.h"

#include "core/Effect.h"
#include "core/LedLayout.h"
#include "core/Scene.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace fg {

QPixmap renderThumbnail(const std::vector<Rgb>& linear, const QSize& size, qreal dpr)
{
    const LedLayout& L = LedLayout::cd2();
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF ob = L.outline().boundingRect();
    const QRectF area = QRectF(QPointF(0, 0), QSizeF(size)).adjusted(2, 2, -2, -2);
    const double s = std::min(area.width() / ob.width(), area.height() / ob.height());
    QTransform xf;
    xf.translate(area.center().x(), area.center().y());
    xf.scale(s, s);
    xf.translate(-ob.center().x(), -ob.center().y());

    p.setPen(QPen(QColor(0x34, 0x39, 0x46), 1));
    p.setBrush(QColor(0x14, 0x16, 0x1c));
    p.drawPath(xf.map(L.outline()));

    p.setPen(Qt::NoPen);
    const double r = std::max(0.9, s * 0.42);
    for (const Led& led : L.leds()) {
        const Rgb c = size_t(led.index) < linear.size() ? linear[size_t(led.index)] : Rgb{};
        p.setBrush(color::toQColor(c));
        p.drawEllipse(xf.map(led.pos), r, r);
    }
    return pm;
}

QPixmap renderEffectThumbnail(const Effect& effect, double phase, const QSize& size, qreal dpr, const Layer* look)
{
    const LedLayout& L = LedLayout::cd2();
    Frame frame(size_t(L.count()));
    RenderContext ctx;
    ctx.phase = effect.showcasePhase(phase);
    ctx.layout = &L;
    // A filter has no picture of its own, so it shows what it does to a rainbow.
    std::vector<Rgb> sample;
    if (effect.isFilter()) {
        for (const Led& led : L.leds())
            sample.push_back(color::rainbow(float(led.uv.x() * 0.8 + led.uv.y() * 0.2)));
        ctx.below = &sample;
    }
    effect.render(ctx, frame);
    if (look)
        applyLayerLook(*look, frame, L);
    std::vector<Rgb> out(frame.size());
    for (size_t i = 0; i < frame.size(); ++i) {
        // Overlays are shown over black.
        const float a = std::clamp(frame[i].a, 0.f, 1.f);
        out[i] = {frame[i].r * a, frame[i].g * a, frame[i].b * a};
    }
    return renderThumbnail(out, size, dpr);
}

} // namespace fg
