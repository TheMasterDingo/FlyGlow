// Pinwheel: curled arms turning around the centre of the LEDs, colours flowing round them.
// Loops through iPhase: the arms turn one arm's width per loop. Set "Loop crossfade" to 0.

const int ARMS = 6;
const float CURL = 1.5;        // how much the arms bend outward
const float SHARPNESS = 3.0;   // higher makes thinner arms with darker gaps

const float TAU = 6.2831853;

vec3 palette(float t)
{
    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    uv.y -= 0.05;                      // the LEDs sit a little above the middle of the frame
    float a = atan(uv.y, uv.x) / TAU;  // -0.5..0.5 around the centre
    float r = length(uv);
    float s = a * float(ARMS) + CURL * r * float(ARMS) - iPhase;
    float arm = pow(0.5 + 0.5 * cos(TAU * s), SHARPNESS);
    vec3 col = palette(a + iPhase) * arm;
    col += vec3(1.0) * exp(-r * r * 400.0);  // a bright hub
    fragColor = vec4(col, 1.0);
}
