<p align="center">
  <img src="resources/flyglow-256.png" width="112" alt="FlyGlow icon">
</p>

<h1 align="center">FlyGlow</h1>

<p align="center">
  <b>Smooth, layered lighting for the Flydigi CD2 charging dock.</b><br>
  Effects in layers, LED-by-LED painting, media and Shadertoy shaders, side by side with Space Station.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <a href="#installation"><img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-lightgrey.svg" alt="Platform"></a>
  <a href="https://github.com/PsinaDev/FlyGlow/releases/latest"><img src="https://img.shields.io/github/v/release/PsinaDev/FlyGlow?color=brightgreen" alt="Release"></a>
  <a href="https://github.com/PsinaDev/FlyGlow/releases"><img src="https://img.shields.io/github/downloads/PsinaDev/FlyGlow/total?color=brightgreen" alt="Downloads"></a>
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6.10-41CD52?logo=qt&logoColor=white" alt="Qt 6.10"></a>
  <a href="#building-from-source"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20"></a>
  <a href="#how-it-works"><img src="https://img.shields.io/badge/device-Flydigi%20CD2-39d0ff.svg" alt="Device: Flydigi CD2"></a>
  <a href="README.ru.md"><img src="https://img.shields.io/badge/UI-English%20%7C%20%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-8b5cf6.svg" alt="UI: English, Russian"></a>
</p>

<p align="center">
  <a href="README.ru.md">Русская версия</a>
</p>

<p align="center">
  <img src="docs/screenshot-en.png" width="900" alt="FlyGlow editor with aurora, stars and a painted layer">
</p>

## Highlights

The CD2 dock has 162 good RGB LEDs, but the effects in Flydigi Space Station look rough: every effect is a fixed
50-frame loop, colours are blended in plain sRGB and fades step through raw 8-bit values. The dock only plays back
the frames it receives, so FlyGlow renders better frames and uploads them.

- **21 smooth effects.** Aurora, fire, plasma, lava lamp, rainbow waves, ripples, visor, comets, stars, rain,
  sparkles, fireflies and more. Linear-light rendering, OKLab blending and up to 200 frames per loop: no muddy
  gradient midpoints, smooth slow fades.
- **Preset gallery.** Ready-made scenes to start from: superheroes, games, arcade classics, emoji, animals, nature,
  space, holidays, patterns and calm mood lighting, with previews that play under the mouse. Many presets have a
  few settings of their own (colours, variants, decorations) at the top of the Inspector. Save your own scenes as
  presets, add folders such as Downloads to the gallery, and get new presets from the online library without
  reinstalling.
- **Layers like iCUE.** Stack effects with Normal, Add, Screen, Multiply or Lighten blending, set opacity, recolour a
  layer with hue, saturation, lightness, contrast or a tint, shift and speed up its timing, paint the area each
  layer covers. Pictures (media and painted layers) also get sharpen, blur and glow.
- **Filters.** A filter layer reworks everything below it. Glitch tears rows sideways, splits the RGB channels, drops
  in noise blocks and flickers in bursts, with strength, bursts per loop, burst length and seed to set and a seamless
  loop. Glint sweeps a soft band of light over whatever is lit below, brightening it in its own colour with a touch
  of the glint's colour, with direction, width, colour, whiteness, strength, passes per loop and sweep time to set.
  The filter layer's opacity, blend mode and area say how much of the result goes over the original.
- **Your own animation.** Paint the LEDs directly (size 1 is exactly one LED), set keyframes on the timeline and let
  FlyGlow interpolate everything in between, smooth, linear or stepped. Select part of the loop on the timeline to
  play just that part and trim the loop to it.
- **Media import.** PNG/JPEG, GIF, APNG, WebP and MP4/MOV/M4V/AVI/WMV. Each LED averages its own hex cell of the
  picture; fit, zoom, pan, rotate, trim and colour controls included.
- **Shaders.** Paste a Shadertoy-style GLSL `mainImage`: it runs on the GPU and each LED averages its cell of the
  frame. A crossfade closes the loop for shaders driven by `iTime`; `iPhase` loops by itself. Commented templates
  to start from (gradients, plasma, noise, shapes, particles, per-LED pixel work, a filter), `.glsl` files to open and
  save. With "Process layers below" on, a shader becomes a filter: the layers under it come in as `iChannel0`, one
  cell per LED, and several such shaders stacked work as a multipass shader. `examples/filters-demo.flyglow` shows a
  shader filter and a glitch over plasma.
- **Side by side with Space Station.** Uploads go straight to the dock over USB, so Space Station can stay open for
  the controller and the dock settings.
