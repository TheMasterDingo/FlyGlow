// Particles: soft dots rising and swaying. Each one crosses the dock a whole number of times per loop, so the
// loop closes; they wrap around below the bottom edge where they cannot be seen.
// Loops through iPhase: set "Loop crossfade" to 0.

const int COUNT = 18;
const float SIZE = 0.065;   // one LED is about 0.07 across
const vec3 COLOR_A = vec3(1.0, 0.45, 0.05);
const vec3 COLOR_B = vec3(1.0, 0.85, 0.3);

const float TAU = 6.2831853;

float dot2(vec2 v)
{
    return dot(v, v);
}

// A hash without sin(): stays random on every GPU.
float hash(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec3 col = vec3(0.0);
    for (int i = 0; i < COUNT; i++) {
        float fi = float(i);
        float speed = 1.0 + floor(hash(fi + 1.0) * 2.0);   // one or two crossings per loop
        float sway = 2.0 + floor(hash(fi + 4.0) * 2.0);    // sways per loop
        float y = fract(hash(fi + 2.0) + speed * iPhase) * 1.3 - 0.65;
        float x = hash(fi + 3.0) * 1.1 - 0.55 + 0.03 * sin(TAU * (sway * iPhase + hash(fi + 5.0)));
        float twinkle = 0.6 + 0.4 * cos(TAU * (3.0 * iPhase + hash(fi + 6.0)));
        float glow = exp(-dot2(uv - vec2(x, y)) / (SIZE * SIZE));
        col += mix(COLOR_A, COLOR_B, hash(fi + 7.0)) * glow * twinkle;
    }
    fragColor = vec4(min(col * 1.5, vec3(1.0)), 1.0);
}
