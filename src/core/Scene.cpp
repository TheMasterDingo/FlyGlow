#include "Scene.h"

#include "KeyframeEffect.h"
#include "LedLayout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace fg {

QStringList blendModeNames()
{
    return {QT_TRANSLATE_NOOP("Effects", "Normal"), QT_TRANSLATE_NOOP("Effects", "Add"),
            QT_TRANSLATE_NOOP("Effects", "Screen"), QT_TRANSLATE_NOOP("Effects", "Multiply"),
            QT_TRANSLATE_NOOP("Effects", "Lighten")};
}

std::unique_ptr<Layer> Layer::clone() const
{
    auto l = std::make_unique<Layer>();
    l->name = name;
    l->visible = visible;
    l->opacity = opacity;
    l->blend = blend;
    l->effect = effect ? effect->clone() : nullptr;
    l->mask = mask;
    l->swaps = swaps;
    l->hue = hue;
    l->saturation = saturation;
    l->lightness = lightness;
    l->contrast = contrast;
    l->tint = tint;
    l->blur = blur;
    l->glow = glow;
    l->glowSize = glowSize;
    l->sharpen = sharpen;
    l->timeOffset = timeOffset;
    l->timeScale = timeScale;
    return l;
}

void recolor(Frame& frame, double hueDegrees, double saturation, double lightness)
{
    const double rad = hueDegrees * 3.14159265358979 / 180.0;
    const float c = float(std::cos(rad)), s = float(std::sin(rad)), k = float(std::max(0.0, saturation));
    const float kl = float(std::max(0.0, lightness));
    for (Rgba& px : frame) {
        if (px.a <= 0.f)
            continue;
        Lab lab = color::toOklab(px.rgb());
        const float a = lab.a * c - lab.b * s;
        const float b = lab.a * s + lab.b * c;
        lab.L *= kl;
        lab.a = a * k;
        lab.b = b * k;
        const Rgb out = color::fromOklab(lab);
        px.r = std::max(0.f, out.r);
        px.g = std::max(0.f, out.g);
        px.b = std::max(0.f, out.b);
    }
}

void contrastFrame(Frame& frame, double amount)
{
    if (amount == 1.0)
        return;
    const float k = float(std::max(0.0, amount));
    for (Rgba& px : frame) {
        if (px.a <= 0.f)
            continue;
        Lab lab = color::toOklab(px.rgb());
        if (lab.L <= 1e-4f)
            continue;
        const float L = std::clamp(0.5f + (lab.L - 0.5f) * k, 0.f, 1.f);
        const float ratio = L / lab.L;
        lab = {L, lab.a * ratio, lab.b * ratio};
        const Rgb out = color::fromOklab(lab);
        px.r = std::max(0.f, out.r);
        px.g = std::max(0.f, out.g);
        px.b = std::max(0.f, out.b);
    }
}

