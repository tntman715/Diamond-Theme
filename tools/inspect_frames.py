"""
프레임 분석 도구 (vanilla 프로필): 버튼 하나가 어떻게 나뉘었는지 보여 주고, 보정 파일을 검증한다.

사람이나 LLM이 "영역 N은 아이콘" 같은 판단만 하면 되도록, 판단에 필요한 정보를 한 번에 낸다.
  <출력 폴더>/<프레임>.png   왼쪽부터 원본 | 영역 번호 | 결과(빨강 테두리, 초록 바탕, 파랑 아이콘, 회색 원본 유지)
  <출력 폴더>/<프레임>.json  영역 표 (번호, 판정, 자동 판정, 이유, 크기, 위치, 색, 깊이, 가장자리 접촉)
  화면 출력                  같은 표를 Markdown으로 + 보정 파일에 붙여 넣을 틀

영역 번호는 extract_masks.py가 쓰는 번호와 같다 (같은 그림이면 항상 같은 번호).
보정 파일(--overrides)을 주면 보정이 적용된 결과를 보여 주고, 없는 영역 번호나 잘못된 종류 이름을 알려 준다.

사용:
  python inspect_frames.py <팩.zip | 팩 폴더 | 추출 결과 폴더(atlas.json)> <출력 폴더> --frames GJ_garageBtn_001.png ...
      [--match 정규식] [--overrides overrides.json] [--base-plists <GD Resources>] [--profile vanilla|glossy]
  python inspect_frames.py <팩> <출력 폴더> --list [--match 정규식]     이름에 맞는 프레임의 판정만 한 줄씩 (그림 없음)

보정 파일 형식 (extract_masks.py / make_theme_data.py --overrides 와 같은 파일):
  {"frames": {"GJ_freeChestBtn_001.png": {"regions": {"14": "icon", "15": "icon"}},
              "GJ_someBtn_001.png": {"themed": false}}}
  종류: outline(흰 테두리) | body(바탕) | icon(아이콘/글자) | keep(원본 그대로, 색을 바꾸지 않음)
"""
import argparse
import colorsys
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

import extract_masks as em

CLASS_COLORS = {"outline": (235, 64, 52), "body": (60, 200, 80), "icon": (60, 110, 255), "keep": (128, 128, 128)}
PANEL_H = 360


def color_name(hex_color: str) -> str:
    r, g, b = (int(hex_color[i:i + 2], 16) / 255 for i in (1, 3, 5))
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if v < 0.2:
        return "black"
    if s < 0.15:
        return "white" if v > 0.85 else "light gray" if v > 0.6 else "gray" if v > 0.35 else "dark gray"
    names = [(15, "red"), (40, "orange"), (65, "yellow"), (160, "green"), (195, "cyan"), (255, "blue"),
             (290, "purple"), (335, "pink"), (360, "red")]
    hue = next(n for limit, n in names if h * 360 < limit)
    return ("dark " if v < 0.5 else "light " if s < 0.45 and v > 0.8 else "") + hue


def where(x: float, y: float) -> str:
    col = "left" if x < 0.36 else "right" if x > 0.64 else "center"
    row = "top" if y < 0.36 else "bottom" if y > 0.64 else "middle"
    return "center" if (col, row) == ("center", "middle") else f"{row}-{col}" if col != "center" else row


# ---------------------------------------------------------------- 프레임 읽기

def load_source(src: Path, base_plists: Path | None):
    """프레임 이름 → (rgb, alpha, rotated, single) 을 돌려주는 함수와 전체 이름 목록"""
    textures = []   # (이미지 경로, frames dict, single)
    atlas = src / "atlas.json"
    if atlas.exists():
        data = json.loads(atlas.read_text(encoding="utf-8"))
        for t in data["textures"]:
            textures.append((src / t["image"], t["frames"], t["kind"] == "single"))
    else:
        root = em.open_pack(src)
        for png in sorted(root.glob("*-uhd.png")):
            plist = png.with_suffix(".plist")
            if not plist.exists() and base_plists and (base_plists / plist.name).exists():
                plist = base_plists / plist.name
            if png.with_suffix(".fnt").exists():
                continue
            if plist.exists():
                textures.append((png, em.load_frames(plist), False))
            else:
                w, h = Image.open(png).size
                textures.append((png, {png.name.replace("-uhd", ""): {"rect": [0, 0, w, h], "rotated": False}}, True))
    index = {}
    for path, frames, single in textures:
        for name, fr in frames.items():
            index[name] = (path, fr, single)
    cache = {}

    def get(name):
        path, fr, single = index[name]
        if path not in cache:
            cache.clear()   # 시트는 크므로 하나만 들고 있는다
            cache[path] = np.asarray(Image.open(path).convert("RGBA")).astype(np.float32) / 255
        im = cache[path]
        x, y, w, h = fr["rect"]
        return im[y:y + h, x:x + w, :3], im[y:y + h, x:x + w, 3], bool(fr.get("rotated")), single

    return get, list(index)


