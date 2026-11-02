#include "EffectsInternal.h"

namespace fg {
namespace {

using namespace fxi;

const QString kCategory = QT_TRANSLATE_NOOP("Effects", "Filters");

/**
 * Breaks up the picture below in bursts: rows slip sideways, the colour channels come apart, blocks of LEDs show
 * noise or another part of the picture, and the whole dock flickers. Every choice holds for one jump, a whole number
 * of which fit the loop, so the loop is seamless.
 */
class GlitchEffect : public Effect {
public:
    QString typeId() const override { return "glitch"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Glitch"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pPercent("strength", QT_TRANSLATE_NOOP("Effects", "Strength"), 70),
            pInt("bursts", QT_TRANSLATE_NOOP("Effects", "Bursts per loop"), 3, 1, 16),
            pPercent("burstLength", QT_TRANSLATE_NOOP("Effects", "Burst length"), 35),
            pPercent("idle", QT_TRANSLATE_NOOP("Effects", "Jitter between bursts"), 8),
            pFloat("rate", QT_TRANSLATE_NOOP("Effects", "Jumps per second"), 12, 2, 25, 1),
            pPercent("rowShift", QT_TRANSLATE_NOOP("Effects", "Row shift"), 60),
            pPercent("rgbSplit", QT_TRANSLATE_NOOP("Effects", "RGB split"), 50),
            pPercent("blocks", QT_TRANSLATE_NOOP("Effects", "Noise blocks"), 40),
            pPercent("flicker", QT_TRANSLATE_NOOP("Effects", "Flicker"), 30),
            pSeed(7),
        };
    }
    bool isFilter() const override { return true; }
    bool isAnimated() const override { return f("strength") > 0.0; }

    double showcasePhase(double phase) const override
    {
        // The middle of the burst in phase's stretch of the loop.
        const int k = std::max(1, i("bursts"));
        const int b = std::clamp(int(fx::wrap01(phase) * k), 0, k - 1);
        return (b + burstStart(b) + 0.5 * burstLength()) / k;
    }

    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const int n = L.count();
        std::vector<Rgb> cur(static_cast<size_t>(n));
        if (ctx.below)
            std::copy_n(ctx.below->begin(), std::min(n, int(ctx.below->size())), cur.begin());

        const double phase = fx::wrap01(ctx.phase);
        const int k = std::max(1, i("bursts"));
        const int b = std::clamp(int(phase * k), 0, k - 1);
        const double u = phase * k - b;
        const bool inBurst = u >= burstStart(b) && u < burstStart(b) + burstLength();
        const double amount = clamp01(f("strength") / 100.0) * (inBurst ? 1.0 : clamp01(f("idle") / 100.0));

        if (amount > 0.0) {
            const int ticks = std::max(1, int(std::lround(ctx.loopSeconds * std::max(0.1, f("rate")))));
            const uint32_t tick = uint32_t(std::min(ticks - 1, int(phase * ticks)));
            const int seed = i("seed");
            auto h = [&](uint32_t salt, uint32_t index = 0) { return h01(seed, salt, tick * 7919U + index); };
            shiftRows(L, cur, amount * f("rowShift") / 100.0, h);
            splitChannels(L, cur, amount * f("rgbSplit") / 100.0, h);
            noiseBlocks(L, cur, amount * f("blocks") / 100.0, h);
            flicker(cur, amount * f("flicker") / 100.0, h);
        }
        for (int j = 0; j < n; ++j)
            out[size_t(j)] = Rgba(cur[size_t(j)], 1.f);
    }

private:
    double burstLength() const { return std::clamp(f("burstLength") / 100.0, 0.05, 1.0); }
    /** Where burst b starts within its stretch of the loop, 0..1. */
    double burstStart(int b) const { return h01(i("seed"), 1, uint32_t(b)) * (1.0 - burstLength()); }

