# Texture Pack Theme Data Tuning Guide

The Diamond Theme mod splits each button of a texture pack into **outline / body / icon** so it can recolor them and add effects separately.
The split is automatic, but not perfect. This guide shows how to find buttons that were split wrong and fix them.

> Korean version: [tuning-guide.ko.md](tuning-guide.ko.md) · Handing the work to an LLM: [llm-tuning-instructions.md](llm-tuning-instructions.md) · When the pieces themselves are wrong (painting by hand): [hand-masks.en.md](hand-masks.en.md)

Region-level fixes work with two profiles:
- **vanilla**: packs built like vanilla GD (white rim → black line → colored body → icon), including recolored packs. Regions are split by black lines.
- **glossy**: glossy/rendered packs without black lines (Frutiger Aero style, etc.). Regions are split where color or brightness changes sharply; the classification rules are the same as vanilla.

For packs that are hard to split:
- **whole** (no split): the whole button picture is one body with its original colors. No per-part recoloring, but whole-button hue/saturation/brightness, opacity, emboss and glass work on every button. Needs no fixing, so it is the safest choice. A whole pack keeps **every button in one piece**: painted masks and region fixes are ignored, and in game Geode buttons are treated as one piece too (so no button reacts differently to the same settings).
- **Mixing**: add `--whole-fallback` to vanilla/glossy to give whole treatment only to buttons that couldn't be split. An override file can also set `"whole": true` per button.

Replace `--profile vanilla` with `--profile glossy` in the commands below for such packs. The inspect tool reads the profile from an extracted folder automatically; for a pack folder, add `--profile glossy`.

---

## 1. Concepts

### Four classes

| Class | Meaning | Examples |
|---|---|---|
| `outline` | The white (light) rim along the button's edge | The white ring of a round button |
| `body` | The colored plate of the button | The green circle, the cross-shaped plate, differently colored corner squares of a cross |
| `icon` | Pictures and text drawn on top of the body | Play triangle, hammer and wrench, trophy, the word "FREE" |
| `keep` | Parts that must keep their original color | A small detail that has to stay as drawn |

Black lines and shadows never belong to a class; they always stay original.

### Region numbers

The tool cuts a button into **light blobs separated by black lines (regions)** and numbers them from 1.
The same image always gets the same numbers. Fixes are written per number, e.g. "region 5 is icon".

> If you edit the pack's images, the numbers can change. Re-check them after editing.

---

## 2. Setup

- Python 3.10 or newer
- Libraries: `pip install numpy pillow scipy`
- Run all commands from the `GD-Diamond-Theme` folder.
- Packs without `.plist` files (sheet images only) also need GD's own `Resources` folder.
  Steam default: `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\Resources`

Names used in the examples:

| Name | Example |
|---|---|
| Pack folder | `C:\GD\geode\config\geode.texture-loader\packs\My Pack` |
| GD Resources | `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\Resources` |
| Output folder | `inspect-out` (any folder) |
| Override file | `my-pack-overrides.json` |

---

## 3. Workflow

### Step 1: build theme data

```
python tools/make_theme_data.py "My Pack folder" "My Pack folder" --profile vanilla --base-plists "GD Resources"
```

This creates `theme-data/` inside the pack folder. When the pack is enabled in Texture Loader, the mod picks it up automatically.

### Step 2: check in game

Apply a strong color adjustment (e.g. Advanced Settings → Color → colorize the fill) so wrong splits stand out:

- An icon gets the body color → that region was classified `body` by mistake
- Part of the body gets the icon color → that region was classified `icon` by mistake
- The button doesn't change at all → the whole frame is not themed (`themed`)

### Step 3: find the frame name

Button names (frame names) are GD's sheet names, e.g. `GJ_playBtn_001.png`. If you don't know one, list them:

```
python tools/inspect_frames.py "My Pack folder" inspect-out --list --base-plists "GD Resources"
python tools/inspect_frames.py "My Pack folder" inspect-out --list --match "garage|reward" --base-plists "GD Resources"
```

| frame | themed | why | regions (outline/body/icon/keep) |
|---|---|---|---|
| GJ_garageBtn_001.png | yes | themed | 1/8/4/0 |

Without `--match` it lists every frame with a button-like name.

### Step 4: inspect a button

```
python tools/inspect_frames.py "My Pack folder" inspect-out --frames GJ_freeChestBtn_001.png --base-plists "GD Resources"
```

This writes `inspect-out/GJ_freeChestBtn_001.png`:

- **Left**: original
- **Middle**: each region in its own color, with its number
- **Right**: result (red = outline, green = body, blue = icon, gray = keep)

The same information is printed as a table (and saved as `.json`):

| id | class | auto | where | color | px | depth | edge | reason |
|---|---|---|---|---|---|---|---|---|
| 6 | body | body | top | blue #3482d9 | 2866 | 10.0 | 0.49 | body: main body (largest region at body depth) |
| 9 | icon | icon | top | yellow #f9bd0a | 1147 | 40.0 | 0.0 | icon: drawn on top of the body |
| 2 | body | body | bottom-left | yellow #f5dc1f | 1232 | 8.9 | 0.58 | body: edge piece (…) |