# ---------------------------------------------------------------- 그림

def font(size):
    for f in ("arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def render(rgb, aa, info, rotated: bool, title: str, show_result: bool = True) -> Image.Image:
    """show_result=False: 결과 패널 없이 원본 | 영역 번호만 (채점용 블라인드 과제)"""
    labels = info["labels"]
    h, w = labels.shape
    bg = np.zeros((h, w, 3), np.float32) + np.array([0.24, 0.24, 0.35])
    original = bg * (1 - aa[..., None]) + rgb * aa[..., None]

    regions = {r["id"]: r for r in info["regions"]}
    region_img = np.zeros((h, w, 3), np.float32) + 0.08
    result_img = np.zeros((h, w, 3), np.float32) + 0.08
    result_img[aa > 0.02] = 0.3   # 선/그림자 등 영역이 아닌 부분 (원본 유지)
    label_pts = {}
    for rid, r in regions.items():
        comp = labels == rid
        hue = (rid * 0.61803) % 1
        region_img[comp] = colorsys.hsv_to_rgb(hue, 0.55, 0.95)
        result_img[comp] = np.array(CLASS_COLORS[r["class"]]) / 255
        if r["px"] >= 12:
            # 영역 안쪽 깊숙한 점에 번호를 쓴다 (고리 모양 영역은 중심이 영역 밖일 수 있다)
            ys, xs = np.nonzero(comp)
            y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
            d = ndimage.distance_transform_edt(np.pad(comp[y0:y1, x0:x1], 1))[1:-1, 1:-1]
            py, px = np.unravel_index(np.argmax(d), d.shape)
            label_pts[rid] = (px + x0, py + y0)

    panels = []
    for arr in (original, region_img, result_img) if show_result else (original, region_img):
        im = Image.fromarray(np.clip(arr * 255, 0, 255).astype(np.uint8))
        if rotated:
            im = im.rotate(90, expand=True)
        panels.append(im)
    if rotated:   # rotate(90, expand)는 (x, y) → (y, w - 1 - x)
        label_pts = {k: (y, w - 1 - x) for k, (x, y) in label_pts.items()}
    scale = PANEL_H / panels[0].height
    pw = int(panels[0].width * scale)
    panels = [p.resize((pw, PANEL_H), Image.NEAREST) for p in panels]

    top = 34
    out = Image.new("RGB", (pw * len(panels) + 10 * (len(panels) - 1), PANEL_H + top + 26), (20, 20, 24))
    draw = ImageDraw.Draw(out)
    draw.text((8, 6), title, font=font(18), fill=(255, 255, 255))
    for i, p in enumerate(panels):
        out.paste(p, (i * (pw + 10), top))
    f = font(max(12, min(22, int(pw / 14))))
    for rid, (x, y) in label_pts.items():
        draw.text((pw + 10 + x * scale, top + y * scale), str(rid), font=f, fill=(255, 255, 255),
                  stroke_width=3, stroke_fill=(0, 0, 0), anchor="mm")
    if not show_result:
        draw.text((8, PANEL_H + top + 4), "original | regions (numbers)", font=font(14), fill=(200, 200, 200))
        return out
    draw.text((8, PANEL_H + top + 4), "original | regions (numbers) | result:", font=font(14), fill=(200, 200, 200))
    x = 2 * (pw + 10)
    for k, c in CLASS_COLORS.items():
        draw.rectangle((x, PANEL_H + top + 6, x + 12, PANEL_H + top + 18), fill=c)
        draw.text((x + 16, PANEL_H + top + 4), k, font=font(14), fill=(220, 220, 220))
        x += 16 + len(k) * 8 + 14
    return out


# ---------------------------------------------------------------- 분석

def analyze(get, name: str, overrides: dict, profile: str = "vanilla"):
    """프레임 하나를 나눠 보고 (rgb, alpha, 회전 여부, info, 표에 쓸 영역 목록)을 돌려준다"""
    rgb, aa, rotated, single = get(name)
    info = {}
    force = True if name in overrides.get("themed", []) else False if name in overrides.get("plain", []) else None
    over = overrides.get("frames", {}).get(name)
    em.build_vanilla_mask(rgb, aa, np.zeros(rgb.shape, np.uint8), force, name, single, over, info,
                          segment=em.segment_for(profile, over))
    h, w = info["labels"].shape
    rows = []
    for r in info["regions"]:
        if r["px"] < 12 and r["class"] == "keep":
            continue
        ys, xs = np.nonzero(info["labels"] == r["id"])
        cx, cy = xs.mean() / w, ys.mean() / h
        if rotated:   # 화면에 보이는 방향 기준 위치
            cx, cy = cy, 1 - cx
        r.update(pos=[round(cx, 2), round(cy, 2)], where=where(cx, cy), colorName=color_name(r["color"]))
        rows.append(r)
    return rgb, aa, rotated, info, rows


# ---------------------------------------------------------------- 메인

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--frames", nargs="*", default=[], help="프레임 이름 (예: GJ_garageBtn_001.png)")
    ap.add_argument("--match", help="이 정규식에 맞는 프레임 전부")
    ap.add_argument("--overrides", type=Path, help="적용해서 볼 보정 파일")
    ap.add_argument("--base-plists", type=Path, help="팩에 plist가 없을 때 쓸 GD Resources 폴더")
    ap.add_argument("--profile", choices=["vanilla", "glossy"],
                    help="vanilla: 검은 선으로 나눔, glossy: 색/밝기 경계로 나눔 (광택 팩). "
                         "추출 결과 폴더면 기본값은 그 폴더의 프로필, 아니면 vanilla")
    ap.add_argument("--tasks", action="store_true",
                    help="LLM에게 보낼 과제 파일도 쓴다: 출력 폴더에 프레임마다 <프레임>.md (지침 안내 + 표) + <프레임>.png")
    ap.add_argument("--list", action="store_true",
                    help="그림/표 없이 프레임마다 테마 적용 여부와 이유만 한 줄씩 (--match 없으면 버튼 이름인 프레임 전부)")
    args = ap.parse_args()

    overrides = {}
    if args.overrides:
        overrides = json.loads(args.overrides.read_text(encoding="utf-8"))
    frame_over = overrides.get("frames", {})

    get, names = load_source(args.source, args.base_plists)
    profile = args.profile
    if not profile and (args.source / "atlas.json").exists():
        profile = json.loads((args.source / "atlas.json").read_text(encoding="utf-8")).get("profile")
    profile = profile if profile in ("vanilla", "glossy") else "vanilla"
    wanted = list(args.frames)
    if args.list and not args.match and not wanted:
        wanted = [n for n in names if em.VAN_BUTTON_RE.search(n)]
    if args.match:
        wanted += [n for n in names if re.search(args.match, n) and n not in wanted]
    missing = [n for n in wanted if n not in names]
    for n in missing:
        print(f"! frame not found: {n}")
    for n in frame_over:
        if n not in names:
            print(f"! overrides: frame '{n}' does not exist in this pack")
    wanted = [n for n in wanted if n in names]
    if not wanted:
        print("no frames to inspect (use --frames or --match)")
        return 1
    args.out.mkdir(parents=True, exist_ok=True)

    if args.list:
        print("| frame | themed | why | regions (outline/body/icon/keep) |")
        print("|---|---|---|---|")
        for name in wanted:
            _, _, _, info, _ = analyze(get, name, overrides, profile)
            counts = {c: sum(1 for r in info["regions"] if r["class"] == c and r["px"] >= 12) for c in CLASS_COLORS}
            print(f"| {name} | {'yes' if info['themed'] else 'no'} | {info.get('why', '')} | "
                  f"{counts['outline']}/{counts['body']}/{counts['icon']}/{counts['keep']} |")
        return 0

    errors = 0
    for name in wanted:
        rgb, aa, rotated, info, rows = analyze(get, name, overrides, profile)
        stem = name.removesuffix(".png")
        h, w = info["labels"].shape
        title = f"{name}  —  {'THEMED' if info['themed'] else 'not themed: ' + info.get('why', '')}"
        render(rgb, aa, info, rotated, title).save(args.out / f"{stem}.png")
        (args.out / f"{stem}.json").write_text(json.dumps(
            {"frame": name, "size": [w, h] if not rotated else [h, w], "themed": info["themed"],
             "why": info.get("why", ""), "errors": info["errors"],
             "regions": rows}, ensure_ascii=False, indent=1), encoding="utf-8")

        heading = f"## {name}  ({'themed' if info['themed'] else 'NOT themed: ' + info.get('why', '')})"
        table = ["| id | class | auto | where | color | px | depth | edge | reason |", "|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            mark = "**" if r["class"] != r["auto"] else ""
            table.append(f"| {r['id']} | {mark}{r['class']}{mark} | {r['auto']} | {r['where']} | {r['colorName']} "
                         f"{r['color']} | {r['px']} | {r['depth']} | {r['edgeContact']} | {r['reason']} |")
        print("\n" + heading)
        print(f"image: {args.out / (stem + '.png')}")
        print("\n".join(table))
        if args.tasks:
            (args.out / f"{stem}.md").write_text("\n".join([
                f"# Fix task: {name}", "",
                "Follow `docs/llm-tuning-instructions.md`. Below is the automatic split of this button; "
                "correct only the wrong regions.",
                f"Image: `{stem}.png` (left: original, middle: region numbers, right: current split)", "",
                heading, "", *table, ""]), encoding="utf-8")
        for e in info["errors"]:
            print(f"! {e}")
            errors += 1
        print('override template: "%s": {"regions": {"<id>": "icon"}}' % name)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
