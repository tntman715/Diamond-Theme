# LLM Instructions: fixing button splits for Diamond Theme

> **사용법 (한국어)**: 이 파일 전체를 LLM 대화의 첫 메시지(또는 시스템 프롬프트)로 넣고, 그다음 `inspect_frames.py`가 만든
> 그림과 표를 버튼 하나씩 붙여 넣으세요. LLM이 보정 파일 항목(JSON)을 답합니다. 답은 꼭
> `inspect_frames.py --overrides`로 검증한 뒤 쓰세요. 자세한 흐름: [tuning-guide.ko.md](tuning-guide.ko.md)
>
> **How to use (English)**: put this whole file in the first message (or system prompt), then paste the image and table
> from `inspect_frames.py` one button at a time. The model answers with override entries (JSON). Always verify the answer
> with `inspect_frames.py --overrides` before using it. Full workflow: [tuning-guide.en.md](tuning-guide.en.md)

---

## Your task

You review how a tool split one Geometry Dash button image into parts, and correct the parts it got wrong.
The tool already numbered every region. You only decide the class of regions that are wrong. You never edit pixels.

## What you receive for each button

1. **An image with three panels** (if you can see images):
   - left: the original button
   - middle: every region in its own color, with its number
   - right: the current result — red = `outline`, green = `body`, blue = `icon`, gray = `keep` or not a region
2. **A table**, one row per region:

| Column | Meaning |
|---|---|
| `id` | Region number. Only these numbers exist. |
| `class` | Current class |
| `auto` | Class chosen by the automatic rules |
| `where` | Position inside the button: top-left, top, top-right, middle-left, center, middle-right, bottom-left, bottom, bottom-right |
| `color` | Color name and average color |
| `px` | Size in pixels |
| `depth` | Smallest distance from the button's outside, in pixels |
| `edge` | Share (0–1) of the region's border that touches the button's outer edge |
| `reason` | Why the automatic rules chose the class |

The header line says whether the frame is themed, and if not, why.

## The four classes

- `outline` — the white or light ring that runs along the button's outer edge. Usually 1–3 regions.
- `body` — the colored plate of the button: the circle, square or cross shape behind everything. Includes:
  - pieces of the plate cut apart by an icon or text,
  - differently colored corner squares of a cross-shaped button,
  - the plate showing through a hole in an icon (for example the center of a gear).
- `icon` — anything drawn on top of the plate: symbols, pictures, letters, numbers, faces, trophies, arrows.
  Includes art and letters that poke out over the rim, and small details inside an icon (eyes, mouth, highlights).
- `keep` — a part that must keep its original colors and should not be recolored at all. Use rarely.

Black lines and shadows are not regions; ignore them.

## How to decide

Go through the regions in this order:

1. **Outline**: which region is the ring along the outer edge? It must follow the edge around the button.
   White letters or symbols that touch the edge are not the outline; they are `icon`.
2. **Main body**: which region is the plate? It is large, sits just inside the outline, and its `edge` is high.
3. **Other body pieces**: regions at about the same `depth` as the main body (±3) that are part of the plate shape
   (cut-off pieces, cross corners). A region whose `depth` is clearly smaller than the main body's is art poking out
   over the rim → `icon`.
4. **Everything drawn on the plate** → `icon`. Text is always `icon`, even if its color matches the body.
5. **Holes**: a region surrounded by an icon that shows the plate through it (same color as the body) → `body`.
   A separate picture that merely has a body-like color (a green gem on a green button) → `icon`.

Then compare with the current `class` and report only the regions that are wrong.

### Hints for text-only models

Without the image, use:
- `reason` tells you what the rules thought. Check whether that story makes sense for the button's name
  (e.g. `GJ_freeChestBtn` = a chest with the word FREE; `GJ_playBtn` = a play symbol on a plate).
- Letters usually come as several regions of the same color in a row (`where` bottom-left, bottom, bottom, bottom-right).
- Body pieces share a similar `depth`; icons inside the plate have a larger `depth` and `edge` near 0.

## Rules

- Use only `id`s from the table. Never invent numbers.
- Report only regions whose class should change. Do not repeat correct ones.
- Ignore regions with `px` under 12.
- If the whole image is not a button (a label, a decoration, a picture), answer `{"FRAME": {"themed": false}}`.
- If the frame is "NOT themed" but it clearly is a button with a plate, answer with `"themed": true` and the regions
  that make up the body (and any others that need fixing).
- If you are not sure about a region, leave it unchanged and say so in the notes. A wrong fix is worse than no fix.
- Use exactly these class names: `outline`, `body`, `icon`, `keep`.

## Answer format

First a JSON code block with only the frame entry (it is merged into the `"frames"` object of the override file),
then one short line per changed region explaining why.

```json
{
  "FRAME_NAME.png": {
    "regions": { "ID": "CLASS" }
  }
}
```

If nothing needs to change, answer `{}` and one line saying the split is correct.

## Example 1: wrong split

Input header: `GJ_freeChestBtn_001.png (themed)`. The picture is a treasure chest with the word "FREE" under it.
Table (excerpt):

| id | class | auto | where | color | px | depth | edge | reason |
|---|---|---|---|---|---|---|---|---|
| 1 | outline | outline | center | white #f0f0f0 | 3343 | 1.0 | 0.0 | outline: light region touching the outside |
| 2 | body | body | bottom-left | yellow #f5dc1f | 1232 | 8.9 | 0.58 | body: edge piece (edge contact 0.58, same depth as body) |
| 6 | body | body | top | blue #3482d9 | 2866 | 10.0 | 0.49 | body: main body (largest region at body depth) |
| 8 | body | body | bottom | yellow #f2d217 | 1241 | 9.2 | 0.35 | body: piece of the body cut off by an icon or text (same color, touches the edge) |
| 9 | icon | icon | top | yellow #f9bd0a | 1147 | 40.0 | 0.0 | icon: drawn on top of the body |
| 10 | body | body | bottom | yellow #f3d219 | 1071 | 9.2 | 0.25 | body: piece of the body cut off by an icon or text (same color, touches the edge) |
| 13 | body | body | bottom-right | yellow #f1d019 | 1073 | 9.0 | 0.62 | body: edge piece (edge contact 0.62, same depth as body) |

Answer:

```json
{
  "GJ_freeChestBtn_001.png": {
    "regions": { "2": "icon", "8": "icon", "10": "icon", "13": "icon" }
  }
}
```

- 2, 8, 10, 13: the four yellow letters F, R, E, E in a row along the bottom. They are text, not part of the chest's plate.

## Example 2: correct split

Input header: `GJ_garageBtn_001.png (themed)`. A yellow cube face on a green cross-shaped plate with cyan corners.
The table shows the cross arms and corners as `body` (depth 8.5–9.5, edge about 0.5), the cube as `icon` (depth 38),
the eyes and mouth as `icon`, and the white ring as `outline`.

Answer:

```json
{}
```

- The split is correct: plate pieces are body, the cube and its face are icon.

## Self-check before answering

- [ ] Every `id` in my answer appears in the table.
- [ ] I only listed regions whose class changes.
- [ ] Class names are exactly `outline`, `body`, `icon` or `keep`.
- [ ] The frame name is copied exactly, including `.png`.
- [ ] The JSON is valid (quotes around ids and names, no trailing commas).
