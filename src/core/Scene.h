#pragma once

#include "Effect.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <memory>
#include <vector>

namespace fg {

class LedLayout;

enum class BlendMode { Normal = 0, Add, Screen, Multiply, Lighten };
/** English sources in the "Effects" translation context, in enum order. */
QStringList blendModeNames();

/**
 * One colour of a layer replaced by another, shading kept: pixels near from in hue (or, for a grey or white from,
 * pixels without colour) are turned, scaled and lit so that from itself becomes to exactly. Darker, lighter and
 * glinting versions of from follow along. from == to changes nothing; presets ship swaps like that and let a colour
 * control set to.
 */
struct ColorSwap {
    QColor from;
    QColor to;
    double range = 30.0;   /**< hue distance in degrees at which a pixel stops being affected; full effect within half */
};

struct Layer {
    QString name;
    bool visible = true;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    std::unique_ptr<Effect> effect;
    std::vector<float> mask;   /**< empty = whole dock, else one weight 0..1 per LED */
    std::vector<ColorSwap> swaps;   /**< applied first, to the colours as the effect draws them */
    double hue = 0;            /**< degrees -180..180, turns the layer's colours around the OKLab hue circle */
    double saturation = 1.0;   /**< 0..10, scales the layer's OKLab chroma */
    double lightness = 1.0;    /**< 0..10, scales the layer's OKLab lightness: 0 is black */
    double contrast = 1.0;     /**< 0..4, spreads the OKLab lightness about mid grey, see contrastFrame(); 1 = off */
    QColor tint = Qt::white;   /**< white = off; else the layer turns into shades of this colour, see tintFrame() */
    double blur = 0;           /**< 0..3, Gaussian radius in LED pitches that softens the layer */
    double glow = 0;           /**< 0..1, how strongly lit LEDs light up their neighbours */
    double glowSize = 1.5;     /**< 0.5..4, glow radius in LED pitches */
    double sharpen = 0;        /**< 0..2, unsharp mask amount across neighbouring LEDs, see sharpenFrame() */
    /**
     * The layer plays its own loop from timeOffset at timeScale times the scene's pace: phase timeOffset +
     * phase * timeScale. Whole-number scales keep the loop seamless; trimming the scene makes fractional ones.
     */
    double timeOffset = 0;     /**< 0..1 */
    double timeScale = 1.0;    /**< 0.05..8 */

    std::unique_ptr<Layer> clone() const;
    float maskAt(int led) const { return mask.empty() ? 1.f : mask[size_t(led)]; }
    bool recolors() const { return hue != 0.0 || saturation != 1.0 || lightness != 1.0; }
    bool tints() const { return tint.rgb() != QColor(Qt::white).rgb(); }
};

/**
 * Layers composited bottom-up in linear light, plus loop and output settings: what a .flyglow file holds.
 */
class Scene {
public:
    Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    std::vector<std::unique_ptr<Layer>> layers;   /**< [0] is the bottom layer */
    double loopSeconds = 4.0;
    int brightness = 100;       /**< dock brightness 1..100 */
    double outputGamma = 1.4;   /**< matching kDockGamma keeps the preview's light ratios on the LEDs; 1 sends linear values */
    double darkLift = 0.0;      /**< 0..1, lifts the darkest tones toward a gamma-2.2 curve, see liftedLight() */
    /**
     * A built-in preset's title, description and controls ("preset" in the file), kept as written so they survive
     * opening and saving; empty for an ordinary scene. See docs/presets.md.
     */
    QJsonObject preset;

    /** Composites at phase into out, resized to layout.count(); linear light. */
    void render(double phase, const LedLayout& layout, std::vector<Rgb>& out) const;
    bool isAnimated() const;
    /**
     * Keeps only what plays between phases from and to (from < to) as the whole loop: the loop gets shorter by
     * the same amount and every layer keeps its pace. Keyframe layers are re-keyed, others get a time offset and
     * scale.
     */
    void trim(double from, double to);