| Column | Meaning |
|---|---|
| `id` | Region number (used in fixes) |
| `class` | Current class (after fixes; **bold** = changed by a fix) |
| `auto` | Automatic class |
| `where` | Position inside the button (top-left, center, bottom, …) |
| `color` | Average color |
| `px` | Size in pixels |
| `depth` | Smallest distance from the button's outside. Body pieces usually share a similar value; art poking out over the rim has a smaller value; icons inside have larger values |
| `edge` | Share of the region's border that touches the button's edge (0–1) |
| `reason` | Why the automatic class was chosen |

### Step 5: write the override file

Example: the letters "FREE" on the treasure chest button (regions 2, 8, 10, 13) were classified as body → make them icon.

```json
{
  "frames": {
    "GJ_freeChestBtn_001.png": {
      "regions": { "2": "icon", "8": "icon", "10": "icon", "13": "icon" }
    }
  }
}
```

| Key | Use |
|---|---|
| `"regions": {"id": "class"}` | Class is `outline`, `body`, `icon` or `keep` |
| `"themed": false` | Never touch this frame (it isn't a button, etc.) |
| `"themed": true` | Theme a frame that was skipped (a button whose name doesn't look like one, etc.). Usually together with `regions` |

Regions you don't list keep their automatic class.

### Step 6: verify

```
python tools/inspect_frames.py "My Pack folder" inspect-out --frames GJ_freeChestBtn_001.png --overrides my-pack-overrides.json --base-plists "GD Resources"
```

- Changed regions show their `class` in **bold** and appear in the right-hand image.
- Mistakes are reported with `!`:
  - `frame '…' does not exist in this pack` → typo in the frame name
  - `region 99 does not exist (this frame has regions 1..16)` → wrong number
  - `unknown class 'iconn'` → typo in the class name

### Step 7: rebuild and check in game

```
python tools/make_theme_data.py "My Pack folder" "My Pack folder" --profile vanilla --base-plists "GD Resources" --overrides my-pack-overrides.json
```

A running game usually reloads it within a second. If not, restart GD.

---

## 4. How to decide

When unsure, ask: **"Is this part the button's plate (background), or something drawn on the plate?"**

| Situation | Answer |
|---|---|
| Text or art poking out over the rim (FREE STUFF, MORE GAMES, Newgrounds tank) | `icon` |
| A piece of the body cut off by text | `body` |
| Differently colored corner squares of a cross-shaped button | `body` |
| Body showing through a hole in an icon (center of a gear) | `body` |
| A picture with a body-like color drawn separately (green gem on the Paths button) | `icon` |
| Small details inside an icon (cube eyes, mouth) | `icon` |
| The white ring along the button's edge | `outline` |
| White text that became `outline` but isn't the ring | `icon` |
| Something that must keep its original color | `keep` |
| Specks under 12 `px` | leave alone |

Numbers to help (the automatic rules use these):
- Body pieces have similar `depth` (usually within ±3). A region with clearly smaller `depth` than the main body is art poking out over the rim.
- Body pieces tend to have a large `edge` (0.2+). A region with `edge` 0 is surrounded by the body (except holes like a gear's center).

---

## 5. Working with an LLM

Give the LLM [llm-tuning-instructions.md](llm-tuning-instructions.md) first, then paste the image and table from step 4. It answers with override entries.

- **Vision models**: give the image and the table. This is the most accurate.
- **Text-only models**: the table alone works but is less accurate; the model decides from `where`, `color`, `depth`, `edge` and `reason`.
- **Making task files in one go**: add `--tasks` to the step 4 command and the output folder gets `<button>.md` (instructions + table) and `<button>.png` for every button, ready to send.
- **Collecting answers**: save each reply in one folder, then `python tools/collect_answers.py <answers folder> my-pack-overrides.json --check "My Pack folder"` merges them into one override file, skipping and reporting wrong region numbers or typos.
- Send one button (or a few) at a time. Big batches cause more mistakes.
- **Always verify LLM output with step 6** before using it. The tool catches wrong numbers and names.

---

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `frame not found` | Typo in the name (case matters) |
| Inspect works but nothing changes in game | Theme data only covers GJ_GameSheet03/04, GJ_LaunchSheet, GJ_button_0N and GJ_square0N images. Frames in other sheets are not themed |
| `not themed: not a button name` | The name doesn't look like a button → if it is one, `"themed": true` |
| `not themed: no body found` | No body found → `"themed": true` and mark the body regions `body` |
| `not themed: body too small (…)` | Body is under 15% of the button → if the split is right, `"themed": true` |
| `not themed: only a body, no outline or icon` | A plain plate → `"themed": true` if you want it themed |
| A fix doesn't show in game | Re-run step 7; restart GD |
| Numbers differ from before | The pack's images changed → start again from step 4 |

**Other profiles (diamond, generic)**: region-number fixes are not supported. Only the list keys of the override file work:
- `"themed": [...]`: force the theme on
- `"plain": [...]`: force the theme off
- `"icons": [...]`: split an icon off the outline it touches