- **Everyday comforts.** Undo and redo, `.flyglow` scenes in tabs (files opened from Explorer join the running
  window), session autosave, file association, English and Russian UI switchable on the fly, a daily check for a
  newer release.

## How It Works

```mermaid
flowchart LR
    A[Effects and layers] --> B[Composite in linear light]
    B --> C[Bake up to 200 frames<br/>20 ms steps]
    C --> D[USB HID upload]
    D --> E[CD2 dock plays the loop]
    C -. fallback .-> F[Space Station service] -.-> E
```

- **Rendering.** An effect is a pure function of loop phase, so loops are seamless and the preview shows exactly the
  frames that get baked. Layers are composited in linear light; gradients and fades are mixed in OKLab and follow
  perceived brightness. A filter layer gets the layers below already composited and is blended like any other layer.
- **Baking.** The firmware plays uploaded frames and generates nothing itself: up to 200 frames by default
  (255 configurable), a frame interval in 20 ms steps, 8 bits per channel. The optional "Lift dark tones" raises
  the darkest tones toward a gamma-2.2 curve while the mids and highs keep the output gamma, so dark fades step less
  and stay lit, with hues kept; `examples/dark-lift-test.flyglow` shows it side by side with the plain output.
- **Upload.** FlyGlow opens the dock's vendor HID interface shared with Space Station's service and writes a Custom
  lighting config, about 85 ms per frame. If the HID path is unavailable, it falls back to Space Station's own
  service pipe. An upload rewrites the dock's stored lighting, so it only happens on "Send to dock".
- **Protocols.** [docs/hid-protocol.md](docs/hid-protocol.md) covers the direct USB path to the dock,
  [docs/ipc-protocol.md](docs/ipc-protocol.md) covers Space Station's service pipe.

## Installation

Requirements: Windows 10 1809 or later (x64), a Flydigi CD2 dock connected over USB. Space Station is optional.

| Package | Notes |
|---|---|
| `FlyGlow-<version>-setup-win64.exe` | Recommended. English and Russian setup, installs for the current user without admin rights, optional desktop icon and `.flyglow` association. |
| `FlyGlow-<version>-portable-win64.zip` | Unzip anywhere. Settings and autosave stay in `data\` next to the exe. |

Both are on the [Releases](https://github.com/PsinaDev/FlyGlow/releases/latest) page. Picking any effect in
Space Station brings the stock lighting back.

## Building from source

Requirements: Visual Studio 2022 with the C++ tools, Qt 6.10 (msvc2022_64), CMake 3.21+, Inno Setup 7 for the
installer.

From an "x64 Native Tools" prompt:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
cmake --build build
```

Run `build\FlyGlow.exe` with Qt's `bin` folder on `PATH`.

A Visual Studio solution is generated, not written by hand; F5 then starts FlyGlow with Qt on `PATH`:

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
```

`packaging\build-release.cmd` produces the portable zip and the installer in `dist\<version>\`; set `QTDIR` and, if needed,
`ISCC` first. Keep the checkout path short: CMake's try-compile paths hit the Windows 260-character limit under deep
roots.

Developer tools built alongside the app:

- `effects_check <dir>` - checks every effect for NaN, loop seams and harsh steps, and writes PNG previews.
- `media_check <file> <dir>` - decodes a clip and writes a filmstrip of the sampled LEDs.
- `shader_check <shader.glsl> <dir>` - compiles a shader like the Shader layer does, prints driver messages and timing,
  writes GPU frames and the LED result.
- `scene_check <scene.flyglow> <out.png>` - loads a scene like the app does, validates its preset block and renders
  loop phases as the dock lights them (preset format: `docs/presets.md`).
- `ipc_probe` - read-only queries to Space Station's service; `--selftest` runs offline.
- `FLYGLOW_NO_DEVICE=1` - starts the app without touching the dock.

## License and notices

FlyGlow is released under the [MIT License](LICENSE).

| Component | Notes |
|---|---|
| [Qt 6](https://www.qt.io/) | LGPL-3.0, linked dynamically; the release ships the unmodified Qt DLLs, which can be replaced. |
| Microsoft Visual C++ runtime | Redistributed app-locally under the Visual C++ Redistributable terms. |
| [OKLab](https://bottosson.github.io/posts/oklab/) | Colour space by Björn Ottosson, used for all colour blending. |
| [OpenFlydigi](https://github.com/mkaliaha/openflydigi) | First public documentation of the CD2 HID protocol. |

FlyGlow is an independent project, not affiliated with or endorsed by Flydigi. Flydigi and Space Station are
trademarks of their respective owners. Uploading replaces the dock's stored lighting; use at your own risk.