    QJsonObject toJson() const;
    /**
     * Layers with an unknown effect type are skipped rather than failing the file.
     * @param error Receives a translated message when o is not a FlyGlow scene.
     */
    bool fromJson(const QJsonObject& o, QString* error = nullptr);
    /** Deep copy through a JSON round trip. */
    void copyFrom(const Scene& other);
};

struct BakeResult {
    int frameCount = 0;
    int period = 1;              /**< frame interval in 20 ms units */
    double loopSeconds = 0;      /**< actual loop length after rounding to whole periods */
    QByteArray rgb;              /**< frameCount * LEDs * 3 bytes, 8-bit RGB */
};

/** Frame count and period bake() will use, without rendering anything. */
void bakeTiming(const Scene& scene, int maxFrames, int* frameCount, int* period);

/** @param maxFrames Clamped to 2..255. */
BakeResult bake(const Scene& scene, const LedLayout& layout, int maxFrames = 200);

/**
 * How the dock turns an output value into light: roughly (value / 255)^kDockGamma, except that values up to
 * kDockDarkValue leave the LEDs dark. Judged by eye against a 50% checker on firmware 0.0.3.9.
 */
inline constexpr double kDockGamma = 1.4;
inline constexpr int kDockDarkValue = 8;

/**
 * Turns each colour hueDegrees around the OKLab hue circle and scales its chroma by saturation and its lightness by
 * lightness. Together they take any one colour to any other, black and white included.
 */
void recolor(Frame& frame, double hueDegrees, double saturation, double lightness = 1.0);
/**
 * Spreads each colour's OKLab lightness away from mid grey by amount (below 1 pulls it towards grey), its chroma
 * following in proportion so the hue and saturation hold: dark parts get darker, light parts lighter.
 */
void contrastFrame(Frame& frame, double amount);
/** Applies the swaps that change something; see ColorSwap. */
void swapColors(Frame& frame, const std::vector<ColorSwap>& swaps);
/**
 * Turns each colour into a shade of tint (sRGB): its brightest channel scales the tint in linear light, so white
 * and every full colour become the tint exactly and darker colours a darker shade of it.
 */
void tintFrame(Frame& frame, const QColor& tint);
/** Gaussian blur across the LED lattice, radius in LED pitches; premultiplied, so edges fade into transparency. */
void blurFrame(Frame& frame, const LedLayout& layout, double radius);
/**
 * Unsharp mask across the LED lattice: each LED moves away from the average of its neighbours by amount times the
 * difference, in perceived brightness, so outlines of a picture read crisper. Transparent neighbours do not count,
 * and the layer's coverage stays as it is.
 */
void sharpenFrame(Frame& frame, const LedLayout& layout, double amount);
/**
 * Lit LEDs light up their neighbours: each LED takes the brightest of its own colour and its neighbours' colours
 * faded by strength and a Gaussian of the given radius. Never brightens an LED beyond its brightest neighbour, so
 * colours keep their hue.
 */
void glowFrame(Frame& frame, const LedLayout& layout, double strength, double radius);
/** All of a layer's post-processing, in render order. */
void applyLayerLook(const Layer& layer, Frame& frame, const LedLayout& layout);

/** Linear light to the unrounded 0..255 output value for the given output gamma. */
float encodeOutput(float linear, double gamma);

/**
 * The light an LED's brightest channel gets with the dark tones lifted. At output gamma 1.4 the darkest tones sit on
 * output values where one step is a big step of light and the LEDs switch off below 9; a gamma-2.2 curve keeps them
 * higher and finer but pales every colour. This follows the output curve in the mids and highs and blends towards
 * gamma 2.2 near black (a soft maximum of the two), so only the darks rise. lift 0 changes nothing, 1 is the most.
 */
float liftedLight(float light, double gamma, double lift);

/**
 * One frame's 8-bit RGB values exactly as bake() sends them.
 * @param out  frame.size() * 3 bytes.
 * @param lift Scene::darkLift: each LED's colour is scaled so its brightest channel gets liftedLight(), which keeps
 *             its hue.
 */
void encodeFrame(const std::vector<Rgb>& frame, double gamma, uchar* out, double lift = 0.0);

} // namespace fg
