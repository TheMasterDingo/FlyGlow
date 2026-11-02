// Template: a starting point for a shader of your own.
//
// fragCoord is the pixel and iResolution the frame size; the frame has the proportions of the dock, so
// circles stay round on the LEDs. iPhase runs from 0 to 1 once per loop: animate with whole multiples of it
// and the loop closes by itself (set "Loop crossfade" to 0). iTime (seconds) and iLoop (loop length in
// seconds) are there too. Black leaves an LED off.

const float TAU = 6.2831853;

// Smooth colours from one number; every whole step of t is one trip around the colour wheel.
vec3 palette(float t)
{
    return 0.5 + 0.5 * cos(TAU * (t + vec3(0.0, 0.33, 0.67)));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    // Centred coordinates: y runs from -0.5 at the bottom tip to 0.5 at the top edge, x on the same scale.
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    // The picture: colour by height, sliding up once per loop.
    vec3 col = palette(uv.y - iPhase);

    fragColor = vec4(col, 1.0);
}
