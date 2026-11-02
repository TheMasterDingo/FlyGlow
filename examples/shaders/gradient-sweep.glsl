// Gradient sweep: two colours sliding across the dock at an angle, once per loop.
// Change the colours, the angle and how many times the gradient repeats across the dock.
// Loops through iPhase: set "Loop crossfade" to 0.

const vec3 COLOR_A = vec3(1.0, 0.1, 0.5);
const vec3 COLOR_B = vec3(0.1, 0.4, 1.0);
const float ANGLE = 30.0;   // degrees: 0 runs left to right, 90 bottom to top
const float REPEATS = 1.0;  // gradients across the dock

const float TAU = 6.2831853;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = radians(ANGLE);
    float d = dot(uv, vec2(cos(a), sin(a)));
    // A cosine instead of a ramp: no hard edge where the pattern repeats.
    float k = 0.5 + 0.5 * cos(TAU * (d * REPEATS - iPhase));
    fragColor = vec4(mix(COLOR_A, COLOR_B, k), 1.0);
}
