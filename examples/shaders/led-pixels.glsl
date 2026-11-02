// Per-LED pixels: work LED by LED, like pixel art. ledCell() finds the LED under a pixel, so each LED gets one
// clean colour instead of a blend of whatever crosses its cell. Here a wave of colour runs down the rows.
// Loops through iPhase: set "Loop crossfade" to 0.

const float TAU = 6.2831853;

// Dock layout units: one LED pitch is 1, x to the right, y down, (0, 0) at the centre of the LEDs.
// The frame spans the dock outline's bounding box: 17.82 x 16.17 units, centred at (-0.018, 0.220).
vec2 dockPos(vec2 fragCoord)
{
    return (fragCoord / iResolution.xy - 0.5) * vec2(17.82, -16.17) + vec2(-0.018, 0.220);
}

// The LED nearest to p: .xy its centre, .z its row (0 is the top edge, 15 the bottom tip),
// .w its column within the row (0 on the left). Rows hold 14, 15, 16, 15, 14 ... 3 LEDs.
vec4 ledCell(vec2 p)
{
    float r0 = floor((p.y + 6.477) / 0.8636 + 0.5);
    vec4 best = vec4(0.0);
    float bestD = 1e9;
    for (int k = -1; k <= 1; k++) {
        float row = clamp(r0 + float(k), 0.0, 15.0);
        float n = row <= 2.0 ? 14.0 + row : 18.0 - row;
        float off = mod(n, 2.0) == 0.0 ? 0.5 : 0.0;
        float x = clamp(floor(p.x - off + 0.5) + off, -0.5 * (n - 1.0), 0.5 * (n - 1.0));
        vec2 c = vec2(x, -6.477 + row * 0.8636);
        float d = dot(p - c, p - c);
        if (d < bestD) {
            bestD = d;
            best = vec4(c, row, x + 0.5 * (n - 1.0));
        }
    }
    return best;
}

vec3 palette(float t)
{
    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 p = dockPos(fragCoord);
    vec4 led = ledCell(p);

    // One colour per row, a full trip down the dock per loop; every other column a little dimmer.
    vec3 col = palette(led.z / 16.0 - iPhase) * (mod(led.w, 2.0) < 1.0 ? 1.0 : 0.6);

    // Light only a disc around the LED's centre: the edge of its cell is shared with the neighbours.
    col *= step(length(p - led.xy), 0.46);
    fragColor = vec4(col, 1.0);
}