namespace {

float smooth(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

} // namespace

void swapColors(Frame& frame, const std::vector<ColorSwap>& swaps)
{
    constexpr float kPi = 3.14159265358979f;
    struct Prepared {
        Lab from, to;
        float fromC, fromH, toC, toH, range;
        bool neutral;
    };
    std::vector<Prepared> active;
    for (const ColorSwap& s : swaps) {
        if (!s.from.isValid() || !s.to.isValid() || s.from.rgb() == s.to.rgb())
            continue;
        Prepared p;
        p.from = color::toOklab(color::fromQColor(s.from));
        p.to = color::toOklab(color::fromQColor(s.to));
        // Black has no shading to carry over.
        if (p.from.L < 0.02f)
            continue;
        p.fromC = std::hypot(p.from.a, p.from.b);
        p.fromH = std::atan2(p.from.b, p.from.a);
        p.toC = std::hypot(p.to.a, p.to.b);
        p.toH = std::atan2(p.to.b, p.to.a);
        p.neutral = p.fromC < 0.03f;
        p.range = float(std::clamp(s.range, 1.0, 180.0)) * kPi / 180.f;
        active.push_back(p);
    }
    if (active.empty())
        return;
    for (Rgba& px : frame) {
        if (px.a <= 0.f)
            continue;
        const Lab lab = color::toOklab(px.rgb());
        const float c = std::hypot(lab.a, lab.b), h = std::atan2(lab.b, lab.a);
        float sum = 0.f, dL = 0.f, da = 0.f, db = 0.f;
        for (const Prepared& p : active) {
            float w = 0.f;
            Lab t;
            if (p.neutral) {
                // A white or grey source picks the pixels without colour (not black) and colours them in.
                w = (1.f - smooth(0.03f, 0.07f, c)) * smooth(0.02f, 0.08f, lab.L);
                const float k = lab.L / p.from.L;
                t = {p.to.L * k, p.to.a * k, p.to.b * k};
            } else {
                const float dh = std::abs(std::remainder(h - p.fromH, 2.f * kPi));
                w = (1.f - smooth(p.range * 0.5f, p.range, dh)) * smooth(0.015f, 0.04f, c);
                const float c2 = c * p.toC / p.fromC;
                const float h2 = h + (p.toH - p.fromH);
                t = {lab.L * p.to.L / p.from.L, c2 * std::cos(h2), c2 * std::sin(h2)};
            }
            if (w <= 0.f)
                continue;
            sum += w;
            dL += w * (t.L - lab.L);
            da += w * (t.a - lab.a);
            db += w * (t.b - lab.b);
        }
        if (sum <= 0.f)
            continue;
        // Overlapping swaps share the pixel instead of adding up.
        const float norm = sum > 1.f ? 1.f / sum : 1.f;
        const Rgb out = color::fromOklab({lab.L + dL * norm, lab.a + da * norm, lab.b + db * norm});
        px.r = std::max(0.f, out.r);
        px.g = std::max(0.f, out.g);
        px.b = std::max(0.f, out.b);
    }
}

void tintFrame(Frame& frame, const QColor& tint)
{
    const Rgb t = color::fromQColor(tint);
    for (Rgba& px : frame) {
        const float v = std::max({px.r, px.g, px.b});
        px.r = t.r * v;
        px.g = t.g * v;
        px.b = t.b * v;
    }
}

namespace {

/** Linear light against perceived brightness: a glow that looks 40% bright is about 13% of the light. */
constexpr double kPerceptual = 2.2;
/** Linear light of output value kDockDarkValue + 1 at the dock's gamma, rounded up: the faintest the dock shows. */
constexpr float kGlowFloor = 0.0095f;

/**
 * exp(-d² / 2r²) between every pair of LEDs, n * n, zero past 3r, raised to power. Cached per layout, radius
 * (to 0.01 LED) and power: presets use a handful of radii and bake every frame with the same one.
 */
std::shared_ptr<const std::vector<float>> falloff(const LedLayout& layout, double radius, double power = 1.0)
{
    static std::mutex mutex;
    static std::map<std::tuple<const LedLayout*, int, int>, std::shared_ptr<const std::vector<float>>> cache;
    const int key = std::max(1, int(std::lround(radius * 100.0)));
    const std::lock_guard lock(mutex);
    auto& slot = cache[{&layout, key, int(std::lround(power * 100.0))}];
    if (!slot) {
        const int n = layout.count();
        auto w = std::make_shared<std::vector<float>>(size_t(n) * size_t(n), 0.f);
        const double r = key / 100.0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const QPointF d = layout.led(i).pos - layout.led(j).pos;
                const double d2 = d.x() * d.x() + d.y() * d.y();
                if (d2 <= 9.0 * r * r)
                    (*w)[size_t(i) * size_t(n) + size_t(j)] = float(std::pow(std::exp(-d2 / (2.0 * r * r)), power));
            }
        slot = std::move(w);
    }
    return slot;
}

} // namespace

void blurFrame(Frame& frame, const LedLayout& layout, double radius)
{
    const int n = layout.count();
    if (radius <= 0.01 || int(frame.size()) != n)
        return;
    const auto w = falloff(layout, radius);
    const Frame src = frame;
    for (int i = 0; i < n; ++i) {
        const float* row = w->data() + size_t(i) * size_t(n);
        double r = 0, g = 0, b = 0, a = 0, sum = 0;
        for (int j = 0; j < n; ++j) {
            const float k = row[j];
            if (k <= 0.f)
                continue;
            const Rgba& p = src[size_t(j)];
            const float pa = std::clamp(p.a, 0.f, 1.f);
            r += k * p.r * pa;
            g += k * p.g * pa;
            b += k * p.b * pa;
            a += k * pa;
            sum += k;
        }
        Rgba& o = frame[size_t(i)];
        o.a = float(a / sum);
        if (a > 1e-6) {
            o.r = float(r / a);
            o.g = float(g / a);
            o.b = float(b / a);
        }
    }
}

