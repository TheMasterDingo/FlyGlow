// Filter: reworks the layers below this one instead of drawing its own picture. With "Process layers below
// (iChannel0)" on (loading this template turns it on), iChannel0 holds those layers as they show, one flat hex
// cell per LED, at the frame's size: texture(iChannel0, fragCoord / iResolution.xy) gives them back unchanged.
// Opacity, blend mode and area of the layer say how much of the result goes over them. Stack several filter
// shaders for several passes. This one tears rows sideways and pulls the colours apart now and then.
// Loops through iPhase: set "Loop crossfade" to 0.

const float LED_X = 1.0 / 17.82;    // one LED pitch as a share of the frame's width
const float ROW_Y = 0.8636 / 16.17; // one row pitch as a share of the frame's height

float hash(float n)
{
    return fract(sin(n * 127.1 + 311.7) * 43758.5453);
}

vec3 below(vec2 uv)
{
    return texture(iChannel0, uv).rgb;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;

    // 24 jumps per loop; about one in three tears the picture.
    float jump = floor(iPhase * 24.0);
    float tear = step(0.65, hash(jump));

    // Bands of three rows slide by whole LEDs, so every LED still shows one LED's colour.
    float band = floor(uv.y / (3.0 * ROW_Y));
    uv.x += tear * floor((hash(jump * 13.0 + band) - 0.5) * 6.0 + 0.5) * LED_X;

    // Red comes from one LED to the left, blue from one to the right.
    float split = tear * LED_X;
    vec3 col = vec3(below(uv - vec2(split, 0.0)).r, below(uv).g, below(uv + vec2(split, 0.0)).b);
    fragColor = vec4(col, 1.0);
}
