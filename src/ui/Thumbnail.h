#pragma once

#include <QPixmap>
#include <vector>

#include "core/Color.h"

namespace fg {

class Effect;
struct Layer;

QPixmap renderThumbnail(const std::vector<Rgb>& linear, const QSize& size, qreal dpr = 2.0);
/** look, when given, applies a layer's colour, tint, blur and glow settings to the effect. */
QPixmap renderEffectThumbnail(const Effect& effect, double phase, const QSize& size, qreal dpr = 2.0,
                              const Layer* look = nullptr);

} // namespace fg