void sharpenFrame(Frame& frame, const LedLayout& layout, double amount)
{
    const int n = layout.count();
    if (amount <= 0.0 || int(frame.size()) != n)
        return;
    // Neighbours sit one pitch away; at this radius the six of them carry nearly all the weight.
    constexpr double kRadius = 0.7;
    const auto w = falloff(layout, kRadius);
    auto perceived = [](float v) { return float(std::pow(std::clamp(v, 0.f, 1.f), 1.0 / kPerceptual)); };
    std::vector<float> p(size_t(n) * 3);
    for (int i = 0; i < n; ++i) {
        const Rgba& c = frame[size_t(i)];
        p[size_t(i) * 3] = perceived(c.r);
        p[size_t(i) * 3 + 1] = perceived(c.g);
        p[size_t(i) * 3 + 2] = perceived(c.b);
    }
    const float k = float(amount);
    for (int i = 0; i < n; ++i) {
        Rgba& o = frame[size_t(i)];
        if (o.a <= 0.f)
            continue;
        const float* row = w->data() + size_t(i) * size_t(n);
        double acc[3] = {0, 0, 0}, sum = 0;
        for (int j = 0; j < n; ++j) {
            const float wj = row[j] * std::clamp(frame[size_t(j)].a, 0.f, 1.f);
            if (wj <= 0.f)
                continue;
            for (size_t c = 0; c < 3; ++c)
                acc[c] += wj * p[size_t(j) * 3 + c];
            sum += wj;
        }
        float* ch[3] = {&o.r, &o.g, &o.b};
        for (size_t c = 0; c < 3; ++c) {
            const float own = p[size_t(i) * 3 + c];
            const float v = std::clamp(own + k * (own - float(acc[c] / sum)), 0.f, 1.f);
            *ch[c] = float(std::pow(v, kPerceptual));
        }
    }
}

void glowFrame(Frame& frame, const LedLayout& layout, double strength, double radius)
{
    const int n = layout.count();
    if (strength <= 0.0 || int(frame.size()) != n)
        return;
    // Strength and falloff are perceived brightness; the halo itself is linear light.
    const auto w = falloff(layout, radius, kPerceptual);
    const float s = float(std::pow(std::min(strength, 1.0), kPerceptual));
    const Frame src = frame;
    for (int i = 0; i < n; ++i) {
        const float* row = w->data() + size_t(i) * size_t(n);
        // Premultiplied, so a halo reaching into a transparent part of the layer brings its own coverage along.
        const Rgba& own = src[size_t(i)];
        const float oa = std::clamp(own.a, 0.f, 1.f);
        float r = own.r * oa, g = own.g * oa, b = own.b * oa, a = oa;
        for (int j = 0; j < n; ++j) {
            const float k = row[j] * s;
            if (k <= 0.f || j == i)
                continue;
            const Rgba& p = src[size_t(j)];
            const float pa = std::clamp(p.a, 0.f, 1.f) * k;
            // Halo fainter than the dock can light: dropped, so the preview agrees with the LEDs.
            if (std::max({p.r, p.g, p.b}) * pa < kGlowFloor)
                continue;
            r = std::max(r, p.r * pa);
            g = std::max(g, p.g * pa);
            b = std::max(b, p.b * pa);
            a = std::max(a, pa);
        }
        Rgba& o = frame[size_t(i)];
        o.a = a;
        if (a > 1e-6) {
            o.r = r / a;
            o.g = g / a;
            o.b = b / a;
        }
    }
}

void applyLayerLook(const Layer& layer, Frame& frame, const LedLayout& layout)
{
    swapColors(frame, layer.swaps);
    if (layer.recolors())
        recolor(frame, layer.hue, layer.saturation, layer.lightness);
    contrastFrame(frame, layer.contrast);
    if (layer.tints())
        tintFrame(frame, layer.tint);
    sharpenFrame(frame, layout, layer.sharpen);
    blurFrame(frame, layout, layer.blur);
    glowFrame(frame, layout, layer.glow, layer.glowSize);
}

Scene::Scene() = default;

