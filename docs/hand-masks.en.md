# Hand-Painted Button Split Guide

For packs that don't split well automatically (glossy/rendered styles), you can paint each button's outline/body/icon yourself in an image editor.
You start from the automatic split, so it's faster than drawing from scratch.

> Korean version: [hand-masks.ko.md](hand-masks.ko.md) · Automatic split and region fixes: [tuning-guide.en.md](tuning-guide.en.md)
>
> **Without Python**, use step 4 of the [web pack tool](web-tool.en.md): export / import / rebuild all work in the browser. Painting (step 2 below) is the same.

## When to use it

| Method | When |
|---|---|
| Automatic split as is | Buttons that split well |
| Region fixes (`"regions"`) | Pieces are right, only their labels (outline/body/icon) are wrong |
| **Hand-painted (this guide)** | The pieces themselves are wrong (text merged into the plate, gloss merged into a picture, ...) |
| No split (`"whole": true`) | You only want whole-button effects, no per-part recoloring |

In vanilla/glossy packs you can mix them: leave most buttons automatic and paint only the few that stand out.

However, **a pack built with the whole (no split) profile keeps every button in one piece**: painted masks and region fixes are ignored, because a few buttons with separate body/icon colors would react differently to the same settings. To use painted buttons, build the pack as vanilla/glossy (add `--whole-fallback` to keep the rest in one piece).

## Setup

- Python 3.10+, `pip install numpy pillow scipy`
- Image editor: Krita or GIMP (free, open `.ora` layer files directly) are recommended. Photoshop and others work with PNGs.
- Run commands from the `GD-Diamond-Theme` folder.

## Step 1: export

```
python tools/hand_masks.py export "pack folder" work-folder --profile glossy --frames GJ_achBtn_001.png GJ_freeStuffBtn_001.png --base-plists "GD Resources"
```

- Use `--match "Btn"` (a regex) instead of `--frames` to export many at once.
- If you already use an override file, add `--overrides overrides.json` to start from the fixed state.
- Pick `--profile` for the pack: `vanilla` for packs with black lines, `glossy` for glossy packs, `whole` for no split.

Each button gets a folder in the work folder:

| File | What |
|---|---|
| `original.png` | The original button (reference, don't edit) |
| `mask.png` | The current automatic split. **Edit this** |
| `<button>.ora` | Original and mask as layers. Edit the "mask" layer in Krita/GIMP |
| `guide.png` | Reference: mask over the original |

Buttons are exported upright (even ones stored rotated in the sheet).

## Step 2: paint

| Color | Meaning |
|---|---|
| Red `#FF0000` | Outline: the ring along the button's edge |
| Green `#00FF00` | Body: the button's plate (background). Gloss and reflections on the plate count as body |
| Blue `#0000FF` | Icon: pictures and text drawn on the plate |
| Black or erased | Keep original (not recolored): shadows, details that must keep their colors |

- **Edges**: soft edges are fine; mixed colors are split proportionally (e.g. 50% red + 50% green).
- **Don't change**: image size, orientation or position. Import reports it as an error.
- **Outside the button (transparent area)**: whatever you paint there doesn't matter.
- **How to decide**: ask "is this the button's plate, or something drawn on it?" Gloss covering the whole plate is body; a highlight only on the picture is icon.
- **Saving, by editor**:
  - Krita/GIMP: open the `.ora`, paint the "mask" layer, and save as `.ora`.
  - Photoshop and others: put `mask.png` as a semi-transparent layer over `original.png`, paint, and save as `mask.png`.

## Step 3: import

```
python tools/hand_masks.py import work-folder overrides.json
```

- Only edited buttons are imported (whichever of `.ora` / `mask.png` was saved last). Untouched buttons are skipped.
- Painted masks are copied to `<override file name>-masks/` and registered in the override file as `"mask": "..."`.
- Check each button in `work-folder/<button>/check.png`: original | painted mask | result preview.

## Step 4: rebuild the theme data

```
python tools/make_theme_data.py "pack folder" "pack folder" --profile glossy --base-plists "GD Resources" --overrides overrides.json
```

Painted buttons use the painted mask instead of the automatic split. A running game usually reloads it within a second.

## Editing again

- Run `export` again (with the same `--overrides`) to get the buttons back with your painted masks in them.
- To drop a painted mask and go back to the automatic split, delete that button's `"mask"` line in the override file.
