# Presets

FlyGlow ships ready-made scenes in its preset gallery. A preset is an ordinary `.flyglow` scene with one extra
top-level object, `"preset"`, that gives it a title, a description and a few simple controls. The gallery shows
every scene under `examples/scenes/<category>/`; the build compiles them into the program, so a new preset is just a
new file in one of those folders.

## 1. Categories

The folder a preset sits in is its category. Known folders get a translated name and a fixed place in the gallery;
any other folder is shown under its own name after them.

| Folder | Shown as |
|---|---|
| `heroes` | Superheroes |
| `games` | Games |
| `retro` | Retro and arcade |
| `emoji` | Emoji |
| `memes` | Memes |
| `anime` | Anime |
| `animals` | Animals |
| `nature` | Nature |
| `space` | Space |
| `holidays` | Holidays |
| `abstract` | Patterns |
| `ambient` | Mood lighting |
| `minimal` | Minimal |

Besides the built-in presets the gallery shows:

- `mine` (My presets) - scenes saved with "Save current scene…" in the gallery (Ctrl+Shift+P), kept in
  `presets/mine` in the data folder (`data\` next to a portable exe, else `%APPDATA%\FlyGlow\FlyGlow`).
- `other` (Other) - every `.flyglow` in the folders added with "Folders…", without subfolders. A scene without a
  preset block is listed under its file name.
- presets from the online library (section 6), in `presets/online/<category>/` of the data folder; one with the same
  `<category>/<name>` replaces the built-in copy.

## 2. The preset block

```json
{
 "format": "flyglow",
 "version": 1,
 "preset": {
  "title": {"en": "Fox", "ru": "Лиса"},
  "description": {"en": "An orange fox that blinks twice per loop.", "ru": "Рыжая лиса, моргает дважды за цикл."},
  "cover": 0.3,
  "order": 10,
  "tags": ["animal", "cute"],
  "controls": [ ... ]
 },
 "loopSeconds": 5,
 ...
}
```

| Key | Type | Meaning |
|---|---|---|
| `title` | `{"en", "ru"}` | Name in the gallery and on the scene's tab until it is saved under a name of its own |
| `description` | `{"en", "ru"}` | One or two sentences under the preview |
| `cover` | number 0..1 | Loop phase the gallery thumbnail shows (default 0.25) |
| `order` | number | Position inside the category, lower first (default: after the numbered ones, by title) |
| `group` | string or `{"en", "ru"}` | Franchise or theme inside the category ("Minecraft", "Pac-Man"); a category with two or more groups gets a row of group chips |
| `tags` | strings | Extra words the gallery search matches; the details panel shows the ones in the UI's script as links that search for them |
| `controls` | array | Settings shown at the top of the Inspector while the scene is open (section 3) |

The block is kept when the scene is opened, edited and saved, so a saved copy keeps its title and controls.

## 3. Controls

A control is a view over values the scene already has: it reads its value from its first target and writes every
target when changed. Nothing else is stored, so a control and the Inspector field it drives always agree, and
undo works the same for both.

Every control has `type`, `label` (`{"en", "ru"}`) and `targets`. Layers are numbered from the bottom, starting at 0,
as in the file.

| Type | Targets | Shown as |
|---|---|---|
| `hue` | `{"layer": i}` | Colour picker: the chosen colour sets the layers' hue turn, saturation and lightness (`hue`, `saturation`, `lightness`, section 4) relative to `base` |
| `saturation` | `{"layer": i}` | Slider 0..200 % (`Layer.saturation`) |
| `opacity` | `{"layer": i}` | Slider 0..100 % |
| `toggle` | `{"layer": i}` | Switch that shows or hides the layers |
| `color` | `{"layer": i, "param": "id"}`, `{"layer": i, "swap": k}` or `{"layer": i, "prop": "tint"}` | Colour button for a Color parameter of the layer's effect, for the replacement colour of the layer's swap `k` (with a button back to the original), or for the layer's tint |
| `number` | `{"layer": i, "param": "id"}`, `{"layer": i, "prop": "name"}` or `{"scene": "loopSeconds"}` | Slider from `min` to `max` in `step`, with an optional `suffix` |
| `choice` | none: `options` instead | Drop-down list of variants, see below |

A `hue` control may carry `"base": "#rrggbb"`, the layers' main colour as drawn: the picker shows it turned and
scaled by the current settings, and a picked colour sets the hue turn from the difference in hue, the saturation from
the ratio of chroma and the lightness from the ratio of OKLab lightness. The base colour itself therefore turns into
exactly the picked one, black and white included. Without `base` FlyGlow takes the dominant saturated colour of the
first target layer. A layer drawn in white or grey has no hue to turn: give it a `tint` control instead.

A `prop` target drives one of the layer's own settings from section 4: `opacity`, `hue`, `saturation`,
`lightness`, `contrast`, `sharpen`, `blur`, `glow` or `glowSize` on a `number` control, `tint` on a `color` control.

A `choice` control lists `options`, each with a `label` and a `set` of values to apply when it is picked. A `set`
entry names a layer and either `"visible": true/false`, `"prop"` and `"value"`, or `"param"` and `"value"` (colours
as `"#rrggbb"`). The list shows the first option whose values all hold, and none after the user changes one of them
by hand.

```json
{"type": "choice", "label": {"en": "Logo", "ru": "Логотип"}, "options": [
 {"label": {"en": "Lambda in a circle", "ru": "Лямбда в круге"},
  "set": [{"layer": 1, "visible": true}, {"layer": 2, "visible": true}]},
 {"label": {"en": "Lambda only", "ru": "Только лямбда"},
  "set": [{"layer": 1, "visible": false}, {"layer": 2, "visible": true}]}
]}
```

Any scene can get controls of its own: right-click a layer setting, an effect parameter or a layer in the list and
choose "Show in preset settings…"; right-click a control's name at the top of the Inspector to rename or remove it.

A `number` target may carry `"from"` and `"to"`: the control's `min`..`max` is then mapped linearly onto
`from`..`to` for that target, so one slider can drive parameters with different ranges. Without them the value is
written as is.

```json
"controls": [
 {"type": "hue", "label": {"en": "Fur colour", "ru": "Цвет шерсти"}, "targets": [{"layer": 0}]},
 {"type": "toggle", "label": {"en": "Stars", "ru": "Звёзды"}, "targets": [{"layer": 1}]},
 {"type": "number", "label": {"en": "Loop", "ru": "Петля"}, "min": 2, "max": 12, "step": 0.5, "suffix": "s",
  "targets": [{"scene": "loopSeconds"}]},
 {"type": "number", "label": {"en": "Sparkles", "ru": "Искры"}, "min": 0, "max": 100, "suffix": "%",
  "targets": [{"layer": 2, "param": "count", "from": 2, "to": 40}]}
]
```

## 4. Layer look and time

Every layer has these settings, stored in the file only when changed. They apply, in this order, to whatever the
layer's effect draws, painted keyframes included, which is what lets a preset offer a colour choice for a picture.
The Inspector shows `sharpen`, `blur` and the glow for media and painted layers only (and for any other layer that
already has them set), and the time settings for every layer except painted ones.

| Key | Range | Default | Meaning |
|---|---|---|---|
| `swaps` | `[{"from", "to", "range"}]` | none | Colour replacements, applied first: pixels within `range` degrees of `from`'s hue (or, for a grey or white `from`, pixels without colour) are turned, scaled and lit so that `from` becomes `to` exactly, shading and glints included. A swap with `to` equal to `from` changes nothing: presets ship them that way for their colour pickers |
| `hue` | -180..180 | 0 | Degrees around the OKLab hue circle |
| `saturation` | 0..10 | 1 | OKLab chroma scale; 0 is grey |
| `lightness` | 0..10 | 1 | OKLab lightness scale; 0 is black |
| `contrast` | 0..4 | 1 | Spreads OKLab lightness about mid grey (below 1 towards grey), hue kept; applied after `lightness` |
| `tint` | `"#rrggbb"` | white (off) | Turns the layer into shades of one colour: the brightest channel of each LED scales the tint, so white and every full colour become the tint exactly |
| `sharpen` | 0..2 | 0 | Unsharp mask across neighbouring LEDs in perceived brightness: each LED moves away from its neighbours' average by this times the difference, so a picture's outlines read crisper |
| `blur` | 0..3 | 0 | Gaussian blur across the LED lattice, radius in LED pitches |
| `glow` | 0..1 | 0 | Lit LEDs light up their neighbours with a falloff that looks even to the eye; never brighter than the brightest neighbour, so colours keep their hue |
| `glowSize` | 0.5..4 | 1.5 | Glow radius in LED pitches |
| `timeOffset` | 0..1 | 0 | Where the layer's own loop starts |
| `timeScale` | 0.05..8 | 1 | Layer loops per scene loop; whole numbers stay seamless |

A filter layer reworks the layers below it instead of drawing a picture of its own: its effect gets them composited
so far, and its output is blended like any layer's, so `opacity`, `blend` and `mask` say how much of the result goes
over the original and the look settings above apply to the result. A filter at the bottom works on black. Filters
are the `glitch` and `glint` effects and a `shader` effect with the parameter `"input": true`, which receives the layers below as
`iChannel0`, one flat hex cell per LED at the frame's size; several filter shaders stacked work as passes.

"Trim the loop to the selection" (drag on the timeline below its ruler, then right-click) keeps part of the loop: keyframe
layers are re-keyed, other layers get `timeOffset` and `timeScale`, and the loop gets shorter, so everything keeps its
pace.

## 5. Checking a preset

`scene_check <scene.flyglow> <out.png>` loads the scene like the app, checks the preset block and renders loop phases
as the dock lights them. Lines marked `<--` are problems: a missing title, a control that points nowhere, a seam, or
`TOO DARK`, LEDs the scene lights but the dock leaves dark (output values 1..8) for most of the loop. A gradient into
black is the usual cause: end it a little above the dock's threshold instead.

## 6. Online library

FlyGlow checks `examples/scenes/index.json` on the main branch once a day (Output settings can turn it off) and
downloads every preset whose SHA-256 differs from its built-in copy, so presets published after a release reach users
without reinstalling.

```json
{"format": "flyglow-presets", "version": 1, "presets": [
 {"path": "games/half-life.flyglow", "sha256": "…", "size": 41230, "minApp": "0.3.0"}
]}
```

The maintainer's index script (kept outside the repository) writes the index: an unchanged file keeps its `minApp`, a new or changed one gets the
version in `CMakeLists.txt`, since it may use anything that version has. FlyGlow skips presets with a newer `minApp`
and removes downloaded presets that left the index. To publish presets: add them under `examples/scenes/`, run the
script, commit both and push to main. `FLYGLOW_PRESET_FEED` points a build at another copy of `examples/scenes/`, such
as a fork or a local server, for testing.