void Scene::render(double phase, const LedLayout& layout, std::vector<Rgb>& out) const
{
    const int n = layout.count();
    out.assign(size_t(n), Rgb{});
    Frame frame(static_cast<size_t>(n));
    RenderContext ctx;
    ctx.phase = phase - std::floor(phase);
    ctx.loopSeconds = loopSeconds;
    ctx.layout = &layout;

    for (const auto& layer : layers) {
        if (!layer->visible || !layer->effect || layer->opacity <= 0.0)
            continue;
        std::fill(frame.begin(), frame.end(), Rgba());
        // A layer with its own pace sees its own loop, so effects timed in seconds keep their speed.
        const double local = layer->timeOffset + ctx.phase * layer->timeScale;
        RenderContext lctx = ctx;
        lctx.phase = local - std::floor(local);
        lctx.loopSeconds = loopSeconds / layer->timeScale;
        // A filter reworks everything composited so far; it is done reading before the blend below writes.
        if (layer->effect->isFilter())
            lctx.below = &out;
        layer->effect->render(lctx, frame);
        applyLayerLook(*layer, frame, layout);
        const float opacity = float(layer->opacity);
        for (int i = 0; i < n; ++i) {
            const Rgba& s = frame[size_t(i)];
            const float a = std::clamp(s.a, 0.f, 1.f) * opacity * layer->maskAt(i);
            if (a <= 0.f)
                continue;
            Rgb& d = out[size_t(i)];
            switch (layer->blend) {
            case BlendMode::Normal:
                d.r += (s.r - d.r) * a;
                d.g += (s.g - d.g) * a;
                d.b += (s.b - d.b) * a;
                break;
            case BlendMode::Add:
                d.r += s.r * a;
                d.g += s.g * a;
                d.b += s.b * a;
                break;
            case BlendMode::Screen:
                d.r = 1.f - (1.f - d.r) * (1.f - s.r * a);
                d.g = 1.f - (1.f - d.g) * (1.f - s.g * a);
                d.b = 1.f - (1.f - d.b) * (1.f - s.b * a);
                break;
            case BlendMode::Multiply:
                d.r *= 1.f + (s.r - 1.f) * a;
                d.g *= 1.f + (s.g - 1.f) * a;
                d.b *= 1.f + (s.b - 1.f) * a;
                break;
            case BlendMode::Lighten:
                d.r = std::max(d.r, s.r * a + d.r * (1.f - a));
                d.g = std::max(d.g, s.g * a + d.g * (1.f - a));
                d.b = std::max(d.b, s.b * a + d.b * (1.f - a));
                break;
            }
        }
    }
    for (Rgb& c : out)
        c = color::clamp01(c);
}

bool Scene::isAnimated() const
{
    for (const auto& layer : layers)
        if (layer->visible && layer->effect && layer->opacity > 0 && layer->effect->isAnimated())
            return true;
    return false;
}

void Scene::trim(double from, double to)
{
    from = std::clamp(from, 0.0, 1.0);
    to = std::clamp(to, 0.0, 1.0);
    const double span = to - from;
    if (span < 1e-3)
        return;
    for (auto& layer : layers) {
        auto* keys = dynamic_cast<KeyframeEffect*>(layer->effect.get());
        if (keys && layer->timeOffset == 0.0 && layer->timeScale == 1.0) {
            keys->trim(from, to);
            continue;
        }
        const double start = layer->timeOffset + from * layer->timeScale;
        layer->timeOffset = start - std::floor(start);
        layer->timeScale = std::clamp(layer->timeScale * span, 0.05, 8.0);
    }
    loopSeconds = std::clamp(loopSeconds * span, 0.2, 60.0);
}

