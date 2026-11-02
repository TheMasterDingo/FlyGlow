// Glowing shape: a signed-distance shape that pulses and glows. Pick SHAPE and COLOR.
// Distance functions return how far a point is from the shape's edge, negative inside, which makes outlines,
// fills and glows one line each. Loops through iPhase: set "Loop crossfade" to 0.

const int SHAPE = 2;                      // 0 circle, 1 heart, 2 star
const vec3 COLOR = vec3(1.0, 0.75, 0.1);
const float PULSES = 2.0;                 // per loop, a whole number

const float TAU = 6.2831853;

float dot2(vec2 v)
{
    return dot(v, v);
}

// Heart and star distances by Inigo Quilez (MIT).
float sdHeart(vec2 p)
{
    p.x = abs(p.x);
    if (p.y + p.x > 1.0)
        return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;
    return sqrt(min(dot2(p - vec2(0.0, 1.0)), dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);
}

float sdStar5(vec2 p, float r, float rf)
{
    const vec2 k1 = vec2(0.809016994375, -0.587785252292);
    const vec2 k2 = vec2(-k1.x, k1.y);
    p.x = abs(p.x);
    p -= 2.0 * max(dot(k1, p), 0.0) * k1;
    p -= 2.0 * max(dot(k2, p), 0.0) * k2;
    p.x = abs(p.x);
    p.y -= r;
    vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0, 1);
    float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
    return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

float shape(vec2 p)
{
    if (SHAPE == 1)
        return sdHeart((p + vec2(0.0, 0.32)) / 0.62) * 0.62;
    if (SHAPE == 2)
        return sdStar5(p - vec2(0.0, 0.02), 0.27, 0.5);
    return length(p) - 0.2;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    uv.y -= 0.05;   // the LEDs sit a little above the middle of the frame

    float pulse = pow(0.5 + 0.5 * cos(TAU * PULSES * iPhase), 4.0);
    float scale = 1.0 + 0.08 * pulse;
    float d = shape(uv / scale) * scale;

    // A tight glow keeps the shape's outline readable on the LEDs; the pulse widens it a little.
    float fill = smoothstep(0.01, -0.01, d);
    float glow = exp(-max(d, 0.0) * (40.0 - 15.0 * pulse)) * (0.25 + 0.5 * pulse);
    fragColor = vec4(COLOR * max(fill * (0.75 + 0.25 * pulse), glow), 1.0);
}