    template <class Hash>
    static void shiftRows(const LedLayout& L, std::vector<Rgb>& cur, double amount, const Hash& h)
    {
        if (amount <= 0.0)
            return;
        const std::vector<Rgb> src = cur;
        int shift = 0;
        for (int r = 0; r < L.rowCount(); ++r) {
            // Rows slip in bands of a few, like a torn picture.
            if (r == 0 || h(2, uint32_t(r)) < 0.45) {
                shift = 0;
                if (h(3, uint32_t(r)) < std::min(1.0, amount * 1.2))
                    shift = int(std::lround((2.0 * h(4, uint32_t(r)) - 1.0) * (1.0 + 6.0 * amount)));
            }
            if (shift == 0)
                continue;
            const int start = L.rowStart(r), len = L.rowLength(r);
            for (int c = 0; c < len; ++c)
                cur[size_t(start + c)] = src[size_t(start + imod(c - shift, len))];
        }
    }

    template <class Hash>
    static void splitChannels(const LedLayout& L, std::vector<Rgb>& cur, double amount, const Hash& h)
    {
        if (amount <= 0.0)
            return;
        const std::vector<Rgb> src = cur;
        const double d = amount * 2.5 * (h(5) < 0.5 ? 1.0 : -1.0);
        for (int r = 0; r < L.rowCount(); ++r) {
            const int start = L.rowStart(r), len = L.rowLength(r);
            // One channel read along the row at a fractional LED offset, held at the row's ends.
            auto along = [&](double c, float Rgb::* ch) {
                c = std::clamp(c, 0.0, double(len - 1));
                const int c0 = int(std::floor(c)), c1 = std::min(len - 1, c0 + 1);
                const float t = float(c - c0);
                const float a = src[size_t(start + c0)].*ch, b = src[size_t(start + c1)].*ch;
                return a + (b - a) * t;
            };
            for (int c = 0; c < len; ++c) {
                Rgb& o = cur[size_t(start + c)];
                o.r = along(c - d, &Rgb::r);
                o.b = along(c + d, &Rgb::b);
            }
        }
    }

    template <class Hash>
    static void noiseBlocks(const LedLayout& L, std::vector<Rgb>& cur, double amount, const Hash& h)
    {
        if (amount <= 0.0)
            return;
        static const Rgb kNoise[] = {{1, 1, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {0, 0, 0}};
        const std::vector<Rgb> src = cur;
        const int count = int(std::floor(amount * 5.0 + h(6)));
        for (int k = 0; k < count; ++k) {
            const uint32_t base = uint32_t(k) * 16U;
            const QPointF center = L.led(std::min(L.count() - 1, int(h(7, base) * L.count()))).pos;
            const double radius = 0.8 + 1.7 * h(7, base + 1);
            const double mode = h(7, base + 2);
            // A copy of another part of the picture, LED noise, or one flat colour.
            const QPointF offset((2.0 * h(7, base + 3) - 1.0) * 5.0, (2.0 * h(7, base + 4) - 1.0) * 4.0);
            const Rgb flat = kNoise[size_t(h(7, base + 5) * 6.0)];
            for (int j : L.ledsWithin(center, radius)) {
                Rgb& o = cur[size_t(j)];
                if (mode < 0.5) {
                    const int from = L.ledAt(L.led(j).pos + offset, 0.8);
                    o = from >= 0 ? src[size_t(from)] : Rgb{};
                } else if (mode < 0.8) {
                    o = kNoise[size_t(h(8, base * 64U + uint32_t(j)) * 7.0)];
                } else {
                    o = flat;
                }
            }
        }
    }

    template <class Hash>
    static void flicker(std::vector<Rgb>& cur, double amount, const Hash& h)
    {
        if (amount <= 0.0)
            return;
        if (h(9) < amount) {
            const float gain = float(0.15 + 0.6 * h(10));
            for (Rgb& c : cur)
                c = {c.r * gain, c.g * gain, c.b * gain};
        } else if (h(11) < amount * 0.15) {
            // A rare negative frame, inverted as the eye sees it.
            auto inv = [](float v) { return color::srgbToLinear(1.f - color::linearToSrgb(std::clamp(v, 0.f, 1.f))); };
            for (Rgb& c : cur)
                c = {inv(c.r), inv(c.g), inv(c.b)};
        }
    }
};

/**
 * A glint sweeping over the picture below: a soft band of light crosses the dock in the chosen direction and lights
 * up what is lit there, the way a shiny surface catches the light: the colour underneath gets brighter in its own
 * hue, and only "Whiteness" of it turns towards the glint's colour, so a red logo flashes light red rather than
 * under a white stripe. The dark round it stays dark ("Light the dark" lets the band show on unlit LEDs too).
 * Whole passes per loop, each crossing in part of its stretch and resting for the rest, so the loop is seamless.
 */
class GlintEffect : public Effect {
public:
    QString typeId() const override { return "glint"; }
    QString name() const override { return QT_TRANSLATE_NOOP("Effects", "Glint"); }
    QString category() const override { return kCategory; }
    QList<ParamDesc> params() const override
    {
        return {
            pAngle(30.0),
            pFloat("width", QT_TRANSLATE_NOOP("Effects", "Width"), 1.6, 0.3, 8.0, 0.1),
            pPercent("strength", QT_TRANSLATE_NOOP("Effects", "Strength"), 70),
            pInt("passes", QT_TRANSLATE_NOOP("Effects", "Passes per loop"), 1, 1, 8),
            pPercent("sweep", QT_TRANSLATE_NOOP("Effects", "Sweep time"), 50),
            pColor("color", QT_TRANSLATE_NOOP("Effects", "Color"), QColor(255, 255, 255)),
            pPercent("whiteness", QT_TRANSLATE_NOOP("Effects", "Whiteness"), 30),
            pPercent("dark", QT_TRANSLATE_NOOP("Effects", "Light the dark"), 0),
        };
    }
    bool isFilter() const override { return true; }
    bool isAnimated() const override { return f("strength") > 0.0; }