QJsonObject Scene::toJson() const
{
    QJsonArray arr;
    for (const auto& layer : layers) {
        QJsonObject lo{{"name", layer->name},
                       {"visible", layer->visible},
                       {"opacity", layer->opacity},
                       {"blend", int(layer->blend)}};
        if (layer->effect)
            lo.insert("effect", layer->effect->toJson());
        if (!layer->swaps.empty()) {
            QJsonArray swaps;
            for (const ColorSwap& s : layer->swaps)
                swaps.append(QJsonObject{{"from", s.from.name()}, {"to", s.to.name()}, {"range", s.range}});
            lo.insert("swaps", swaps);
        }
        if (layer->hue != 0.0)
            lo.insert("hue", layer->hue);
        if (layer->saturation != 1.0)
            lo.insert("saturation", layer->saturation);
        if (layer->lightness != 1.0)
            lo.insert("lightness", layer->lightness);
        if (layer->contrast != 1.0)
            lo.insert("contrast", layer->contrast);
        if (layer->tints())
            lo.insert("tint", layer->tint.name());
        if (layer->sharpen > 0.0)
            lo.insert("sharpen", layer->sharpen);
        if (layer->blur > 0.0)
            lo.insert("blur", layer->blur);
        if (layer->glow > 0.0) {
            lo.insert("glow", layer->glow);
            lo.insert("glowSize", layer->glowSize);
        }
        if (layer->timeOffset != 0.0)
            lo.insert("timeOffset", layer->timeOffset);
        if (layer->timeScale != 1.0)
            lo.insert("timeScale", layer->timeScale);
        if (!layer->mask.empty()) {
            QJsonArray m;
            for (float w : layer->mask)
                m.append(std::round(w * 1000.0) / 1000.0);
            lo.insert("mask", m);
        }
        arr.append(lo);
    }
    QJsonObject o{{"format", "flyglow"},
                  {"version", 1},
                  {"loopSeconds", loopSeconds},
                  {"brightness", brightness},
                  {"outputGamma", outputGamma},
                  {"darkLift", darkLift},
                  {"layers", arr}};
    if (!preset.isEmpty())
        o.insert("preset", preset);
    return o;
}

bool Scene::fromJson(const QJsonObject& o, QString* error)
{
    if (o.value("format").toString() != "flyglow") {
        if (error)
            *error = QCoreApplication::translate("Scene", "This is not a FlyGlow file.");
        return false;
    }
    std::vector<std::unique_ptr<Layer>> loaded;
    for (const QJsonValue& v : o.value("layers").toArray()) {
        const QJsonObject lo = v.toObject();
        auto layer = std::make_unique<Layer>();
        layer->name = lo.value("name").toString();
        layer->visible = lo.value("visible").toBool(true);
        layer->opacity = std::clamp(lo.value("opacity").toDouble(1.0), 0.0, 1.0);
        layer->blend = BlendMode(std::clamp(lo.value("blend").toInt(), 0, 4));
        layer->hue = std::clamp(lo.value("hue").toDouble(0.0), -180.0, 180.0);
        for (const QJsonValue& sv : lo.value("swaps").toArray()) {
            const QJsonObject so = sv.toObject();
            ColorSwap s;
            s.from = QColor(so.value("from").toString());
            s.to = QColor(so.value("to").toString());
            s.range = std::clamp(so.value("range").toDouble(30.0), 1.0, 180.0);
            if (s.from.isValid() && s.to.isValid())
                layer->swaps.push_back(s);
        }
        layer->saturation = std::clamp(lo.value("saturation").toDouble(1.0), 0.0, 10.0);
        layer->lightness = std::clamp(lo.value("lightness").toDouble(1.0), 0.0, 10.0);
        layer->contrast = std::clamp(lo.value("contrast").toDouble(1.0), 0.0, 4.0);
        const QColor tint(lo.value("tint").toString());
        layer->tint = tint.isValid() ? tint : QColor(Qt::white);
        layer->sharpen = std::clamp(lo.value("sharpen").toDouble(0.0), 0.0, 2.0);
        layer->blur = std::clamp(lo.value("blur").toDouble(0.0), 0.0, 3.0);
        layer->glow = std::clamp(lo.value("glow").toDouble(0.0), 0.0, 1.0);
        layer->glowSize = std::clamp(lo.value("glowSize").toDouble(1.5), 0.5, 4.0);
        layer->timeOffset = std::clamp(lo.value("timeOffset").toDouble(0.0), 0.0, 1.0);
        layer->timeScale = std::clamp(lo.value("timeScale").toDouble(1.0), 0.05, 8.0);
        layer->effect = Effect::createFromJson(lo.value("effect").toObject());
        if (!layer->effect)
            continue;
        const QJsonArray m = lo.value("mask").toArray();
        if (!m.isEmpty()) {
            layer->mask.assign(size_t(LedLayout::cd2().count()), 1.f);
            for (qsizetype i = 0; i < m.size() && size_t(i) < layer->mask.size(); ++i)
                layer->mask[size_t(i)] = float(std::clamp(m[i].toDouble(), 0.0, 1.0));
        }
        loaded.push_back(std::move(layer));
    }
    layers = std::move(loaded);
    loopSeconds = std::clamp(o.value("loopSeconds").toDouble(4.0), 0.2, 60.0);
    brightness = std::clamp(o.value("brightness").toInt(100), 1, 100);
    outputGamma = std::clamp(o.value("outputGamma").toDouble(kDockGamma), 0.5, 3.0);
    darkLift = std::clamp(o.value("darkLift").toDouble(0.0), 0.0, 1.0);
    preset = o.value("preset").toObject();
    return true;
}

