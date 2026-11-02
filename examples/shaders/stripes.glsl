// Stripes: diagonal bands sliding across, barber-pole style. They move one stripe per loop.
// Loops through iPhase: set "Loop crossfade" to 0.

const vec3 COLOR_A = vec3(1.0, 0.7, 0.0);
const vec3 COLOR_B = vec3(0.0);   // black leaves those LEDs off
const float ANGLE = 45.0;         // degrees
const float COUNT = 4.0;          // stripe pairs across the dock
const float SOFTNESS = 0.35;      // 0 gives hard edges, 1 a smooth wave

const float TAU = 6.2831853;

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    float a = radians(ANGLE);
    float d = dot(uv, vec2(cos(a), sin(a)));
    float wave = sin(TAU * (d * COUNT - iPhase));
    float k = smoothstep(-SOFTNESS - 0.01, SOFTNESS + 0.01, wave);
    fragColor = vec4(mix(COLOR_B, COLOR_A, k), 1.0);
}