    double showcasePhase(double phase) const override
    {
        // The band halfway across, in phase's pass.
        const int k = std::max(1, i("passes"));
        const int p = std::clamp(int(fx::wrap01(phase) * k), 0, k - 1);
        return (p + 0.5 * sweep()) / k;
    }

    void render(const RenderContext& ctx, Frame& out) const override
    {
        const LedLayout& L = layoutOf(ctx);
        const int n = L.count();
        std::vector<Rgb> cur(static_cast<size_t>(n));
        if (ctx.below)
            std::copy_n(ctx.below->begin(), std::min(n, int(ctx.below->size())), cur.begin());

        const int k = std::max(1, i("passes"));
        const double u = fx::wrap01(ctx.phase) * k;
        const double progress = (u - std::floor(u)) / sweep();
        const double strength = clamp01(f("strength") / 100.0);
        if (progress < 1.0 && strength > 0.0) {
            std::vector<double> t;
            const double span = std::max(1e-6, projectLeds(L, f("angle"), t));
            const double width = std::max(0.1, f("width"));
            // The band starts and ends wholly off the dock: two widths past either edge.
            const double margin = 2.0 * width / span;
            const double centre = -margin + (1.0 + 2.0 * margin) * progress;
            const Rgb colour = c("color");
            const double dark = clamp01(f("dark") / 100.0);
            const double whiteness = clamp01(f("whiteness") / 100.0);
            for (int j = 0; j < n && j < int(t.size()); ++j) {
                const double g = gauss((t[size_t(j)] - centre) * span, 0.5 * width) * strength;
                if (g < 1e-3)
                    continue;
                Rgb& o = cur[size_t(j)];
                // How lit the LED is, as the eye sees it: full from a quarter of the way up.
                const double lit = clamp01(color::linearToSrgb(std::max({o.r, o.g, o.b})) / 0.25);
                const float k = float(g * (dark + (1.0 - dark) * lit));
                // The colour underneath, brighter in its own hue (linear light, each channel up to full)...
                const float gain = 1.f + 2.5f * k;
                const Rgb lifted{std::min(1.f, o.r * gain), std::min(1.f, o.g * gain), std::min(1.f, o.b * gain)};
                // ...then a sheen of the glint's colour, as bright as the surface it lands on.
                const float level = std::max({lifted.r, lifted.g, lifted.b, float(dark * k)});
                const float w = k * float(whiteness);
                o = {lifted.r + (colour.r * level - lifted.r) * w, lifted.g + (colour.g * level - lifted.g) * w,
                     lifted.b + (colour.b * level - lifted.b) * w};
            }
        }
        for (int j = 0; j < n; ++j)
            out[size_t(j)] = Rgba(cur[size_t(j)], 1.f);
    }

private:
    double sweep() const { return std::clamp(f("sweep") / 100.0, 0.05, 1.0); }
};

} // namespace

void registerFilterEffects(EffectRegistry& r)
{
    r.add<GlitchEffect>();
    r.add<GlintEffect>();
}

} // namespace fg