void Scene::copyFrom(const Scene& other)
{
    fromJson(other.toJson());
}

float encodeOutput(float linear, double gamma)
{
    const float v = std::clamp(linear, 0.f, 1.f);
    return 255.f * (gamma == 1.0 ? v : float(std::pow(v, 1.0 / gamma)));
}

void bakeTiming(const Scene& scene, int maxFrames, int* frameCount, int* period)
{
    maxFrames = std::clamp(maxFrames, 2, 255);
    if (!scene.isAnimated()) {
        *frameCount = 1;
        *period = 1;
        return;
    }
    const double ms = scene.loopSeconds * 1000.0;
    *period = std::clamp(int(std::ceil(ms / (20.0 * maxFrames))), 1, 255);
    *frameCount = std::clamp(int(std::lround(ms / (20.0 * *period))), 2, maxFrames);
    // N % 25 == 4 makes the blob a multiple of 50 bytes, and Space Station then announces one HID pack too many.
    if (*frameCount % 25 == 4)
        *frameCount += *frameCount < maxFrames ? 1 : -1;
}

BakeResult bake(const Scene& scene, const LedLayout& layout, int maxFrames)
{
    BakeResult r;
    const int n = layout.count();
    bakeTiming(scene, maxFrames, &r.frameCount, &r.period);
    r.loopSeconds = r.frameCount * r.period * 0.02;

    std::vector<std::vector<Rgb>> frames(size_t(r.frameCount));
    for (int k = 0; k < r.frameCount; ++k)
        scene.render(double(k) / r.frameCount, layout, frames[size_t(k)]);

    r.rgb.resize(qsizetype(r.frameCount) * n * 3);
    auto* outBytes = reinterpret_cast<uchar*>(r.rgb.data());
    for (int k = 0; k < r.frameCount; ++k)
        encodeFrame(frames[size_t(k)], scene.outputGamma, outBytes + size_t(k) * size_t(n) * 3, scene.darkLift);
    return r;
}

float liftedLight(float light, double gamma, double lift)
{
    if (lift <= 0.0 || light <= 0.f)
        return light;
    // Output curves as 0..1 output values: the scene's own and gamma 2.2 scaled down, so 2.2 only wins near black.
    constexpr double kLow = 2.2, kSharp = 6.0, kMost = 0.55;
    const double weight = kMost * std::min(lift, 1.0);
    const double v = std::min(1.0, double(light));
    const double own = std::pow(v, 1.0 / gamma);
    const double low = weight * std::pow(v, 1.0 / kLow);
    // A soft maximum, renormalised so white stays white.
    const double norm = std::pow(1.0 + std::pow(weight, kSharp), 1.0 / kSharp);
    const double out = std::pow(std::pow(own, kSharp) + std::pow(low, kSharp), 1.0 / kSharp) / norm;
    return float(std::pow(std::min(1.0, out), gamma));
}

void encodeFrame(const std::vector<Rgb>& frame, double gamma, uchar* out, double lift)
{
    for (size_t i = 0; i < frame.size(); ++i) {
        float ch[3] = {frame[i].r, frame[i].g, frame[i].b};
        if (lift > 0.0) {
            // The whole colour scales with its brightest channel, so a dark colour gets lighter, not paler.
            const float m = std::max({ch[0], ch[1], ch[2]});
            if (m > 0.f) {
                const float k = liftedLight(m, gamma, lift) / m;
                for (float& v : ch)
                    v *= k;
            }
        }
        for (size_t c = 0; c < 3; ++c)
            out[i * 3 + c] = uchar(std::clamp(std::round(encodeOutput(ch[c], gamma)), 0.f, 255.f));
    }
}

} // namespace fg
