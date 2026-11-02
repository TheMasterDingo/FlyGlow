// Plasma: overlapping sine waves turned into colour. Every term moves a whole number of times per loop.
// Loops through iPhase: set "Loop crossfade" to 0.

const float SCALE = 6.0;     // waves across the dock
const float CONTRAST = 1.8;  // above 1 deepens the colours, which reads better on the LEDs

const float TAU = 6.2831853;

vec3 palette(float t)
{
    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * SCALE;
    float t = TAU * iPhase;
    float v = sin(uv.x + t)
            + sin(0.8 * uv.y - t)
            + sin(0.7 * (uv.x + uv.y) + 2.0 * t)
            + sin(1.3 * length(uv - 2.0 * vec2(sin(t), cos(t))));
    vec3 col = palette(0.15 * v + iPhase);
    fragColor = vec4(pow(col, vec3(CONTRAST)), 1.0);
}
