// Noise flow: soft clouds that drift and change shape, looping seamlessly.
// Each noise layer is moved around a circle in noise space, so the end of the loop meets its start.
// Loops through iPhase: set "Loop crossfade" to 0.

const float SCALE = 3.0;
const vec3 DARK = vec3(0.0, 0.0, 0.06);
const vec3 MID = vec3(0.0, 0.35, 0.9);
const vec3 LIGHT = vec3(0.5, 1.0, 0.85);

const float TAU = 6.2831853;

// A hash without sin(): stays random on every GPU.
float hash(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float noise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float x00 = mix(hash(i), hash(i + vec3(1, 0, 0)), f.x);
    float x10 = mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x);
    float x01 = mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x);
    float x11 = mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x);
    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}

float fbm(vec3 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * noise(p);
        p = p * 2.03 + 1.7;
        a *= 0.5;
    }
    return v;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y * SCALE;
    float t = TAU * iPhase;
    // One layer circles once per loop, the other twice the other way round.
    float n = 0.6 * fbm(vec3(uv + 0.8 * vec2(cos(t), sin(t)), 0.0))
            + 0.4 * fbm(vec3(1.7 * uv - 0.6 * vec2(cos(2.0 * t), -sin(2.0 * t)), 5.0));
    n = smoothstep(0.3, 0.7, n);
    vec3 col = n < 0.5 ? mix(DARK, MID, n * 2.0) : mix(MID, LIGHT, n * 2.0 - 1.0);
    fragColor = vec4(col, 1.0);
}
